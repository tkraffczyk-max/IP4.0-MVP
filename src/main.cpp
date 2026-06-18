#include <Arduino.h>

#define LED_PIN 2

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  delay(500);
  Serial.println("ESP32 lebt!");
}

void loop() {
  digitalWrite(LED_PIN, HIGH);
  Serial.println("blink");
  delay(500);
  digitalWrite(LED_PIN, LOW);
  delay(500);
}