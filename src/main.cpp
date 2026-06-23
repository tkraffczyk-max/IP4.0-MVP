/*Neuer Code 18.06.26
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
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT  32
#define OLED_RESET     -1
#define OLED_ADDRESS 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

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

// ─── FH Aachen Chatbot (MHD-Schätzung, nur spielerisch) ──
const char* CHATBOT_URL    = "https://chat.kiconnect.nrw/api/v1/chat/completions";
const char* CHATBOT_APIKEY = "6a394fdab443313573c55afa:/5IBu0qILFwUHMjS4BnS3ypw2UPuFFrquYOhJJ31BqI=";
const char* CHATBOT_MODEL  = "openai-gpt-oss-120b";

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

// ─── Action-Schalter: Einlagern (IN) / Entnehmen (OUT) ────
//  Mapping: LOW (gedrückt / auf OUT geschaltet) → "out"
//           HIGH (offen)                        → "in"
//  GPIO 25 entspricht T1(G25) aus TASTER_PINS oben.
const int ACTION_SWITCH_PIN = 25;

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
void showDisplay(const String& l1, const String& l2, const String& l3, const String& l4);
void publishOutStatus(const char* status, float gewicht, const char* line2);
void publishDelete(float gewicht);
void fetchAndDisplay(const String& ean);
void publishWithWeight(const String& ean, float measuredWeight = -1.0);
void fetchMHD(const String& produktName, const String& categories);

// ─── Globale Objekte ───────────────────────────────────────
WiFiClientSecure  wifiSecure;
PubSubClient      mqttClient(wifiSecure);

unsigned long lastPublishMs   = 0;
unsigned long lastSensorMs    = 0;
int           messageCount    = 0;
String        scanBuffer      = "";
const long    SENSOR_INTERVAL = 2000;

// ─── Scan-Zustand ─────────────────────────────────────────
String        pendingEAN      = "";
bool          scanPending     = false;

// ─── Zwischengespeicherte Produktdaten ────────────────────
String        storedName      = "";
String        storedBrands    = "";
String        storedCategories= "";
String        storedQuantity  = "";
String        storedMHD       = "";
String        storedMHD_datum = "";
bool          productFetched  = false;

// ─── "Bereit"-Anzeige nach Publish ───────────────────────
unsigned long readyTimerStart = 0;
bool          readyPending    = false;
const long    READY_DELAY_MS  = 5000;

// ─── In-Monitoring (Einlagerung per Gewichtszunahme) ──────
enum InState { IN_IDLE, IN_WAIT_PLACE, IN_SETTLING_IN };
InState       inState         = IN_IDLE;
float         inRefWeight     = 0.0;
unsigned long inSettleStart   = 0;
unsigned long inLastReadMs    = 0;

// ─── Out-Monitoring (Entnahme-Knopf GPIO25) ───────────────
enum OutState { OUT_IDLE, OUT_ARMED, OUT_SETTLING_1, OUT_WAIT_RETURN, OUT_SETTLING_2 };
OutState      outState        = OUT_IDLE;
float         outRefWeight    = 0.0;   // Gewicht VOR Entnahme
float         outAfterWeight  = 0.0;  // Gewicht NACH Entnahme (eingeschwungen)
unsigned long outSettleStart  = 0;
unsigned long outLastReadMs   = 0;
bool          prevOutBtn      = HIGH;
const long    OUT_SETTLE_MS   = 2000;
const float   OUT_THRESHOLD_G = 25.0;

// ─── Delete-Monitoring (Löschen-Knopf GPIO27) ─────────────
#define DELETE_BTN_PIN 27
enum DelState { DEL_IDLE, DEL_ARMED, DEL_SETTLING };
DelState      delState        = DEL_IDLE;
float         delRefWeight    = 0.0;
unsigned long delSettleStart  = 0;
unsigned long delLastReadMs   = 0;
bool          prevDelBtn      = HIGH;


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


// ─── Zentrierte Display-Ausgabe ───────────────────────────
void showDisplay(const String& l1, const String& l2 = "",
                 const String& l3 = "", const String& l4 = "") {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  const String* lines[4] = {&l1, &l2, &l3, &l4};
  int count = 0;
  for (int i = 0; i < 4; i++) if (lines[i]->length() > 0) count++;

  int startY = (SCREEN_HEIGHT - count * 8) / 2;
  int row    = 0;
  for (int i = 0; i < 4; i++) {
    if (lines[i]->length() == 0) continue;
    int16_t x1, y1; uint16_t w, h;
    display.getTextBounds(*lines[i], 0, 0, &x1, &y1, &w, &h);
    int x = max(0, (SCREEN_WIDTH - (int)w) / 2);
    display.setCursor(x, startY + row * 8);
    display.print(*lines[i]);
    row++;
  }
  display.display();
}

// ─── Out_1 / Out_2 per MQTT senden ───────────────────────
void publishOutStatus(const char* status, float gewicht, const char* line2) {
  if (!mqttClient.connected()) return;
  StaticJsonDocument<256> doc;
  doc["device"]    = CLIENT_ID;
  doc["status"]    = status;
  doc["gewicht_g"] = gewicht;
  doc["uptime_s"]  = millis() / 1000;
  char buf[256];
  serializeJson(doc, buf);
  bool ok = mqttClient.publish(TOPIC_PUB, buf);
  Serial.printf("[MQTT] %s: %.1f g – %s\n", status, gewicht, ok ? "OK" : "FEHLER");
  showDisplay(status, String(line2), "", "");
}

// ─── Delete-Payload per MQTT senden ──────────────────────
void publishDelete(float gewicht) {
  if (!mqttClient.connected()) return;
  StaticJsonDocument<256> doc;
  doc["device"]    = CLIENT_ID;
  doc["status"]    = "delete";
  doc["gewicht_g"] = gewicht;
  doc["uptime_s"]  = millis() / 1000;
  char buf[256];
  serializeJson(doc, buf);
  bool ok = mqttClient.publish(TOPIC_PUB, buf);
  Serial.printf("[DEL] delete: %.1f g Restgewicht – %s\n", gewicht, ok ? "OK" : "FEHLER");
  showDisplay("Produkt geloescht", String(gewicht, 1) + "g Rest", "", "");
}

// ─── FH Aachen Chatbot: MHD-Schätzung (spielerisch) ──────
void fetchMHD(const String& produktName, const String& categories) {
  storedMHD       = "";
  storedMHD_datum = "";
  showDisplay("Schaetze MHD...", produktName.substring(0, 21), "", "");

  WiFiClientSecure httpSecure;
  httpSecure.setInsecure();
  HTTPClient http;
  http.begin(httpSecure, CHATBOT_URL);
  http.setTimeout(15000);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", String("Bearer ") + CHATBOT_APIKEY);

  String prompt = String("Wie viele Tage betraegt das typische Mindesthaltbarkeitsdatum (MHD)"
                  " eines UNGEÖFFNETEN, aus dem Supermarkt gekauften Produkts ab Kaufdatum?"
                  " Gemeint ist die aufgedruckte Haltbarkeit auf der Verpackung,"
                  " NICHT die Haltbarkeit nach dem Oeffnen."
                  " Produkt: '") + produktName + "', Kategorie: '" + categories +
                  "'. Antworte AUSSCHLIESSLICH mit einer einzigen ganzen Zahl"
                  " (die Anzahl der Tage). Kein Text, keine Einheit, nur die Zahl.";

  DynamicJsonDocument reqDoc(1280);
  reqDoc["model"]       = CHATBOT_MODEL;
  reqDoc["max_tokens"]  = 500;
  reqDoc["temperature"] = 0.0;
  JsonArray msgs        = reqDoc.createNestedArray("messages");
  JsonObject sys        = msgs.createNestedObject();
  sys["role"]           = "system";
  sys["content"]        = "Du bist ein Lebensmittelexperte. Antworte ausschliesslich"
                          " mit einer ganzen Zahl (Anzahl Tage). Kein Text, kein Satz.";
  JsonObject m          = msgs.createNestedObject();
  m["role"]             = "user";
  m["content"]          = prompt;

  String body;
  serializeJson(reqDoc, body);

  int code = http.POST(body);
  Serial.print("[Chatbot] HTTP Code: "); Serial.println(code);

  if (code != HTTP_CODE_OK) {
    Serial.println("[Chatbot] Fehler: " + http.errorToString(code));
    http.end();
    storedMHD = storedMHD_datum = "unbekannt";
    return;
  }

  String raw = http.getString();
  http.end();
  Serial.print("[Chatbot] Response: "); Serial.println(raw);

  DynamicJsonDocument resDoc(2048);
  DeserializationError err = deserializeJson(resDoc, raw);
  if (err) {
    Serial.print("[Chatbot] JSON Fehler: "); Serial.println(err.c_str());
    storedMHD = storedMHD_datum = "unbekannt";
    return;
  }

  String rawContent = resDoc["choices"][0]["message"]["content"] | "0";
  rawContent.trim();
  int days = rawContent.toInt();
  Serial.print("[Chatbot] Tage: "); Serial.println(days);

  if (days <= 0) {
    storedMHD = storedMHD_datum = "unbekannt";
    return;
  }

  // Tage in lesbare Dauer umwandeln
  if      (days <= 6)   storedMHD = String(days) + " Tage";
  else if (days <= 13)  storedMHD = "1 Woche";
  else if (days <= 29)  storedMHD = String(days / 7) + " Wochen";
  else if (days <= 59)  storedMHD = "1 Monat";
  else if (days <= 364) storedMHD = String(days / 30) + " Monate";
  else if (days <= 729) storedMHD = "1 Jahr";
  else                  storedMHD = String(days / 365) + " Jahre";

  // MHD-Datum berechnen: aktuelles Datum + Tage
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    time_t t = mktime(&timeinfo);
    t += (time_t)days * 86400;
    struct tm future;
    localtime_r(&t, &future);
    char dateBuf[11];
    snprintf(dateBuf, sizeof(dateBuf), "%02d.%02d.%04d",
             future.tm_mday, future.tm_mon + 1, future.tm_year + 1900);
    storedMHD_datum = String(dateBuf);
  } else {
    storedMHD_datum = "NTP fehlt";
  }

  Serial.print("[Chatbot] MHD Dauer: "); Serial.println(storedMHD);
  Serial.print("[Chatbot] MHD Datum: "); Serial.println(storedMHD_datum);
}

// ─── Sofortanzeige: API abrufen + Display ─────────────────
void fetchAndDisplay(const String& ean) {
  productFetched  = false;
  storedMHD       = "";
  storedMHD_datum = "";
  Serial.print("[API] EAN: "); Serial.println(ean);

  showDisplay("Suche Produkt...", ean);

  WiFiClientSecure httpSecure;
  httpSecure.setInsecure();

  HTTPClient http;
  String url = "https://world.openfoodfacts.org/api/v0/product/" + ean
             + ".json?fields=status,product_name,product_name_de,product_name_en,brands,categories,product_quantity,quantity_per_unit";
  http.begin(httpSecure, url);
  http.setTimeout(15000);
  http.addHeader("User-Agent", "ESP32-IoT-Scanner/1.0");

  int code = http.GET();
  Serial.print("[API] HTTP Code: "); Serial.println(code);

  if (code != HTTP_CODE_OK) {
    Serial.println("[API] Fehler: " + http.errorToString(code));
    http.end();
    showDisplay("API Fehler!", http.errorToString(code));
    return;
  }

  String raw = http.getString();
  http.end();

  DynamicJsonDocument doc(1024);
  if (deserializeJson(doc, raw) || doc["status"].as<int>() != 1) {
    Serial.println("[API] Produkt nicht gefunden");
    showDisplay("Nicht gefunden!", ean);
    return;
  }

  storedName = doc["product"]["product_name"] | "";
  if (storedName.isEmpty()) storedName = doc["product"]["product_name_de"] | "";
  if (storedName.isEmpty()) storedName = doc["product"]["product_name_en"] | "";
  if (storedName.isEmpty()) storedName = "unbekannt";
  storedBrands     = doc["product"]["brands"]       | "unbekannt";
  storedCategories = doc["product"]["categories"]   | "unbekannt";
  if (storedCategories.length() > 120) storedCategories = storedCategories.substring(0, 120);

  storedQuantity = "";
  if (!doc["product"]["product_quantity"].isNull())
    storedQuantity = doc["product"]["product_quantity"].as<String>();
  else if (!doc["product"]["quantity_per_unit"].isNull())
    storedQuantity = doc["product"]["quantity_per_unit"].as<String>();

  productFetched = true;

  Serial.print("[API] Name:   "); Serial.println(storedName);
  Serial.print("[API] Marke:  "); Serial.println(storedBrands);
  Serial.print("[API] Menge:  "); Serial.println(storedQuantity);

  // MHD vom FH Aachen Chatbot schätzen lassen (spielerisch)
  fetchMHD(storedName, storedCategories);

  showDisplay(storedName.substring(0, 21),
              storedBrands.substring(0, 21),
              storedQuantity.length() > 0 ? storedQuantity.substring(0, 21) : "",
              "Bitte wiegen...");
}

// ─── Nach 5s: Gewicht lesen + MQTT publish ────────────────
void publishWithWeight(const String& ean, float measuredWeight) {
  if (!productFetched) return;
  if (!mqttClient.connected()) {
    Serial.println("[MQTT] Nicht verbunden – Publish abgebrochen");
    return;
  }

  // Bei "out" kein Publish – wird im Backend verarbeitet
  if (digitalRead(ACTION_SWITCH_PIN) == LOW) {
    Serial.println("[MQTT] Status 'out' – kein Publish");
    showDisplay("Status: OUT", "Kein Publish");
    readyTimerStart = millis();
    readyPending    = true;
    return;
  }

  const char* action = "in";
  float gewicht = (measuredWeight >= 0) ? measuredWeight
                : (waage.is_ready() ? waage.get_units(5) : 0.0);

  DynamicJsonDocument mqtt_doc(1024);
  mqtt_doc["device"]           = CLIENT_ID;
  mqtt_doc["action"]           = action;
  mqtt_doc["ean"]              = ean;
  mqtt_doc["product_name"]     = storedName;
  mqtt_doc["brands"]           = storedBrands;
  mqtt_doc["categories"]       = storedCategories;
  mqtt_doc["netto_g"]          = storedQuantity;
  mqtt_doc["mhd_schaetzung"]   = storedMHD;
  mqtt_doc["mhd_datum"]        = storedMHD_datum;
  mqtt_doc["gewicht_g"]        = gewicht;
  mqtt_doc["uptime_s"]         = millis() / 1000;

  char jsonBuffer[1024];
  serializeJson(mqtt_doc, jsonBuffer);

  bool ok = mqttClient.publish(TOPIC_PUB, jsonBuffer);
  Serial.print("[MQTT] Gesendet: "); Serial.println(ok ? "OK ✓" : "FEHLER ✗");
  Serial.println(jsonBuffer);

  showDisplay(ok ? "Gesendet!" : "Send-Fehler!", storedName.substring(0, 21));

  readyTimerStart = millis();
  readyPending    = true;
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

  // Action-Schalter explizit initialisieren (GPIO 32 wird oben bereits durch
  // TASTER_PINS-Schleife abgedeckt; dieser Aufruf dokumentiert die Absicht)
  pinMode(ACTION_SWITCH_PIN, INPUT_PULLUP);
  Serial.println("[Action]  Schalter Einlagern/Entnehmen bereit auf GPIO 25");

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

  // OLED Display initialisieren (SDA=GPIO21, SCL=GPIO22)
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    Serial.println("[Display] SSD1306 nicht gefunden – Verkabelung pruefen!");
  } else {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("System bereit.");
    display.display();
    Serial.println("[Display] SSD1306 bereit auf SDA=GPIO21, SCL=GPIO22");
  }

  // Verbinden
  connectWifi();

  // NTP Zeitsynchronisation (fuer MHD-Datumsberechnung)
  setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
  tzset();
  configTime(0, 0, "pool.ntp.org");
  Serial.print("[NTP] Synchronisiere Zeit");
  { struct tm t; for (int i = 0; i < 20 && !getLocalTime(&t); i++) { delay(500); Serial.print("."); } }
  Serial.println();

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
    if (c == 'r' || c == 'R') {
      prefs.begin("waage", false);
      prefs.remove("kalib");
      prefs.end();
      waage.set_scale(-7050.0);
      waage.tare();
      Serial.println("[Waage]   Kalibrierfaktor zurueckgesetzt! Bitte neu kalibrieren mit 'k'.");
    } else if (c == 'k' || c == 'K') {
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
        pendingEAN   = scanBuffer;
        scanBuffer   = "";
        scanPending  = true;
        productFetched = false;
        inRefWeight  = waage.is_ready() ? waage.get_units(3) : 0.0;
        inLastReadMs = millis();
        inState      = IN_WAIT_PLACE;
        Serial.printf("[IN] Scan: %s – Ref: %.1f g\n", pendingEAN.c_str(), inRefWeight);
        showDisplay("Bitte ablegen", pendingEAN, String(inRefWeight, 1) + "g Ref", "");
      }
    } else {
      scanBuffer += c;
    }
  }

  // ─── Nach Publish: "Bereit" nach 5s anzeigen ─────────────
  if (readyPending && (millis() - readyTimerStart >= READY_DELAY_MS)) {
    readyPending = false;
    showDisplay("System bereit", "Bitte scannen");
  }

  unsigned long now = millis();

  // ─── In-Monitoring: Gewichtszunahme erkennen ─────────────
  if (inState == IN_WAIT_PLACE && waage.is_ready() && (now - inLastReadMs >= 500)) {
    inLastReadMs = now;
    float curW = waage.get_units(1);
    if ((curW - inRefWeight) > OUT_THRESHOLD_G) {
      inSettleStart = now;
      inState       = IN_SETTLING_IN;
      Serial.printf("[IN] Zunahme %.1f g – warte 2s\n", curW - inRefWeight);
      showDisplay("Produkt erkannt", "Einpendeln...", "", "");
    }
  }
  if (inState == IN_SETTLING_IN && (now - inSettleStart >= OUT_SETTLE_MS)) {
    unsigned long t0 = millis();
    while (!waage.is_ready() && millis() - t0 < 300) delay(5);
    float settled = waage.is_ready() ? waage.get_units(5) : 0.0;
    if ((settled - inRefWeight) > OUT_THRESHOLD_G) {
      scanPending = false;
      inState     = IN_IDLE;
      showDisplay("Suche Produkt...", pendingEAN, "", "");
      fetchAndDisplay(pendingEAN);
      publishWithWeight(pendingEAN, settled);
      pendingEAN  = "";
    } else {
      inState = IN_WAIT_PLACE;
    }
  }

  // ─── Out-Monitoring: Entnahme-Knopf (GPIO25, Flanke) ─────
  {
    bool outBtn  = (digitalRead(ACTION_SWITCH_PIN) == LOW);
    bool pressed = (outBtn && !prevOutBtn);
    prevOutBtn   = outBtn;

    bool  freshRead = false;
    float curW      = 0.0;
    // Nur in ARMED/WAIT_RETURN lesen – nicht während Einpendelzeiten
    if ((outState == OUT_ARMED || outState == OUT_WAIT_RETURN) &&
        waage.is_ready() && (now - outLastReadMs >= 500)) {
      outLastReadMs = now;
      curW          = waage.get_units(1);
      freshRead     = true;
    }

    switch (outState) {
      case OUT_IDLE:
        if (pressed) {
          outRefWeight  = waage.is_ready() ? waage.get_units(3) : 0.0;
          outLastReadMs = now;
          outState      = OUT_ARMED;
          Serial.printf("[OUT] Entnahme aktiv – Ref: %.1f g\n", outRefWeight);
          showDisplay("Entnahme aktiv", String(outRefWeight, 1) + "g Ref", "", "");
        }
        break;

      case OUT_ARMED:
        if (freshRead && (outRefWeight - curW) > OUT_THRESHOLD_G) {
          outSettleStart = now;
          outState       = OUT_SETTLING_1;
          Serial.printf("[OUT] Abnahme %.1f g – warte 2s\n", outRefWeight - curW);
          showDisplay("Messe Entnahme", "Einpendeln...", "", "");
        }
        break;

      case OUT_SETTLING_1:
        if (now - outSettleStart >= OUT_SETTLE_MS) {
          unsigned long t0 = millis();
          while (!waage.is_ready() && millis() - t0 < 300) delay(5);
          float settled = waage.is_ready() ? waage.get_units(5) : 0.0;
          float removed = outRefWeight - settled;
          if (removed > OUT_THRESHOLD_G) {
            outAfterWeight = settled;
            char disp[22];
            snprintf(disp, sizeof(disp), "%.1f g entnommen", removed);
            publishOutStatus("Out_1", removed, disp);
            outState = OUT_WAIT_RETURN;
          } else {
            outState = OUT_ARMED;
          }
        }
        break;

      case OUT_WAIT_RETURN:
        if (freshRead && (curW - outAfterWeight) > OUT_THRESHOLD_G) {
          outSettleStart = now;
          outState       = OUT_SETTLING_2;
          Serial.printf("[OUT] Zunahme %.1f g – warte 2s\n", curW - outAfterWeight);
          showDisplay("Objekt zurueck", "Einpendeln...", "", "");
        }
        break;

      case OUT_SETTLING_2:
        if (now - outSettleStart >= OUT_SETTLE_MS) {
          unsigned long t0 = millis();
          while (!waage.is_ready() && millis() - t0 < 300) delay(5);
          float settled = waage.is_ready() ? waage.get_units(5) : 0.0;
          if ((settled - outAfterWeight) > OUT_THRESHOLD_G) {
            float netto = outRefWeight - outAfterWeight;
            if (mqttClient.connected()) {
              StaticJsonDocument<256> doc;
              doc["device"]    = CLIENT_ID;
              doc["status"]    = "Out_2";
              doc["gewicht_g"] = settled;
              doc["netto_g"]   = netto;
              doc["uptime_s"]  = millis() / 1000;
              char buf[256];
              serializeJson(doc, buf);
              bool ok = mqttClient.publish(TOPIC_PUB, buf);
              Serial.printf("[MQTT] Out_2: %.1f g gesamt, %.1f g netto – %s\n",
                            settled, netto, ok ? "OK" : "FEHLER");
            }
            showDisplay("Out_2",
                        String(settled, 1) + "g gesamt",
                        String(outRefWeight - outAfterWeight, 1) + "g Netto", "");
            outState = OUT_IDLE;
          } else {
            outState = OUT_WAIT_RETURN;
          }
        }
        break;
    }
  }

  // ─── Delete-Monitoring: Produkt komplett entfernen (GPIO27) ─
  {
    bool delBtn  = (digitalRead(DELETE_BTN_PIN) == LOW);
    bool pressed = (delBtn && !prevDelBtn);
    prevDelBtn   = delBtn;

    bool  freshRead = false;
    float curW      = 0.0;
    if (delState == DEL_ARMED &&
        waage.is_ready() && (now - delLastReadMs >= 500)) {
      delLastReadMs = now;
      curW          = waage.get_units(1);
      freshRead     = true;
    }

    switch (delState) {
      case DEL_IDLE:
        if (pressed) {
          delRefWeight  = waage.is_ready() ? waage.get_units(3) : 0.0;
          delLastReadMs = now;
          delState      = DEL_ARMED;
          Serial.printf("[DEL] Loeschen aktiv – Ref: %.1f g\n", delRefWeight);
          showDisplay("Loeschen aktiv", String(delRefWeight, 1) + "g Ref", "Produkt abnehmen", "");
        }
        break;

      case DEL_ARMED:
        if (freshRead && (delRefWeight - curW) > OUT_THRESHOLD_G) {
          delSettleStart = now;
          delState       = DEL_SETTLING;
          Serial.printf("[DEL] Abnahme %.1f g – warte 2s\n", delRefWeight - curW);
          showDisplay("Messe Loeschung", "Einpendeln...", "", "");
        }
        break;

      case DEL_SETTLING:
        if (now - delSettleStart >= OUT_SETTLE_MS) {
          unsigned long t0 = millis();
          while (!waage.is_ready() && millis() - t0 < 300) delay(5);
          float settled = waage.is_ready() ? waage.get_units(5) : 0.0;
          if ((delRefWeight - settled) > OUT_THRESHOLD_G) {
            publishDelete(settled);
            delState = DEL_IDLE;
          } else {
            delState = DEL_ARMED;
          }
        }
        break;
    }
  }

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