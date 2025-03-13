#include <ESP8266WiFi.h>
#include <map>
#include <string>

const int32_t durationMs_{1000};
uint8_t gpioPin_{0};
std::vector<bool> data_;
void readCode();
void airScan(void);

void setup() {
  Serial.begin(115200);
  Serial.println("Starting continuous scan...");
}

void loop() {
  static unsigned long lastScanTime = 0;
  const unsigned long scanInterval = 100; // Intervall für Scans in Millisekunden

  if (millis() - lastScanTime >= scanInterval) {
    lastScanTime = millis();
    airScan();
    readCode();
  }
}

void airScan(void) {
    const int32_t MICROSECONDS_PER_MILLISECOND = 1000;
    const int32_t SAMPLES = (durationMs_ * MICROSECONDS_PER_MILLISECOND) / 100;

    pinMode(gpioPin_, INPUT);

    data_.clear();
    while (data_.size() < static_cast<size_t>(SAMPLES)) {
        data_.push_back(digitalRead(gpioPin_) > 0);
        delayMicroseconds(100);
    }
}

void readCode() {
    struct Waremacode {
        std::string command{};
        std::string device1{};
        std::string device2{};
        int count{0};

        void clear() {
            command.clear();
            device1.clear();
            device2.clear();
            count = 0;
        }

        auto key() const {
            return command + device1 + device1;
        }

        void dump() const {
            Serial.print("Count: ");
            Serial.println(count);
            Serial.println("Code: ");
            Serial.println(command.c_str());
            Serial.println(device1.c_str());
            Serial.println(device2.c_str());
        }
    };

    std::map<std::string, Waremacode> mapKeyCode{};
    std::vector<Waremacode> collectedCodes{};

    bool previousData{false};
    int constexpr iLOW{1};
    int constexpr iHIGH{2};
    int cntLow{0};
    int cntHigh{0};
    int lastCntLow{0};
    int lastCntHigt{0};
    int startReading{0};
    std::string actualBinarycode{};
    Waremacode actualWaremacode{};
    int last = 0;

    for (size_t i{0}; i < data_.size(); i++) {
        auto const isHigh{data_[i]};

        if ((isHigh && !previousData) || (!isHigh && previousData)) {
            lastCntLow = cntLow;
            lastCntHigt = cntHigh;

            if (last != iLOW) cntLow = 0;
            if (last != iHIGH) cntHigh = 0;

            if (lastCntLow > 50 && lastCntHigt > 30) {
                if (last == iLOW) {
                    i++;
                    startReading = 1;
                    cntLow = 0;
                    cntHigh = 0;
                    lastCntLow = 0;
                    lastCntHigt = 0;
                    previousData = isHigh;
                    continue;
                }
            }

            if (startReading == 1) {
                if ((lastCntHigt + lastCntLow) >= 10) {
                    if (lastCntHigt > 25) {
                        auto const len{actualBinarycode.size()};
                        if (len == 15) {
                            actualWaremacode.clear();
                            actualWaremacode.command = actualBinarycode;
                            actualWaremacode.count = 1;
                        } else if (len == 10) {
                            if (actualWaremacode.device1.empty()) {
                                actualWaremacode.device1 = actualBinarycode;
                            } else {
                                actualWaremacode.device2 = actualBinarycode;
                                if (mapKeyCode.find(actualWaremacode.key()) != mapKeyCode.end()) {
                                    mapKeyCode[actualWaremacode.key()].count++;
                                } else {
                                    mapKeyCode.insert({actualWaremacode.key(), actualWaremacode});
                                }
                                collectedCodes.push_back(actualWaremacode);
                            }
                        }
                        actualBinarycode = 'S';
                    }
                    auto const nextLine{data_[i + 1]};
                    if (!nextLine) {
                        actualBinarycode += '0';
                    } else {
                        actualBinarycode += '1';
                    }
                    cntLow = 0;
                    cntHigh = 0;
                    lastCntLow = 0;
                    lastCntHigt = 0;
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

    for (auto const& e : mapKeyCode) {
        e.second.dump();
    }
}
