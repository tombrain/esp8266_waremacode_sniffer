#include <ESP8266WiFi.h>
#include <PubSubClient.h>  // MQTT Bibliothek
#include <ArduinoJson.h>   // ArduinoJson Bibliothek

#include "secure.h"

//const char* ssid = "your-SSID";  // WLAN-SSID
//const char* password = "your-PASSWORD";  // WLAN-Passwort
const char* mqtt_server = "homeassistant.fritz.box";  // IP-Adresse des MQTT-Brokers
const int mqtt_port = 1883;  // Standard-MQTT-Port
//const char* mqtt_user = "mqtt-user";  // MQTT Benutzername
//const char* mqtt_pass = "mqtt-password";  // MQTT Passwort

WiFiClient espClient;
PubSubClient client(espClient);

const int durationMs_ = 1000;
const int gpioPin_ = 0;
const int SAMPLES = (durationMs_ * 1000) / 100;
bool data_[SAMPLES];

// Data pulse length (total length of a single 0/1 element), unit: us
const int dataLength = 1780;

// Sync time length (total length of a single s/S element), unit: us
const int syncLength = 5000;

// Struktur global definieren
struct Waremacode {
  String command;
  String device1;
  String device2;
  int count = 0;

  void clear() {
    command = "";
    device1 = "";
    device2 = "";
    count = 0;
  }

  void print() const {
    Serial.printf("Count: %d\n", count);
    Serial.println("Command: " + command);
    Serial.println("Device1: " + device1);
    Serial.println("Device2: " + device2);
  }
};

// Konstanten zur besseren Lesbarkeit
const int SIGNAL_LOW = 1;
const int SIGNAL_HIGH = 2;

void readCode();
void airScan();
void reconnect();
void sendMqttMessage(const String& jsonMessage);

// Funktion zum Senden des MQTT-Nachricht
void sendWaremaCode(const Waremacode &code) {
  StaticJsonDocument<200> doc;
  doc["command"] = code.command;
  doc["device1"] = code.device1;
  doc["device2"] = code.device2;
  doc["count"] = code.count;

  String jsonMessage;
  serializeJson(doc, jsonMessage);

  sendMqttMessage(jsonMessage); // Externe Funktion aufrufen
}

void setup() {
  Serial.begin(115200);
  Serial.println("Starting continuous scan...");
  pinMode(gpioPin_, INPUT);

  // WLAN-Verbindung herstellen
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("Connected to WiFi");

  // MQTT-Verbindung herstellen
  client.setServer(mqtt_server, mqtt_port);
  reconnect();
}

void loop() {
  static unsigned long lastScanTime = 0;
  const unsigned long scanInterval = 100; // Intervall für Scans in Millisekunden

  if (millis() - lastScanTime >= scanInterval) {
    lastScanTime = millis();
    airScan();
    readCode();
  }

  // Sicherstellen, dass die MQTT-Verbindung aufrechterhalten wird
  if (!client.connected()) {
    reconnect();
  }
  client.loop();
}

void airScan() {
  for (int i = 0; i < SAMPLES; i++) {
    data_[i] = GPIO_INPUT_GET(gpioPin_);
    delayMicroseconds(100);
  }
}

// Hauptfunktion zur Signalauswertung
void readCode() {
  int lowPulseCount = 0, highPulseCount = 0;
  int lastLowCount = 0, lastHighCount = 0;
  bool previousState = false;
  bool startReading = false;
  String binaryCode;
  Waremacode decodedCode;
  int lastSignalType = 0;

  for (size_t i = 0; i < SAMPLES; i++) {
    bool isHigh = data_[i];

    // Signalwechsel erkannt
    if (isHigh != previousState) {
      lastLowCount = lowPulseCount;
      lastHighCount = highPulseCount;

      if (lastSignalType != SIGNAL_LOW) lowPulseCount = 0;
      if (lastSignalType != SIGNAL_HIGH) highPulseCount = 0;

      // Prüfen, ob ein neues Signal beginnt
      if (lastLowCount > (syncLength / 100) && lastHighCount > (dataLength / 100) && lastSignalType == SIGNAL_LOW) {
        i++; // Überspringen
        startReading = true;
        lowPulseCount = highPulseCount = 0;
        lastLowCount = lastHighCount = 0;
        previousState = isHigh;
        continue;
      }

      // Daten auswerten, falls Signal aktiv
      if (startReading) {
        if ((lastHighCount + lastLowCount) >= (dataLength / 100)) {
          if (lastHighCount > (dataLength / 100)) {
            if (binaryCode.length() == 15) {
              decodedCode.clear();
              decodedCode.command = binaryCode;
              decodedCode.count = 1;
            } else if (binaryCode.length() == 10) {
              if (decodedCode.device1.isEmpty()) {
                decodedCode.device1 = binaryCode;
              } else {
                decodedCode.device2 = binaryCode;
                Serial.println("Empfangenes Signal:");
                decodedCode.print();

                // JSON senden
                sendWaremaCode(decodedCode);
              }
            }
            binaryCode = 'S'; // Neues Signal starten
          }
          binaryCode += data_[i + 1] ? '1' : '0';
          lowPulseCount = highPulseCount = lastLowCount = lastHighCount = 0;
        }
      }
    }

    // Pulszähler aktualisieren
    if (!isHigh) {
      lowPulseCount++;
      lastSignalType = SIGNAL_LOW;
    } else {
      highPulseCount++;
      lastSignalType = SIGNAL_HIGH;
    }
    previousState = isHigh;
  }
}

void sendMqttMessage(const String& jsonMessage) {
  if (client.publish("waremacode/detected", jsonMessage.c_str())) {
    Serial.println("Waremacode als JSON gesendet: " + jsonMessage);
  } else {
    Serial.println("Fehler beim Senden des Waremacodes über MQTT");
  }
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Verbindung zu MQTT-Server...");
    if (client.connect("ESP8266Client", mqtt_username, mqtt_password)) {
      Serial.println("Verbunden");
    } else {
      Serial.print("Fehler, rc=");
      Serial.print(client.state());
      Serial.println("Versuche es erneut in 5 Sekunden");
      delay(5000);
    }
  }
}
