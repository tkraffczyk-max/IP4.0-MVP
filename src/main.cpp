/*Neuer Code18.06.26
 * ============================================================
 *  ESP32 → AWS IoT Core  |  Arduino IDE
 *  Region:  eu-central-1
 *  Client:  ESP32
 *  Publish: esp32/pub
 *  Subscribe: esp32/Sub
 * ============================================================
 *
 *  BENÖTIGTE BIBLIOTHEKEN (Arduino Library Manager):
 *    - PubSubClient   (Nick O'Leary)
 *    - ArduinoJson    (Benoit Blanchon)
 *    - WiFiClientSecure  (bereits in ESP32-Core enthalten)
 *
 *  ZERTIFIKATE – WO EINFÜGEN:
 *  ┌─────────────────────────────────────────────────────────┐
 *  │  Zertifikat / Schlüssel          → Variable unten       │
 *  │  ─────────────────────────────────────────────────────  │
 *  │  AmazonRootCA1.pem               → aws_root_ca          │
 *  │  Geräte-Zertifikat (*-cert.pem)  → aws_device_cert      │
 *  │  Privater Schlüssel (*-private)  → aws_private_key      │
 *  │  Öffentlicher Schlüssel          → wird NICHT benötigt  │
 *  └─────────────────────────────────────────────────────────┘
 */

#include <WiFi.h>
#include <WiFiMulti.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <HX711.h>
#include <Preferences.h>
#include <HTTPClient.h>

// ─── WLAN (mehrere Netzwerke) ──────────────────────────────
const char* WIFI_SSID     = "iPhone Sebastian";
const char* WIFI_PASSWORD = "12345678";
const char* WIFI_SSID2    = "Teunis";
const char* WIFI_PASSWORD2= "maasssiiii";
WiFiMulti wifiMulti;

// ─── AWS IoT Endpoint ──────────────────────────────────────
const char* AWS_ENDPOINT  = "a1niwvuc208pma-ats.iot.eu-central-1.amazonaws.com";

// ─── MQTT Topics ───────────────────────────────────────────
const char* TOPIC_PUB     = "esp32/pub";
const char* TOPIC_SUB     = "esp32/sub";
const char* CLIENT_ID     = "ESP321";

// ─── Port ──────────────────────────────────────────────────
const int   MQTT_PORT     = 8883;

// ─── Intervall für Testnachrichten (Millisekunden) ─────────
const long  PUBLISH_INTERVAL = 5000;   // alle 5 Sekunden

// ─── Barcode-Scanner (UART) ────────────────────────────────
#define SCANNER_RX_PIN  16    // ESP32 GPIO16 ← Scanner TX
#define SCANNER_TX_PIN  17    // ESP32 GPIO17 → Scanner RX
#define SCANNER_BAUD    9600
HardwareSerial ScannerSerial(2);  // UART2

// ─── Drucktaster DS427 ─────────────────────────────────────
//  Verschaltung: Taster-Pin → GPIO, anderer Taster-Pin → GND
//  INPUT_PULLUP: LOW = gedrückt, HIGH = offen
const int   TASTER_PINS[4]    = {25, 26, 27, 32};
const char* TASTER_NAMEN[4]   = {"T1(G25)", "T2(G26)", "T3(G27)", "T4(G32)"};

// ─── Wägezellen / HX711 ────────────────────────────────────
const int   HX711_DT_PIN      = 18;   // DOUT → GPIO18
const int   HX711_SCK_PIN     = 19;   // SCK  → GPIO19
float       kalibrierung      = -7050.0;  // Wird aus NVS geladen, falls vorhanden
HX711       waage;
Preferences prefs;


// ===========================================================
//  ZERTIFIKATE  –  HIER EINFÜGEN
// ===========================================================

// 1) AmazonRootCA1  (aus AmazonRootCA1.pem)
const char* aws_root_ca = R"EOF(
-----BEGIN CERTIFICATE-----
MIIDQTCCAimgAwIBAgITBmyfz5m/jAo54vB4ikPmljZbyjANBgkqhkiG9w0BAQsF
ADA5MQswCQYDVQQGEwJVUzEPMA0GA1UEChMGQW1hem9uMRkwFwYDVQQDExBBbWF6
b24gUm9vdCBDQSAxMB4XDTE1MDUyNjAwMDAwMFoXDTM4MDExNzAwMDAwMFowOTEL
MAkGA1UEBhMCVVMxDzANBgNVBAoTBkFtYXpvbjEZMBcGA1UEAxMQQW1hem9uIFJv
b3QgQ0EgMTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBALJ4gHHKeNXj
ca9HgFB0fW7Y14h29Jlo91ghYPl0hAEvrAIthtOgQ3pOsqTQNroBvo3bSMgHFzZM
9O6II8c+6zf1tRn4SWiw3te5djgdYZ6k/oI2peVKVuRF4fn9tBb6dNqcmzU5L/qw
IFAGbHrQgLKm+a/sRxmPUDgH3KKHOVj4utWp+UhnMJbulHheb4mjUcAwhmahRWa6
VOujw5H5SNz/0egwLX0tdHA114gk957EWW67c4cX8jJGKLhD+rcdqsq08p8kDi1L
93FcXmn/6pUCyziKrlA4b9v7LWIbxcceVOF34GfID5yHI9Y/QCB/IIDEgEw+OyQm
jgSubJrIqg0CAwEAAaNCMEAwDwYDVR0TAQH/BAUwAwEB/zAOBgNVHQ8BAf8EBAMC
AYYwHQYDVR0OBBYEFIQYzIU07LwMlJQuCFmcx7IQTgoIMA0GCSqGSIb3DQEBCwUA
A4IBAQCY8jdaQZChGsV2USggNiMOruYou6r4lK5IpDB/G/wkjUu0yKGX9rbxenDI
U5PMCCjjmCXPI6T53iHTfIUJrU6adTrCC2qJeHZERxhlbI1Bjjt/msv0tadQ1wUs
N+gDS63pYaACbvXy8MWy7Vu33PqUXHeeE6V/Uq2V8viTO96LXFvKWlJbYK8U90vv
o/ufQJVtMVT8QtPHRh8jrdkPSHCa2XV4cdFyQzR1bldZwgJcJmApzyMZFo6IQ6XU
5MsI+yMRQ+hDKXJioaldXgjUkK642M4UwtBV8ob2xJNDd2ZhwLnoQdeXeGADbkpy
rqXRfboQnoZsG4q5WTP468SQvvG5
-----END CERTIFICATE-----
)EOF";

// 2) Geräte-Zertifikat  (aus *-certificate.pem.crt)
const char* aws_device_cert = R"EOF(
-----BEGIN CERTIFICATE-----
MIIDWTCCAkGgAwIBAgIUeRPf/53Cc2h5p6+M+gstzDdmSiowDQYJKoZIhvcNAQEL
BQAwTTFLMEkGA1UECwxCQW1hem9uIFdlYiBTZXJ2aWNlcyBPPUFtYXpvbi5jb20g
SW5jLiBMPVNlYXR0bGUgU1Q9V2FzaGluZ3RvbiBDPVVTMB4XDTI2MDUyMDE1MjM0
OFoXDTQ5MTIzMTIzNTk1OVowHjEcMBoGA1UEAwwTQVdTIElvVCBDZXJ0aWZpY2F0
ZTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBAMD/W5wPylKF2C5s14kJ
ZxrV687mhkadbOfG+3UvpO7DVAmLNBSQFEMDzr0DCEHsq3wX0O96obiOSegy5o84
+sj0kmAUHRhW7/SzlT3zO3vhjsakDqwMVbrPfx4k0pSKvqdoH8FjAwFqI2ePtCHr
mcQhSYY490bvfrTc3G5Ry9EUFf1tdF09+C43qw+ZZ0xvKx+hqYsBEQsPvvYfAPW9
jRLy6bWYKUv2ynKDBxdBPo/CnERDYPoPgz/7IY+WvqiyEcwlOhA5wKA3nESm0h1X
fbbOcKlxXRvOQgc4ZbMNJHAnfRM4xHZbPz8vv4PKSUsvY1qrRgjr9h9KPnSnQ3iu
RQkCAwEAAaNgMF4wHwYDVR0jBBgwFoAUFDf4j3AZVrTbikPVAJzuh5H7Wg4wHQYD
VR0OBBYEFFJgYjrwhwgXzS9wuGZv0lzuzRP4MAwGA1UdEwEB/wQCMAAwDgYDVR0P
AQH/BAQDAgeAMA0GCSqGSIb3DQEBCwUAA4IBAQBl6jIead/3TxSl3EZB4TX5H1x5
LJiepZAUI4CDISgZhsvQxC89kAMQ68wOOVf9LXCiqZ9LHF6uI9u1uRbVv0NNigam
lbU5uXdsu7gngFXGPaCR6hxht1uU1DpsNA24SxYiTZLo6YnHcBdOoJjVs7k54ECs
69b2r46gGm89u+ezL18WJ/5LZF6sWEP82eBTINN5nz0HrgzQdeLwCkC0YvOq8VUg
Ih8R7PXXjttAnQiUtW/WiZMbzwY37lQCSqBmwLIu3MfjLUIhx8FCyuShTjbIg1xt
GoX7ucrtVdxIkg4bbdDgKDEoQjIAQeokhyrU9eROCa1N6mrMlDLRZFiMxbrW
-----END CERTIFICATE-----
)EOF";

// 3) Privater Schlüssel  (aus *-private.pem.key)
const char* aws_private_key = R"EOF(
-----BEGIN RSA PRIVATE KEY-----
MIIEowIBAAKCAQEAwP9bnA/KUoXYLmzXiQlnGtXrzuaGRp1s58b7dS+k7sNUCYs0
FJAUQwPOvQMIQeyrfBfQ73qhuI5J6DLmjzj6yPSSYBQdGFbv9LOVPfM7e+GOxqQO
rAxVus9/HiTSlIq+p2gfwWMDAWojZ4+0IeuZxCFJhjj3Ru9+tNzcblHL0RQV/W10
XT34LjerD5lnTG8rH6GpiwERCw++9h8A9b2NEvLptZgpS/bKcoMHF0E+j8KcRENg
+g+DP/shj5a+qLIRzCU6EDnAoDecRKbSHVd9ts5wqXFdG85CBzhlsw0kcCd9EzjE
dls/Py+/g8pJSy9jWqtGCOv2H0o+dKdDeK5FCQIDAQABAoIBAB2XyjM+1JYcwc40
kEQJ9g5OWO8Lt/Eh+G39u9b3LEIUH1GrT5wLj5/K+KtQyLZbFNxdRoNB+YQ0BS8e
hu3VHdcscTlcPC3cPsT1uF+caTWQm3Tn+Pv2lDql0GcN3GA8eglvVNcxWGeiH7Wk
ekltN9G0DPMQzxHIbVRMPVyJHEh9mJ26xsY2D4FUxQoko7Z7JzUUKapedJM6NUiH
YDMGT9dlJ2iKaQT91VFxQGznZD/ujlux4KNicuV1lPwJ4XEpRngnpOXyx9WDl2q5
mrMI2s7BW1Zskm4DxWc8+f9s0PK3pML23a5cKdfyA8h0hjYb/i1OvFZmYtDnF9nX
Q7zRnQECgYEA7uMqr/Tt6eDOQxBAaPg04cHK4mDE1R+N5kZareOTeDZlMUM3HZD6
cO05W+E3Tl6M/lvPYDAvbDQh3nh69zl+3gm3Xe97P19MV0Bp0JN09plMlI9xX+9D
IASdovtC4M8tLZH9Rt8m8mdAA7e+x+uuoZ4Pi8eg0OfUzHJsl0NmzCECgYEAztKj
3aDSI6qEON7f/XW0mc+tl1WkU7jKqrxvzgeHON2MGFfWrQdqPd+BnZp3c7zeiQmX
H/aoko1gs8bQb8x4yMc9td/kfnGd4LZ2SfLAhb/hvvarDvdYoWLDfUmw7q5WUI8t
Qk3dgEZtBwNlCJrHSk0uoF5rMAlGTA1o6723G+kCgYAQPhHg9N5YUJi1pp06heXS
k8KfscSwWPzk81OnDxovykMHHnLY58qfcwG7ZYzFH1im0sc+2wfea40B+18hAB/I
KRD2qhnPLUObix2muN/zBGlYkIg85Z/CxY2ST1M0xeFkf9CgyPt07+BCYHPwU+4D
vyN/dYtRqPGnFQCdKFANAQKBgEV5rsAhaNXATwBHphifdJaCk/6VL0lMdxAZkrk0
ADGTAp+eyid/qSm1rfXCUMQZOuvvw8th8Uiho01gyN31YtGXVFyHJFtxUSaORHgc
yp1jUrLya43wet5jreWlXoEfITGYlwyB2ZlgjvlfI45bZ/zGppYKgZk2VF3x1z/x
j9QZAoGBAI558tGJJdsedEAnIPoZoccgQajl48DA5NVGcXEEI6jDRZdZDniLB80z
+5ehqF10DB9uDUHoyN0kSZyxOPw99oddNInNYrJtWkns8Ry+7FD4cRG7T15uHs5h
bmwsDhiKfsmLXomX00p2E6Qrq/5FhAXwMuYgaAoyoZs9sQmdyCl/
-----END RSA PRIVATE KEY-----
)EOF";


// ─── Forward Declarations ──────────────────────────────────
void publishTestMessage(const char* statusText);
void lookupAndPublish(const String& ean);

// ─── Globale Objekte ───────────────────────────────────────
WiFiClientSecure  wifiSecure;
PubSubClient      mqttClient(wifiSecure);

unsigned long lastPublishMs  = 0;
unsigned long lastSensorMs   = 0;
int           messageCount   = 0;
String        scanBuffer     = "";
const long    SENSOR_INTERVAL = 2000;


// ─── WLAN verbinden ────────────────────────────────────────
void connectWifi() {
  WiFi.mode(WIFI_STA);
  wifiMulti.addAP(WIFI_SSID,  WIFI_PASSWORD);
  wifiMulti.addAP(WIFI_SSID2, WIFI_PASSWORD2);

  Serial.println("[WLAN] Suche bekannte Netzwerke ...");
  while (wifiMulti.run() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("[WLAN] Verbunden mit: ");
  Serial.println(WiFi.SSID());
  Serial.print("[WLAN] IP: ");
  Serial.println(WiFi.localIP());
}


// ─── Eingehende MQTT-Nachrichten ───────────────────────────
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  Serial.print("[MQTT] Nachricht empfangen auf Topic: ");
  Serial.println(topic);

  String message;
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  Serial.print("[MQTT] Inhalt: ");
  Serial.println(message);
}


// ─── MQTT verbinden ────────────────────────────────────────
void connectMQTT() {
  while (!mqttClient.connected()) {
    Serial.print("[MQTT] Verbinde mit AWS IoT ...");

    if (mqttClient.connect(CLIENT_ID)) {
      Serial.println(" verbunden!");

      mqttClient.subscribe(TOPIC_SUB);
      Serial.print("[MQTT] Subscribed auf: ");
      Serial.println(TOPIC_SUB);

      publishTestMessage("ESP32 verbunden mit AWS IoT Core!");
    } else {
      Serial.print(" Fehler, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" – erneuter Versuch in 5 s");
      delay(5000);
    }
  }
}


// ─── Testnachricht senden ──────────────────────────────────
void publishTestMessage(const char* statusText) {
  messageCount++;

  StaticJsonDocument<512> doc;
  doc["device"]    = CLIENT_ID;
  doc["status"]    = statusText;
  doc["messageNr"] = messageCount;
  doc["uptime_s"]  = millis() / 1000;
  doc["rssi_dBm"]  = WiFi.RSSI();

  if (waage.is_ready()) {
    doc["gewicht_g"] = waage.get_units(3);
  }

  char jsonBuffer[512];
  serializeJson(doc, jsonBuffer);

  bool ok = mqttClient.publish(TOPIC_PUB, jsonBuffer);
  Serial.print("[MQTT] Publish auf '");
  Serial.print(TOPIC_PUB);
  Serial.print("': ");
  Serial.println(ok ? "OK ✓" : "FEHLER ✗");
  Serial.print("[MQTT] Payload: ");
  Serial.println(jsonBuffer);
}


// ─── EAN → OpenFoodFacts API → MQTT ───────────────────────
void lookupAndPublish(const String& ean) {
  Serial.print("[API] EAN: ");
  Serial.print(ean);
  Serial.print("  Freier Heap: ");
  Serial.println(ESP.getFreeHeap());

  WiFiClientSecure httpSecure;
  httpSecure.setInsecure();

  HTTPClient http;
  // ?fields= begrenzt die Antwort auf ~500 Bytes statt 100+ KB
  String url = "https://world.openfoodfacts.org/api/v0/product/" + ean
             + ".json?fields=status,product_name,brands,categories";
  http.begin(httpSecure, url);
  http.setTimeout(15000);
  http.addHeader("User-Agent", "ESP32-IoT-Scanner/1.0");

  int code = http.GET();
  Serial.print("[API] HTTP Code: ");
  Serial.println(code);

  if (code != HTTP_CODE_OK) {
    Serial.print("[API] Fehler: ");
    Serial.println(http.errorToString(code));
    http.end();
    return;
  }

  String raw = http.getString();
  http.end();
  Serial.print("[API] Antwort: ");
  Serial.println(raw);

  StaticJsonDocument<1024> doc;
  DeserializationError err = deserializeJson(doc, raw);

  if (err) {
    Serial.print("[API] JSON Fehler: ");
    Serial.println(err.c_str());
    return;
  }

  Serial.print("[API] Status: ");
  Serial.println(doc["status"].as<int>());

  if (doc["status"].as<int>() != 1) {
    Serial.println("[API] Produkt nicht gefunden – kein Publish");
    return;
  }

  String name       = doc["product"]["product_name"] | "unbekannt";
  String brands     = doc["product"]["brands"]       | "unbekannt";
  String categories = doc["product"]["categories"]   | "unbekannt";
  if (categories.length() > 120) categories = categories.substring(0, 120);

  Serial.print("[API] Name:   "); Serial.println(name);
  Serial.print("[API] Marke:  "); Serial.println(brands);
  Serial.print("[API] Kateg.: "); Serial.println(categories);

  if (!mqttClient.connected()) {
    Serial.println("[MQTT] Nicht verbunden – Publish abgebrochen");
    return;
  }

  float gewicht = waage.is_ready() ? waage.get_units(5) : 0.0;

  // Einzige MQTT-Nachricht mit allen Daten
  StaticJsonDocument<768> mqtt_doc;
  mqtt_doc["device"]       = CLIENT_ID;
  mqtt_doc["ean"]          = ean;
  mqtt_doc["product_name"] = name;
  mqtt_doc["brands"]       = brands;
  mqtt_doc["categories"]   = categories;
  mqtt_doc["gewicht_g"]    = gewicht;
  mqtt_doc["uptime_s"]     = millis() / 1000;

  char jsonBuffer[768];
  serializeJson(mqtt_doc, jsonBuffer);

  bool ok = mqttClient.publish(TOPIC_PUB, jsonBuffer);
  Serial.print("[MQTT] Produkt gesendet: ");
  Serial.println(ok ? "OK ✓" : "FEHLER ✗");
  Serial.print("[MQTT] Payload: ");
  Serial.println(jsonBuffer);
}


// ===========================================================
//  SETUP
// ===========================================================
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== ESP32 AWS IoT Core Test ===");

  // TLS-Zertifikate setzen
  wifiSecure.setCACert(aws_root_ca);
  wifiSecure.setCertificate(aws_device_cert);
  wifiSecure.setPrivateKey(aws_private_key);

  // MQTT-Server konfigurieren
  mqttClient.setKeepAlive(60);       // ← FIX: Keep-Alive auf 60s erhöht
  mqttClient.setSocketTimeout(60);   // ← FIX: Socket-Timeout auf 60s erhöht
  mqttClient.setServer(AWS_ENDPOINT, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setBufferSize(1024);

  // Barcode-Scanner initialisieren
  ScannerSerial.begin(SCANNER_BAUD, SERIAL_8N1, SCANNER_RX_PIN, SCANNER_TX_PIN);
  Serial.println("[Scanner] Bereit auf GPIO RX=16, TX=17 @ 9600 Baud");

  // Taster initialisieren (INPUT_PULLUP: kein externer Widerstand nötig)
  for (int i = 0; i < 4; i++) {
    pinMode(TASTER_PINS[i], INPUT_PULLUP);
  }
  Serial.println("[Taster]  Bereit auf GPIO 25, 26, 27, 32");

  // Kalibrierfaktor aus NVS laden (überlebt Flashen)
  prefs.begin("waage", true);
  if (prefs.isKey("kalib")) {
    kalibrierung = prefs.getFloat("kalib", -7050.0);
    Serial.print("[Waage]   Kalibrierfaktor aus Speicher geladen: ");
    Serial.println(kalibrierung);
  } else {
    Serial.println("[Waage]   Kein Kalibrierfaktor gespeichert – bitte 'k' senden!");
  }
  prefs.end();

  // HX711 initialisieren
  waage.begin(HX711_DT_PIN, HX711_SCK_PIN);
  if (waage.is_ready()) {
    waage.set_scale(kalibrierung);
    waage.tare();
    Serial.println("[Waage]   Bereit auf GPIO DT=18, SCK=19 – Nullpunkt gesetzt");
    Serial.println("[Waage]   Sende jederzeit 'k' fuer Neukalibrierung");
  } else {
    Serial.println("[Waage]   HX711 nicht gefunden – Verkabelung pruefen!");
  }

  // Verbinden
  connectWifi();
  connectMQTT();
}


// ===========================================================
//  LOOP
// ===========================================================
void loop() {
  if (!mqttClient.connected()) {
    connectMQTT();
  }
  mqttClient.loop();

  // ─── Kalibrierung jederzeit per 'k' auslösbar ─────────────
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == 'k' || c == 'K') {
      Serial.println("[Waage]   Kalibrierung gestartet – Waage leer lassen...");
      delay(2000);
      waage.tare();
      Serial.println("[Waage]   Nullpunkt gesetzt. Bekanntes Gewicht auflegen.");
      Serial.println("[Waage]   Dann Gramm-Wert eingeben (z.B. 1000) + ENTER:");
      while (Serial.available()) Serial.read();  // Puffer leeren (Rest vom 'k'-Enter)
      while (!Serial.available()) { delay(100); }
      float bekannt = Serial.parseFloat();
      Serial.flush();
      float rohwert = waage.get_value(10);
      kalibrierung  = rohwert / bekannt;
      waage.set_scale(kalibrierung);
      prefs.begin("waage", false);
      prefs.putFloat("kalib", kalibrierung);
      prefs.end();
      Serial.print("[Waage]   Kalibrierfaktor gespeichert: ");
      Serial.println(kalibrierung);
      Serial.println("[Waage]   Kalibrierung abgeschlossen – bleibt nach Neustart erhalten!");
      break;
    }
  }

  // ─── Barcode-Scanner auslesen ────────────────────────────
  while (ScannerSerial.available()) {
    char c = (char)ScannerSerial.read();
    if (c == '\r' || c == '\n') {
      if (scanBuffer.length() > 0) {
        lookupAndPublish(scanBuffer);
        scanBuffer = "";
      }
    } else {
      scanBuffer += c;
    }
  }

  unsigned long now = millis();

  // ─── Taster + Waage: Serial-Ausgabe alle 500 ms ───────────
  if (now - lastSensorMs >= SENSOR_INTERVAL) {
    lastSensorMs = now;

    Serial.print("[Taster]  ");
    for (int i = 0; i < 4; i++) {
      bool gedrueckt = (digitalRead(TASTER_PINS[i]) == LOW);
      Serial.print(TASTER_NAMEN[i]);
      Serial.print(": ");
      Serial.print(gedrueckt ? "AN " : "AUS");
      if (i < 3) Serial.print("  |  ");
    }
    Serial.println();

    if (waage.is_ready()) {
      float gramm   = waage.get_units(5);
      float rohwert = waage.get_value(1);
      Serial.print("[Waage]   Gewicht: ");
      Serial.print(gramm, 1);
      Serial.print(" g  (Rohwert: ");
      Serial.print(rohwert, 0);
      Serial.println(")");
    } else {
      Serial.println("[Waage]   HX711 nicht bereit");
    }
  }

}