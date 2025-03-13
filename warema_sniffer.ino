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

void readCode();
void airScan();
void reconnect();
void sendMqttMessage(const String& jsonMessage);

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

void readCode() {
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

    String key() const {
      return command + device1 + device2;
    }

    void dump() const {
      Serial.printf("Count: %d\n", count);
      Serial.println("Code: " + command);
      Serial.println(device1);
      Serial.println(device2);
    }
  };

  const int iLOW = 1;
  const int iHIGH = 2;
  int cntLow = 0, cntHigh = 0, lastCntLow = 0, lastCntHigh = 0;
  bool previousData = false;
  int startReading = 0;
  String actualBinarycode;
  Waremacode actualWaremacode;
  int last = 0;

  for (size_t i = 0; i < SAMPLES; i++) {
    bool isHigh = data_[i];

    if ((isHigh && !previousData) || (!isHigh && previousData)) {
      lastCntLow = cntLow;
      lastCntHigh = cntHigh;

      if (last != iLOW) cntLow = 0;
      if (last != iHIGH) cntHigh = 0;

      if (lastCntLow > 50 && lastCntHigh > 30) {
        if (last == iLOW) {
          i++;
          startReading = 1;
          cntLow = 0;
          cntHigh = 0;
          lastCntLow = 0;
          lastCntHigh = 0;
          previousData = isHigh;
          continue;
        }
      }

      if (startReading == 1) {
        if ((lastCntHigh + lastCntLow) >= 10) {
          if (lastCntHigh > 25) {
            if (actualBinarycode.length() == 15) {
              actualWaremacode.clear();
              actualWaremacode.command = actualBinarycode;
              actualWaremacode.count = 1;
            } else if (actualBinarycode.length() == 10) {
              if (actualWaremacode.device1 == "") {
                actualWaremacode.device1 = actualBinarycode;
              } else {
                actualWaremacode.device2 = actualBinarycode;
                Serial.println("Empfangenes Signal:");
                actualWaremacode.dump();
                
                // JSON-Nachricht erstellen
                StaticJsonDocument<200> doc;
                doc["command"] = actualWaremacode.command;
                doc["device1"] = actualWaremacode.device1;
                doc["device2"] = actualWaremacode.device2;
                doc["count"] = actualWaremacode.count;

                String jsonMessage;
                serializeJson(doc, jsonMessage);

                // Senden des JSON-Nachricht über MQTT
                sendMqttMessage(jsonMessage);
              }
            }
            actualBinarycode = 'S';
          }
          actualBinarycode += data_[i + 1] ? '1' : '0';
          cntLow = 0;
          cntHigh = 0;
          lastCntLow = 0;
          lastCntHigh = 0;
        }
      }
    }

    if (!isHigh) {
      cntLow++;
      last = iLOW;
    } else {
      cntHigh++;
      last = iHIGH;
    }
    previousData = isHigh;
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
