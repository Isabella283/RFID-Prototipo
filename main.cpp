#include<SPI.h>
#include<MFRC522.h>
#include<LiquidCrystal_I2C.h>

#define SS_PIN 5
#define RST_PIN 17

MFRC522 rfid(SS_PIN, RST_PIN);
LiquidCrystal_I2C lcd(0x27, 16, 2);

int contador_rechazos = 0;

void setup() {
  Serial.begin(115200);

  // Pin del relé
  pinMode(4, OUTPUT);
  digitalWrite(4, LOW); 

  // Inicialización de la pantalla LCD
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Bienvenido");

  // Inicialización del bus SPI
  SPI.begin();
  rfid.PCD_Init();
}

void loop() {
  // Fuerza la reinicialización del lector para asegurar la lectura en Wokwi
  rfid.PCD_Init();

  // Revisar si hay tarjeta presente
  if (!rfid.PICC_IsNewCardPresent()) {
    return;
  }

  // Leer la tarjeta
  if (!rfid.PICC_ReadCardSerial()) {
    return;
  }

  // Obtener UID
  String tarjetaLeida = "";
  for (byte i = 0; i < rfid.uid.size; i++) {
    if (rfid.uid.uidByte[i] < 0x10) {
      tarjetaLeida += "0";
    }
    tarjetaLeida += String(rfid.uid.uidByte[i], HEX);
  }
  tarjetaLeida.toUpperCase();

  Serial.print("Tarjeta detectada: ");
  Serial.println(tarjetaLeida);

  // Validar acceso (Azul '01020304' o Amarilla '55667788')
  if (tarjetaLeida == "01020304" || tarjetaLeida == "55667788") {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Acceso Permitido");
    
    digitalWrite(4, HIGH);
    delay(3000);
    digitalWrite(4, LOW);
  } else {
    contador_rechazos++;
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Acceso Denegado");
    lcd.setCursor(0, 1);
    lcd.print("Rechazos: " + String(contador_rechazos));
    delay(2000);
  }

  // Restaurar mensaje de bienvenida
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Bienvenido");

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
}
  

