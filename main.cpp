##include <SPI.h>
#include <MFRC522.h>
#include <LiquidCrystal_I2C.h>

#define SS_PIN 5
#define RST_PIN 17
#define RELAY_PIN 2  // Usamos el pin 2

MFRC522 rfid(SS_PIN, RST_PIN);
LiquidCrystal_I2C lcd(0x27, 16, 2);

int contador_rechazos = 0;

void setup() {
  Serial.begin(115200);

  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);

  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Bienvenido");

  SPI.begin();
  rfid.PCD_Init();
}

void loop() {
  rfid.PCD_Init();

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

  if (tarjetaLeida == "01020304" || tarjetaLeida == "55667788" || tarjetaLeida == "E385381A") {
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
}

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
}
  

