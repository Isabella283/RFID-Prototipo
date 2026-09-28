#include <SPI.h>
#include <MFRC522.h>
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>

// ======================================================
// CONFIGURACION WIFI
// ======================================================

const char* ssid = "Wokwi-GUEST";
const char* password = "";

// ======================================================
// CONFIGURACION MQTT
// ======================================================

const char* mqtt_server = "broker.hivemq.com";
const int mqtt_port = 1883;

const char* mqttCommandTopic = "rfid/comandos";
const char* mqttTelemetryTopic = "rfid/telemetria";
const char* mqttStateTopic = "rfid/estado";

// ======================================================
// THINGSPEAK
// ======================================================

const char* channelID = "3488404";
const char* channelWriteAPIKey = "TU_API_KEY";

// ======================================================
// IDENTIFICACION DEL DISPOSITIVO
// ======================================================

const String device_ID = "ESP32-RFID";

// ======================================================
// PINES
// ======================================================

#define SS_PIN_1 5
#define RST_PIN_1 17

#define SS_PIN_2 16
#define RST_PIN_2 27

#define RELAY_PIN 4

#define POTENTIOMETER_PIN 34

// ======================================================
// PARAMETROS DEL PROYECTO
// ======================================================

#define UID_AUTORIZADO "01020304"

const unsigned long TIEMPO_BLOQUEO_RECHAZADA = 12000;
const unsigned long PERIODO_PUBLICACION = 22000;
const unsigned long INTERVALO_RECONEXION = 12000;
const unsigned long INTERVALO_MUESTREO = 1750;

const unsigned long TIEMPO_MOSTRAR_UID = 1200;
const unsigned long TIEMPO_MENSAJE_RECHAZO = 2000;
const unsigned long TIEMPO_ESTADO_SEGURO = 2000;
const unsigned long TIEMPO_ANTIREPETICION_RFID = 500;

// ======================================================
// DIAGNOSTICO MQTT
// ======================================================

const unsigned long INTERVALO_DIAGNOSTICO_MQTT = 5000;

unsigned long ultimoDiagnosticoMQTT = 0;

// ======================================================
// OBJETOS
// ======================================================

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

MFRC522 rfid1(SS_PIN_1, RST_PIN_1);
MFRC522 rfid2(SS_PIN_2, RST_PIN_2);

LiquidCrystal_I2C lcd(0x27, 16, 2);

// ======================================================
// ESTADO INDIVIDUAL DE CADA TARJETA
// ======================================================

const int MAX_TARJETAS = 20;

struct EstadoTarjeta {

  String uid;

  int rechazos;

  bool bloqueada;

  unsigned long bloqueoHasta;
};

EstadoTarjeta tarjetas[MAX_TARJETAS];

int cantidadTarjetas = 0;

// ======================================================
// VARIABLES DEL SISTEMA
// ======================================================

String modoActual = "AUTO";

String ultimoUID = "";

bool ultimoAccesoPermitido = false;

unsigned long ultimaPublicacion = 0;

unsigned long ultimaReconexion = 0;

unsigned long ultimoMuestreo = 0;

unsigned long ultimoProcesamientoRFID = 0;

// ======================================================
// CONTROL NO BLOQUEANTE DEL RELE
// ======================================================

bool relePorAcceso = false;

unsigned long releAccesoHasta = 0;

// ======================================================
// CONTROL NO BLOQUEANTE DE LCD
// ======================================================

enum EstadoPantalla {

  PANTALLA_BIENVENIDO,

  PANTALLA_UID,

  PANTALLA_ACCESO_PERMITIDO,

  PANTALLA_RECHAZO,

  PANTALLA_ESTADO_SEGURO,

  PANTALLA_BLOQUEADA
};

EstadoPantalla estadoPantalla =
  PANTALLA_BIENVENIDO;

unsigned long pantallaHasta = 0;

// ======================================================
// LECTURA RFID PENDIENTE
// ======================================================

bool lecturaRFIDPendiente = false;

String uidRFIDPendiente = "";

int indiceTarjetaPendiente = -1;

bool accesoPermitidoPendiente = false;

String lectorRFIDPendiente = "";

// ======================================================
// RECONEXION WIFI NO BLOQUEANTE
// ======================================================

bool intentandoReconectarWiFi = false;

unsigned long inicioReconectarWiFi = 0;

// ======================================================
// DECLARACION DE FUNCIONES
// ======================================================

void conectToWiFi();

void reconect();

void procesarTarjeta(
  MFRC522 &rfid,
  const String& lector
);

void finalizarLecturaRFID();

void actualizarTiemposSistema();

void actualizarPantalla();

void mostrarBienvenido();

void mostrarUID(
  const String& uid
);

void mostrarAccesoPermitido();

void mostrarRechazo(
  int rechazos
);

void mostrarEstadoSeguro();

void mostrarTarjetaBloqueada();

void sendRFIDData(
  const String& uid,
  bool accesoPermitido
);

void receiveThingSpeakData();

int simulateRFID();

void exampleJSON();

void mqttCallback(
  char* topic,
  byte* payload,
  unsigned int length
);

void handleCommand(
  const String& command
);

bool publishmqtt(
  const String& uid,
  bool accesoPermitido
);

int readPotentiometer();

void publishConfirmation();

int buscarTarjeta(
  const String& uid
);

int obtenerTarjeta(
  const String& uid
);

void mostrarEstadoTarjeta(
  const String& uid
);

// ======================================================
// SETUP
// ======================================================

void setup() {

  Serial.begin(115200);

  Serial.println("ESP32 iniciado");

  // ====================================================
  // RELAY
  // ====================================================

  pinMode(
    RELAY_PIN,
    OUTPUT
  );

  digitalWrite(
    RELAY_PIN,
    LOW
  );

  // ====================================================
  // POTENCIOMETRO
  // ====================================================

  pinMode(
    POTENTIOMETER_PIN,
    INPUT
  );

  // ====================================================
  // LCD
  // ====================================================

  lcd.init();

  lcd.backlight();

  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("Conectando WiFi");

  // ====================================================
  // WIFI
  // ====================================================

  conectToWiFi();

  // ====================================================
  // CONFIGURAR MQTT
  // ====================================================

  mqttClient.setServer(
    mqtt_server,
    mqtt_port
  );

  mqttClient.setCallback(
    mqttCallback
  );

  // ====================================================
  // PRUEBA DIRECTA DE CONEXION MQTT
  // ====================================================

  Serial.println(
    "Intentando conectar a MQTT..."
  );

  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("Conectando MQTT");

  String clientId =
    device_ID +
    "-TEST-" +
    String(
      random(0xffff),
      HEX
    );

  if (
    mqttClient.connect(
      clientId.c_str()
    )
  ) {

    Serial.println(
      "MQTT CONECTADO"
    );

    bool suscrito =
      mqttClient.subscribe(
        mqttCommandTopic
      );

    Serial.println(
      "Suscripcion MQTT realizada"
    );

    lcd.clear();

    lcd.setCursor(0, 0);
    lcd.print("MQTT OK");

    lcd.setCursor(0, 1);

    if (suscrito) {
      lcd.print("Suscrito");
    } else {
      lcd.print("Sin suscripcion");
    }

  } else {

    Serial.print(
      "MQTT ERROR: "
    );

    Serial.println(
      mqttClient.state()
    );

    lcd.clear();

    lcd.setCursor(0, 0);
    lcd.print("MQTT ERROR");

    lcd.setCursor(0, 1);
    lcd.print("Estado: ");

    lcd.print(
      mqttClient.state()
    );
  }

  // ====================================================
  // DEJAR EL RESULTADO VISIBLE
  // ====================================================

  delay(5000);

  // ====================================================
  // RFID
  // ====================================================

  SPI.begin();

  rfid1.PCD_Init();

  delay(50);

  rfid2.PCD_Init();

  delay(50);

  // ====================================================
  // PANTALLA INICIAL
  // ====================================================

  mostrarBienvenido();

  Serial.println("==============================");
  Serial.println("Sistema iniciado");
  Serial.println("RFID 1 listo");
  Serial.println("RFID 2 listo");
  Serial.println("UID autorizado: 01020304");
  Serial.println("Relay: GPIO 4");
  Serial.println("Estado seguro: APAGADO");
  Serial.println("Muestreo: 1750 ms");
  Serial.println("==============================");
}

// ======================================================
// LOOP
// ======================================================

void loop() {

  // ====================================================
  // MQTT Y RECONEXION
  // ====================================================

  reconect();

  mqttClient.loop();

  // ====================================================
  // DIAGNOSTICO MQTT
  // ====================================================
  // IMPORTANTE:
  // El diagnostico se mantiene por Serial.
  // Ya no modifica el LCD para no sobrescribir
  // "Bienvenido".
  // ====================================================

  if (
    millis() - ultimoDiagnosticoMQTT >=
    INTERVALO_DIAGNOSTICO_MQTT
  ) {

    ultimoDiagnosticoMQTT =
      millis();

    Serial.print(
      "DIAGNOSTICO MQTT - conectado: "
    );

    Serial.println(
      mqttClient.connected()
        ? "SI"
        : "NO"
    );

    if (
      !mqttClient.connected()
    ) {

      Serial.print(
        "Estado MQTT: "
      );

      Serial.println(
        mqttClient.state()
      );
    }
  }

  // ====================================================
  // ACTUALIZAR TEMPORIZADORES
  // ====================================================

  actualizarTiemposSistema();

  actualizarPantalla();

  // ====================================================
  // FINALIZAR LECTURA RFID PENDIENTE
  // ====================================================

  if (
    lecturaRFIDPendiente &&
    millis() >= pantallaHasta
  ) {

    finalizarLecturaRFID();
  }

  // ====================================================
  // MUESTREO
  // ====================================================

  if (
    millis() - ultimoMuestreo >=
    INTERVALO_MUESTREO
  ) {

    ultimoMuestreo = millis();

    int valorMuestreado =
      readPotentiometer();

    Serial.print("Muestreo: ");

    Serial.println(
      valorMuestreado
    );
  }

  // ====================================================
  // PUBLICACION PERIODICA
  // ====================================================

  if (
    millis() - ultimaPublicacion >=
    PERIODO_PUBLICACION
  ) {

    ultimaPublicacion = millis();

    if (
      ultimoUID != ""
    ) {

      Serial.println(
        "------------------------------"
      );

      Serial.println(
        "Publicacion periodica"
      );

      sendRFIDData(
        ultimoUID,
        ultimoAccesoPermitido
      );

      publishmqtt(
        ultimoUID,
        ultimoAccesoPermitido
      );

      Serial.println(
        "------------------------------"
      );
    }
  }

  // ====================================================
  // RFID
  // ====================================================

  if (
    !lecturaRFIDPendiente &&
    millis() - ultimoProcesamientoRFID >=
    TIEMPO_ANTIREPETICION_RFID
  ) {

    // ==================================================
    // RFID 1
    // ==================================================

    if (
      rfid1.PICC_IsNewCardPresent() &&
      rfid1.PICC_ReadCardSerial()
    ) {

      procesarTarjeta(
        rfid1,
        "RFID 1"
      );

      rfid1.PICC_HaltA();

      rfid1.PCD_StopCrypto1();

      ultimoProcesamientoRFID =
        millis();

      return;
    }

    // ==================================================
    // RFID 2
    // ==================================================

    if (
      rfid2.PICC_IsNewCardPresent() &&
      rfid2.PICC_ReadCardSerial()
    ) {

      procesarTarjeta(
        rfid2,
        "RFID 2"
      );

      rfid2.PICC_HaltA();

      rfid2.PCD_StopCrypto1();

      ultimoProcesamientoRFID =
        millis();

      return;
    }
  }
}

// ======================================================
// ACTUALIZAR TIEMPOS DEL SISTEMA
// ======================================================

void actualizarTiemposSistema() {

  unsigned long ahora =
    millis();

  // ====================================================
  // COMPROBAR FIN DE BLOQUEOS DE TARJETAS
  // ====================================================

  for (
    int i = 0;
    i < cantidadTarjetas;
    i++
  ) {

    if (
      tarjetas[i].bloqueada &&
      ahora >= tarjetas[i].bloqueoHasta
    ) {

      Serial.println(
        "================================"
      );

      Serial.println(
        "Bloqueo de tarjeta finalizado"
      );

      Serial.print(
        "Tarjeta desbloqueada: "
      );

      Serial.println(
        tarjetas[i].uid
      );

      Serial.println(
        "Reiniciando contador de esta tarjeta"
      );

      Serial.println(
        "================================"
      );

      tarjetas[i].bloqueada =
        false;

      tarjetas[i].bloqueoHasta =
        0;

      tarjetas[i].rechazos =
        0;

      if (
        modoActual != "ACTIVAR"
      ) {

        digitalWrite(
          RELAY_PIN,
          LOW
        );
      }

      if (
        !lecturaRFIDPendiente
      ) {

        mostrarBienvenido();
      }

      if (
        ultimoUID ==
        tarjetas[i].uid
      ) {

        publishmqtt(
          tarjetas[i].uid,
          false
        );
      }
    }
  }

  // ====================================================
  // FIN DEL TIEMPO DE ACCESO AUTORIZADO
  // ====================================================

  if (
    relePorAcceso &&
    ahora >= releAccesoHasta
  ) {

    relePorAcceso =
      false;

    releAccesoHasta =
      0;

    if (
      modoActual != "ACTIVAR"
    ) {

      digitalWrite(
        RELAY_PIN,
        LOW
      );

      Serial.println(
        "Rele apagado"
      );

      Serial.println(
        "Estado seguro"
      );

      mostrarEstadoSeguro();

    } else {

      Serial.println(
        "Tiempo de acceso RFID finalizado"
      );

      Serial.println(
        "Relay permanece activado por modo MQTT ACTIVAR"
      );
    }
  }
}

// ======================================================
// ACTUALIZAR PANTALLA
// ======================================================

void actualizarPantalla() {

  if (
    estadoPantalla ==
    PANTALLA_ESTADO_SEGURO
  ) {

    if (
      millis() >= pantallaHasta
    ) {

      mostrarBienvenido();
    }
  }

  if (
    estadoPantalla ==
    PANTALLA_RECHAZO
  ) {

    if (
      millis() >= pantallaHasta
    ) {

      mostrarBienvenido();
    }
  }
}

// ======================================================
// MOSTRAR BIENVENIDO
// ======================================================

void mostrarBienvenido() {

  estadoPantalla =
    PANTALLA_BIENVENIDO;

  pantallaHasta =
    0;

  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("Bienvenido");
}

// ======================================================
// MOSTRAR UID
// ======================================================

void mostrarUID(
  const String& uid
) {

  estadoPantalla =
    PANTALLA_UID;

  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("UID:");

  lcd.setCursor(0, 1);
  lcd.print(uid);
}

// ======================================================
// MOSTRAR ACCESO PERMITIDO
// ======================================================

void mostrarAccesoPermitido() {

  estadoPantalla =
    PANTALLA_ACCESO_PERMITIDO;

  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("Acceso Permitido");

  lcd.setCursor(0, 1);
  lcd.print("Rele activado");
}

// ======================================================
// MOSTRAR RECHAZO
// ======================================================

void mostrarRechazo(
  int rechazos
) {

  estadoPantalla =
    PANTALLA_RECHAZO;

  pantallaHasta =
    millis() +
    TIEMPO_MENSAJE_RECHAZO;

  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("Acceso Denegado");

  lcd.setCursor(0, 1);
  lcd.print("Rechazos: ");

  lcd.print(
    rechazos
  );
}

// ======================================================
// MOSTRAR ESTADO SEGURO
// ======================================================

void mostrarEstadoSeguro() {

  estadoPantalla =
    PANTALLA_ESTADO_SEGURO;

  pantallaHasta =
    millis() +
    TIEMPO_ESTADO_SEGURO;

  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("Rele apagado");

  lcd.setCursor(0, 1);
  lcd.print("Estado seguro");
}

// ======================================================
// MOSTRAR TARJETA BLOQUEADA
// ======================================================

void mostrarTarjetaBloqueada() {

  estadoPantalla =
    PANTALLA_BLOQUEADA;

  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("Tarjeta bloqueada");

  lcd.setCursor(0, 1);
  lcd.print("Espere unos seg.");
}

// ======================================================
// BUSCAR TARJETA
// ======================================================

int buscarTarjeta(
  const String& uid
) {

  for (
    int i = 0;
    i < cantidadTarjetas;
    i++
  ) {

    if (
      tarjetas[i].uid == uid
    ) {

      return i;
    }
  }

  return -1;
}

// ======================================================
// OBTENER O CREAR ESTADO DE TARJETA
// ======================================================

int obtenerTarjeta(
  const String& uid
) {

  int indice =
    buscarTarjeta(uid);

  if (
    indice >= 0
  ) {

    return indice;
  }

  if (
    cantidadTarjetas < MAX_TARJETAS
  ) {

    tarjetas[cantidadTarjetas].uid =
      uid;

    tarjetas[cantidadTarjetas].rechazos =
      0;

    tarjetas[cantidadTarjetas].bloqueada =
      false;

    tarjetas[cantidadTarjetas].bloqueoHasta =
      0;

    cantidadTarjetas++;

    return cantidadTarjetas - 1;
  }

  Serial.println(
    "No hay espacio para registrar otra tarjeta"
  );

  return -1;
}

// ======================================================
// PROCESAR TARJETA
// ======================================================

void procesarTarjeta(
  MFRC522 &rfid,
  const String& lector
) {

  String tarjetaLeida = "";

  for (
    byte i = 0;
    i < rfid.uid.size;
    i++
  ) {

    if (
      rfid.uid.uidByte[i] < 0x10
    ) {

      tarjetaLeida += "0";
    }

    tarjetaLeida += String(
      rfid.uid.uidByte[i],
      HEX
    );
  }

  tarjetaLeida.toUpperCase();

  Serial.print(lector);

  Serial.print(" - UID leido: ");

  Serial.println(
    tarjetaLeida
  );

  int indiceTarjeta =
    obtenerTarjeta(
      tarjetaLeida
    );

  if (
    indiceTarjeta < 0
  ) {

    Serial.println(
      "No se pudo registrar el estado de la tarjeta"
    );

    return;
  }

  if (
    tarjetas[indiceTarjeta].bloqueada &&
    millis() <
    tarjetas[indiceTarjeta].bloqueoHasta
  ) {

    Serial.print(
      "Tarjeta temporalmente bloqueada: "
    );

    Serial.println(
      tarjetaLeida
    );

    Serial.print(
      "Rechazos de esta tarjeta: "
    );

    Serial.println(
      tarjetas[indiceTarjeta].rechazos
    );

    mostrarTarjetaBloqueada();

    if (
      modoActual != "ACTIVAR"
    ) {

      digitalWrite(
        RELAY_PIN,
        LOW
      );
    }

    return;
  }

  ultimoUID =
    tarjetaLeida;

  ultimoAccesoPermitido =
    tarjetaLeida ==
    UID_AUTORIZADO;

  lecturaRFIDPendiente =
    true;

  uidRFIDPendiente =
    tarjetaLeida;

  indiceTarjetaPendiente =
    indiceTarjeta;

  accesoPermitidoPendiente =
    ultimoAccesoPermitido;

  lectorRFIDPendiente =
    lector;

  mostrarUID(
    tarjetaLeida
  );

  pantallaHasta =
    millis() +
    TIEMPO_MOSTRAR_UID;
}

// ======================================================
// FINALIZAR LECTURA RFID PENDIENTE
// ======================================================

void finalizarLecturaRFID() {

  if (
    !lecturaRFIDPendiente
  ) {

    return;
  }

  lecturaRFIDPendiente =
    false;

  String tarjetaLeida =
    uidRFIDPendiente;

  int indiceTarjeta =
    indiceTarjetaPendiente;

  bool accesoPermitido =
    accesoPermitidoPendiente;

  if (
    accesoPermitido
  ) {

    tarjetas[indiceTarjeta].rechazos =
      0;

    tarjetas[indiceTarjeta].bloqueada =
      false;

    tarjetas[indiceTarjeta].bloqueoHasta =
      0;

    Serial.println(
      "ACCESO PERMITIDO"
    );

    Serial.println(
      "Contador de esta tarjeta reiniciado"
    );

    publishmqtt(
      tarjetaLeida,
      true
    );

    digitalWrite(
      RELAY_PIN,
      HIGH
    );

    relePorAcceso =
      true;

    releAccesoHasta =
      millis() +
      12000;

    Serial.println(
      "Rele activado"
    );

    Serial.println(
      "Tiempo: 12 segundos"
    );

    mostrarAccesoPermitido();

    pantallaHasta =
      releAccesoHasta;

    return;
  }

  tarjetas[indiceTarjeta].rechazos++;

  Serial.println(
    "ACCESO RECHAZADO"
  );

  Serial.print(
    "Rechazos de esta tarjeta: "
  );

  Serial.println(
    tarjetas[indiceTarjeta].rechazos
  );

  if (
    modoActual != "ACTIVAR"
  ) {

    digitalWrite(
      RELAY_PIN,
      LOW
    );
  }

  if (
    tarjetas[indiceTarjeta].rechazos == 3
  ) {

    tarjetas[indiceTarjeta].bloqueada =
      true;

    tarjetas[indiceTarjeta].bloqueoHasta =
      millis() +
      TIEMPO_BLOQUEO_RECHAZADA;

    Serial.println(
      "================================"
    );

    Serial.println(
      "TERCER RECHAZO DE ESTA TARJETA"
    );

    Serial.print(
      "Tarjeta bloqueada: "
    );

    Serial.println(
      tarjetas[indiceTarjeta].uid
    );

    Serial.println(
      "Tiempo de bloqueo: 12 segundos"
    );

    Serial.println(
      "Las otras tarjetas pueden continuar"
    );

    Serial.println(
      "================================"
    );

    mostrarTarjetaBloqueada();

    publishmqtt(
      tarjetaLeida,
      false
    );

    return;
  }

  publishmqtt(
    tarjetaLeida,
    false
  );

  mostrarRechazo(
    tarjetas[indiceTarjeta].rechazos
  );
}

// ======================================================
// CONECTAR WIFI
// ======================================================

void conectToWiFi() {

  WiFi.begin(
    ssid,
    password
  );

  Serial.println(
    "Conectando a WiFi..."
  );

  while (
    WiFi.status() != WL_CONNECTED
  ) {

    delay(500);

    Serial.print(".");
  }

  Serial.println();

  Serial.println(
    "Conectado a WiFi"
  );
}

// ======================================================
// SIMULACION RFID
// ======================================================

int simulateRFID() {

  return random(
    1000,
    2500
  );
}

// ======================================================
// ENVIAR DATOS A THINGSPEAK
// ======================================================

void sendRFIDData(
  const String& uid,
  bool accesoPermitido
) {

  if (
    WiFi.status() != WL_CONNECTED
  ) {

    Serial.println(
      "No se puede enviar: WiFi desconectado"
    );

    return;
  }

  int valorRFID =
    simulateRFID();

  int indiceTarjeta =
    buscarTarjeta(uid);

  int rechazosActuales =
    0;

  if (
    indiceTarjeta >= 0
  ) {

    rechazosActuales =
      tarjetas[indiceTarjeta].rechazos;
  }

  String url =
    "http://api.thingspeak.com/update?api_key="
    + String(channelWriteAPIKey)
    + "&field1="
    + String(valorRFID)
    + "&field2="
    + String(
        accesoPermitido
          ? 1
          : 0
      )
    + "&field3="
    + String(
        rechazosActuales
      )
    + "&status="
    + uid;

  HTTPClient http;

  http.begin(url);

  int httpCode =
    http.GET();

  String response =
    http.getString();

  Serial.print(
    "Respuesta ThingSpeak HTTP "
  );

  Serial.print(
    httpCode
  );

  Serial.print(": ");

  Serial.println(
    response
  );

  Serial.print(
    "RFID enviado: valor="
  );

  Serial.print(
    valorRFID
  );

  Serial.print(
    ", UID="
  );

  Serial.print(
    uid
  );

  Serial.print(
    ", acceso="
  );

  Serial.print(
    accesoPermitido
      ? "permitido"
      : "denegado"
  );

  Serial.print(
    ", rechazos="
  );

  Serial.println(
    rechazosActuales
  );

  if (
    httpCode == HTTP_CODE_OK &&
    response.toInt() > 0
  ) {

    Serial.println(
      "Dato aceptado por ThingSpeak"
    );

  } else {

    Serial.println(
      "ThingSpeak no acepto el dato"
    );
  }

  http.end();
}

// ======================================================
// RECIBIR DATOS DE THINGSPEAK
// ======================================================

void receiveThingSpeakData() {

  if (
    WiFi.status() != WL_CONNECTED
  ) {

    Serial.println(
      "No se puede recibir: WiFi desconectado"
    );

    return;
  }

  String url =
    "http://api.thingspeak.com/channels/"
    + String(channelID)
    + "/feeds/last.json";

  HTTPClient http;

  http.begin(url);

  int httpCode =
    http.GET();

  if (
    httpCode == HTTP_CODE_OK
  ) {

    String response =
      http.getString();

    JsonDocument doc;

    DeserializationError error =
      deserializeJson(
        doc,
        response
      );

    if (error) {

      Serial.print(
        "Error al interpretar respuesta de ThingSpeak: "
      );

      Serial.println(
        error.c_str()
      );

    } else {

      Serial.println(
        "Ultimo registro recibido de ThingSpeak:"
      );

      Serial.print(
        "UID: "
      );

      Serial.println(
        doc["status"] |
        "sin UID"
      );

      Serial.print(
        "Valor RFID: "
      );

      Serial.println(
        doc["field1"] |
        "sin valor"
      );

      Serial.print(
        "Acceso: "
      );

      Serial.println(
        doc["field2"] |
        "sin estado"
      );

      Serial.print(
        "Rechazos: "
      );

      Serial.println(
        doc["field3"] |
        "sin datos"
      );
    }

  } else {

    Serial.print(
      "Error al recibir de ThingSpeak HTTP "
    );

    Serial.println(
      httpCode
    );
  }

  http.end();
}

// ======================================================
// RECONEXION WIFI Y MQTT NO BLOQUEANTE
// ======================================================

void reconect() {

  unsigned long ahora =
    millis();

  if (
    WiFi.status() != WL_CONNECTED
  ) {

    if (
      !intentandoReconectarWiFi &&
      ahora - ultimaReconexion >=
      INTERVALO_RECONEXION
    ) {

      ultimaReconexion =
        ahora;

      Serial.println(
        "WiFi desconectado. Intentando reconectar..."
      );

      WiFi.disconnect();

      WiFi.begin(
        ssid,
        password
      );

      intentandoReconectarWiFi =
        true;

      inicioReconectarWiFi =
        ahora;
    }

    if (
      intentandoReconectarWiFi
    ) {

      if (
        WiFi.status() ==
        WL_CONNECTED
      ) {

        intentandoReconectarWiFi =
          false;

        Serial.println(
          "Reconectado a WiFi"
        );

      } else if (
        ahora - inicioReconectarWiFi >=
        10000
      ) {

        intentandoReconectarWiFi =
          false;

        Serial.println(
          "No se pudo reconectar a WiFi"
        );
      }
    }

    return;
  }

  intentandoReconectarWiFi =
    false;

  if (
    mqttClient.connected()
  ) {

    return;
  }

  if (
    ahora - ultimaReconexion <
    INTERVALO_RECONEXION
  ) {

    return;
  }

  ultimaReconexion =
    ahora;

  String clientId =
    device_ID +
    "-" +
    String(
      random(0xffff),
      HEX
    );

  if (
    mqttClient.connect(
      clientId.c_str()
    )
  ) {

    Serial.println(
      "Conectado a MQTT"
    );

    mqttClient.subscribe(
      mqttCommandTopic
    );

  } else {

    Serial.print(
      "Error al conectar a MQTT, estado: "
    );

    Serial.println(
      mqttClient.state()
    );
  }
}

// ======================================================
// JSON DE EJEMPLO
// ======================================================

void exampleJSON() {

  JsonDocument doc;

  doc["ID"] =
    4512;

  doc["valor_RFID"] =
    30.5;

  String output;

  serializeJsonPretty(
    doc,
    output
  );

  Serial.println(
    output
  );
}

// ======================================================
// CALLBACK MQTT
// ======================================================

void mqttCallback(
  char* topic,
  byte* payload,
  unsigned int length
) {

  String message;

  message.reserve(
    length
  );

  for (
    unsigned int i = 0;
    i < length;
    i++
  ) {

    message +=
      static_cast<char>(
        payload[i]
      );
  }

  Serial.print(
    "Mensaje MQTT recibido en "
  );

  Serial.print(
    topic
  );

  Serial.print(
    ": "
  );

  Serial.println(
    message
  );

  handleCommand(
    message
  );
}

// ======================================================
// COMANDOS MQTT
// ======================================================

void handleCommand(
  const String& command
) {

  String normalizedCommand =
    command;

  normalizedCommand.trim();

  normalizedCommand.toUpperCase();

  if (
    normalizedCommand ==
    "ACTIVAR"
  ) {

    modoActual =
      "ACTIVAR";

    relePorAcceso =
      false;

    releAccesoHasta =
      0;

    digitalWrite(
      RELAY_PIN,
      HIGH
    );

    Serial.println(
      "Comando MQTT: ACTIVAR"
    );

    Serial.println(
      "Relay activado"
    );

    publishConfirmation();
  }

  else if (
    normalizedCommand ==
    "DESACTIVAR"
  ) {

    modoActual =
      "DESACTIVAR";

    relePorAcceso =
      false;

    releAccesoHasta =
      0;

    digitalWrite(
      RELAY_PIN,
      LOW
    );

    Serial.println(
      "Comando MQTT: DESACTIVAR"
    );

    Serial.println(
      "Relay desactivado"
    );

    publishConfirmation();
  }

  else if (
    normalizedCommand ==
    "AUTO"
  ) {

    modoActual =
      "AUTO";

    Serial.println(
      "Comando MQTT: AUTO"
    );

    Serial.println(
      "Sistema en modo automatico"
    );

    publishConfirmation();
  }

  else {

    Serial.print(
      "Comando MQTT no reconocido: "
    );

    Serial.println(
      normalizedCommand
    );

    modoActual =
      "AUTO";

    publishConfirmation();
  }
}

// ======================================================
// PUBLICAR JSON MQTT
// ======================================================

bool publishmqtt(
  const String& uid,
  bool accesoPermitido
) {

  if (
    !mqttClient.connected()
  ) {

    Serial.println(
      "No se puede publicar MQTT: desconectado"
    );

    return false;
  }

  int indiceTarjeta =
    buscarTarjeta(uid);

  int rechazosActuales =
    0;

  bool bloqueoActivo =
    false;

  if (
    indiceTarjeta >= 0
  ) {

    rechazosActuales =
      tarjetas[indiceTarjeta].rechazos;

    bloqueoActivo =
      tarjetas[indiceTarjeta].bloqueada &&
      millis() <
      tarjetas[indiceTarjeta].bloqueoHasta;
  }

  JsonDocument doc;

  doc["device_id"] =
    "IOT-592A3B6D5B";

  doc["variable"] =
    "identificador RFID";

  doc["value"] =
    bloqueoActivo
      ? 12
      : 0;

  doc["unit"] =
    "segundos de bloqueo";

  doc["mode"] =
    modoActual;

  doc["alarm"] =
    bloqueoActivo;

  doc["sequence"] =
    accesoPermitido
      ? 0
      : rechazosActuales;

  String payload;

  serializeJson(
    doc,
    payload
  );

  Serial.println(
    "JSON MQTT:"
  );

  Serial.println(
    payload
  );

  bool publicado =
    mqttClient.publish(
      mqttTelemetryTopic,
      payload.c_str()
    );

  if (
    publicado
  ) {

    Serial.println(
      "JSON publicado correctamente en MQTT"
    );

  } else {

    Serial.println(
      "Error al publicar JSON en MQTT"
    );
  }

  return publicado;
}

// ======================================================
// LEER POTENCIOMETRO
// ======================================================

int readPotentiometer() {

  return analogRead(
    POTENTIOMETER_PIN
  );
}

// ======================================================
// PUBLICAR CONFIRMACION
// ======================================================

void publishConfirmation() {

  if (
    !mqttClient.connected()
  ) {

    Serial.println(
      "No se puede publicar confirmacion MQTT"
    );

    return;
  }

  JsonDocument doc;

  doc["device_id"] =
    "IOT-592A3B6D5B";

  doc["variable"] =
    "identificador RFID";

  doc["value"] =
    0;

  doc["unit"] =
    "segundos de bloqueo";

  doc["mode"] =
    modoActual;

  doc["alarm"] =
    false;

  doc["sequence"] =
    0;

  String payload;

  serializeJson(
    doc,
    payload
  );

  bool publicado =
    mqttClient.publish(
      mqttStateTopic,
      payload.c_str()
    );

  Serial.print(
    "Resultado publicacion rfid/estado: "
  );

  Serial.println(
    publicado
      ? "EXITOSA"
      : "FALLIDA"
  );

  Serial.println(
    "Confirmacion MQTT:"
  );

  Serial.println(
    payload
  );
}

 



