#include <Arduino.h>
#include <HX711.h>

// ─── Pin-Belegung Drucktaster (DS427) ────────────────────────────────────────
//  Verschaltung: Ein Taster-Pin → Arduino-Pin, anderer → GND
//  INPUT_PULLUP: kein externer Widerstand nötig; LOW = gedrückt, HIGH = offen
const int TASTER_PINS[4]   = {2, 3, 4, 5};
const char* TASTER_NAMEN[4] = {"T1(D2)", "T2(D3)", "T3(D4)", "T4(D5)"};

// ─── Pin-Belegung HX711 ───────────────────────────────────────────────────────
//  D6/D7: freie Digital-Pins, kein SPI/I2C nötig (Bibliothek macht Bitbanging)
const int HX711_DT  = 6;
const int HX711_SCK = 7;

// ─── Kalibrierfaktor ──────────────────────────────────────────────────────────
//  Muss einmalig ermittelt werden – siehe Kommentar in setup()
float kalibrierfaktor = -7050.0;

HX711 waage;

// ─── Hilfsfunktion: Kalibrierfaktor per Serial einstellen ────────────────────
void kalibriereWaage() {
  Serial.println();
  Serial.println("=== KALIBRIERUNG ===");
  Serial.println("Waage wird genullt (leere Waage)...");
  waage.tare();
  Serial.println("Nullpunkt gesetzt.");
  Serial.println("Lege ein bekanntes Gewicht auf und sende dessen Wert in Gramm");
  Serial.println("im Serial Monitor (z.B. '1000' fuer 1000g), dann ENTER druecken.");

  while (!Serial.available()) { delay(100); }
  float bekanntes_gewicht = Serial.parseFloat();
  Serial.flush();

  float rohwert = waage.get_value(10);
  kalibrierfaktor = rohwert / bekanntes_gewicht;

  Serial.print("Rohwert: ");
  Serial.println(rohwert);
  Serial.print("Neuer Kalibrierfaktor: ");
  Serial.println(kalibrierfaktor);
  waage.set_scale(kalibrierfaktor);
  Serial.println("=== KALIBRIERUNG ABGESCHLOSSEN ===");
  Serial.println();
}

// =============================================================================
//  SETUP
// =============================================================================
void setup() {
  Serial.begin(9600);
  delay(500);
  Serial.println("=== Arduino Taster + Waage ===");
  Serial.println("Pins: T1=D2 | T2=D3 | T3=D4 | T4=D5 | HX711 DT=D6 SCK=D7");
  Serial.println();

  // Taster mit internem Pull-Up initialisieren
  for (int i = 0; i < 4; i++) {
    pinMode(TASTER_PINS[i], INPUT_PULLUP);
  }

  // HX711 initialisieren
  waage.begin(HX711_DT, HX711_SCK);

  if (!waage.is_ready()) {
    Serial.println("[FEHLER] HX711 nicht gefunden – Verkabelung pruefen!");
    while (true) { delay(1000); }
  }

  waage.set_scale(kalibrierfaktor);
  waage.tare();
  Serial.println("[Waage] Nullpunkt gesetzt.");

  // Kalibrierung optional starten: 'k' im Serial Monitor senden
  Serial.println("Tipp: Sende 'k' innerhalb 5s fuer Kalibrierung, sonst weiter.");
  unsigned long start = millis();
  while (millis() - start < 5000) {
    if (Serial.available() && Serial.read() == 'k') {
      kalibriereWaage();
      break;
    }
  }

  Serial.println("─────────────────────────────────────");
}

// =============================================================================
//  LOOP
// =============================================================================
void loop() {
  // ─── Taster auslesen ──────────────────────────────────────────────────────
  Serial.print("[Taster]  ");
  for (int i = 0; i < 4; i++) {
    bool gedrueckt = (digitalRead(TASTER_PINS[i]) == LOW);
    Serial.print(TASTER_NAMEN[i]);
    Serial.print(": ");
    Serial.print(gedrueckt ? "AN " : "AUS");
    if (i < 3) Serial.print("  |  ");
  }
  Serial.println();

  // ─── Gewicht auslesen ─────────────────────────────────────────────────────
  if (waage.is_ready()) {
    float gramm = waage.get_units(5);
    Serial.print("[Waage]   Gewicht: ");
    Serial.print(gramm, 1);
    Serial.println(" g");
  } else {
    Serial.println("[Waage]   HX711 nicht bereit");
  }

  Serial.println("─────────────────────────────────────");
  delay(500);
}
