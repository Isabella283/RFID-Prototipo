#include <SPI.h>
#include <MFRC522.h>
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

const char* ssid = "Wokwi-GUEST";
const char* password = "";

const char* channelID = "3488404";
const char* channelWriteAPIKey = "WEXJG3K18GJR7SG8";

#define SS_PIN 5
#define RST_PIN 17
#define RELAY_PIN 2  // Usamos el pin 2

MFRC522 rfid(SS_PIN, RST_PIN);
LiquidCrystal_I2C lcd(0x27, 16, 2);


int contador_rechazos = 0;

WiFiClient espClient;

void conectToWiFi(); 
int simulateRFID();
void sendRFIDData(const String& uid, bool accesoPermitido);
void receiveThingSpeakData();
void reconect();
void exampleJSON();

void setup() {
  Serial.begin(115200);
  Serial.println("conectando a WiFi...");
  conectToWiFi();

  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);

  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Bienvenido");

  SPI.begin();
  rfid.PCD_Init();

  Serial.println("Enviando prueba a ThingSpeak...");
  sendRFIDData("SIN_TARJETA", false);
  Serial.println("Recibiendo último registro de ThingSpeak...");
  receiveThingSpeakData();
  exampleJSON();
}

void loop() {
  reconect();

  if (WiFi.status() == WL_CONNECTED){
    Serial.println("Conectado a WiFi");

  }

  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) {
    return;
  
  
  }

  String tarjetaLeida = "";
  for (byte i = 0; i < rfid.uid.size; i++) {
    if (rfid.uid.uidByte[i] < 0x10) {
      tarjetaLeida += "0";
    }
    tarjetaLeida += String(rfid.uid.uidByte[i], HEX);
  }
  tarjetaLeida.toUpperCase();

  Serial.print("UID leido: ");
  Serial.println(tarjetaLeida);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("UID: " + tarjetaLeida);
  delay(1200);

  bool accesoPermitido = tarjetaLeida == "01020304" || tarjetaLeida == "55667788" || tarjetaLeida == "E385381A";

  if (accesoPermitido) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Acceso Permitido");
    
    digitalWrite(RELAY_PIN, HIGH);
    delay(3000);
    digitalWrite(RELAY_PIN, LOW);
  } else {
    contador_rechazos++;
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Acceso Denegado");
    lcd.setCursor(0, 1);
    lcd.print("Rechazos: " + String(contador_rechazos));
    delay(2000);
  }

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Bienvenido");

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();

  sendRFIDData(tarjetaLeida, accesoPermitido);
  delay(20000);
}

void conectToWiFi() {
  WiFi.begin(ssid, password);
  Serial.println("Conectando a WiFi...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("Conectado a WiFi");
}

int simulateRFID(){
  return random(1000,2500);
}

void sendRFIDData(const String& uid, bool accesoPermitido) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("No se puede enviar: WiFi desconectado");
    return;
  }
  int valorRFID = simulateRFID();
  String url = "http://api.thingspeak.com/update?api_key=" + String(channelWriteAPIKey)
             + "&field1=" + String(valorRFID)
             + "&field2=" + String(accesoPermitido ? 1 : 0)
             + "&field3=" + String(contador_rechazos)
             + "&status=" + uid;
  HTTPClient http;
  http.begin(url);
  int httpCode = http.GET();
  String response = http.getString();

  Serial.print("Respuesta ThingSpeak HTTP ");
  Serial.print(httpCode);
  Serial.print(": ");
  Serial.println(response);
  Serial.print("RFID enviado: valor=");
  Serial.print(valorRFID);
  Serial.print(", UID=");
  Serial.print(uid);
  Serial.print(", acceso=");
  Serial.print(accesoPermitido ? "permitido" : "denegado");
  Serial.print(", rechazos=");
  Serial.println(contador_rechazos);

  if (httpCode == HTTP_CODE_OK && response.toInt() > 0) {
    Serial.println("Dato aceptado por ThingSpeak");
  }
  else {
    Serial.println("ThingSpeak no acepto el dato");
  }
  http.end();
}

void receiveThingSpeakData() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("No se puede recibir: WiFi desconectado");
    return;
  }

  String url = "http://api.thingspeak.com/channels/" + String(channelID) + "/feeds/last.json";
  HTTPClient http;
  http.begin(url);
  int httpCode = http.GET();

  if (httpCode == HTTP_CODE_OK) {
    String response = http.getString();
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, response);

    if (error) {
      Serial.print("Error al interpretar respuesta de ThingSpeak: ");
      Serial.println(error.c_str());
    } else {
      Serial.println("Último registro recibido de ThingSpeak:");
      Serial.print("UID: ");
      Serial.println(doc["status"] | "sin UID");
      Serial.print("Valor RFID: ");
      Serial.println(doc["field1"] | "sin valor");
      Serial.print("Acceso: ");
      Serial.println(doc["field2"] | "sin estado");
      Serial.print("Rechazos: ");
      Serial.println(doc["field3"] | "sin datos");
    }
  } else {
    Serial.print("Error al recibir de ThingSpeak HTTP ");
    Serial.println(httpCode);
  }

  http.end();
}

void reconect() {
  if (WiFi.status() == WL_CONNECTED) {
    return;
  }

  Serial.println("WiFi desconectado. Intentando reconectar...");
  WiFi.disconnect();
  WiFi.begin(ssid, password);

  unsigned long inicio = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - inicio < 10000) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Reconectado a WiFi");
  } else {
    Serial.println("No se pudo reconectar a WiFi");
  }
}

void exampleJSON(){
  JsonDocument doc;

doc["ID"] = 4512;
doc["valor_RFID"] = 30.5;

String output;

doc.shrinkToFit();  // optional

serializeJsonPretty(doc, output);

Serial.println(output);

}

