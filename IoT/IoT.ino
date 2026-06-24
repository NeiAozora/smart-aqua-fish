#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <math.h>

// ======================== KONFIGURASI UMUM ========================
const char* ssid = "WIFI_SSID";          // Ganti
const char* password = "WIFI_PASSWORD";  // Ganti
String apiBase = "http://192.168.1.100:8000/api"; // IP server API
String deviceId = "DEVICE_001";

// ======================== PIN ========================
#define MQ135_PIN     34      // ADC1_CH6 (sensor MQ135)
#define RELAY_PIN     26      // Kontrol pompa (HIGH = nyala)

// LED Debugging (eksternal, active HIGH)
#define LED_WIFI      13      // WiFi connected
#define LED_SENSOR    12      // Kirim sensor
#define LED_COMMAND   14      // Terima perintah
#define LED_RESPONSE  27      // Kirim respons
#define LED_ERROR     33      // Error
#define LED_HEARTBEAT 2       // Built-in LED (active LOW pada banyak board)

// Interval (ms)
const unsigned long SENSOR_INTERVAL = 30000;   // 30 detik
const unsigned long COMMAND_INTERVAL = 5000;   // 5 detik
const unsigned long HEARTBEAT_INTERVAL = 1000; // 1 detik untuk heartbeat

// ======================== VARIABEL GLOBAL ========================
float R0 = 1.0;                     // Resistansi baseline MQ135
unsigned long lastSensorSend = 0;
unsigned long lastCommandFetch = 0;
unsigned long lastHeartbeat = 0;
bool heartbeatState = false;

// Perintah terakhir yang diterima
int currentCommandId = -1;
bool pendingCommand = false;

// ======================== FUNGSI PEMBANTU ========================
// Kalibrasi MQ135 – udara bersih (resistansi baseline R0)
float calibrateMQ135(int samples = 50, int delayMs = 20) {
  float rs_sum = 0;
  for (int i = 0; i < samples; i++) {
    int val = analogRead(MQ135_PIN);
    float voltage = val * (3.3 / 4095.0);   // ESP32 ADC 12-bit, Vref 3.3V
    // Rs = RL * (Vcc - Vout) / Vout, asumsi RL = 10k, Vcc = 3.3V
    float rs = 10000.0 * (3.3 - voltage) / voltage;
    rs_sum += rs;
    delay(delayMs);
  }
  return rs_sum / samples;
}

// Membaca rasio Rs/R0 saat ini
float readRsRoRatio() {
  int val = analogRead(MQ135_PIN);
  float voltage = val * (3.3 / 4095.0);
  float rs = 10000.0 * (3.3 - voltage) / voltage;
  if (R0 == 0) return 1.0;
  return rs / R0;
}

// Konversi rasio ke PPM amonia (dari kurva NH3 MQ135)
float ammoniaPPM(float ratio) {
  if (ratio <= 0) return 0;
  float logRatio = log10(ratio);
  float logPPM = (0.882 - logRatio) / 0.326;
  return pow(10, logPPM);
}

// Blokir singkat LED (non‑blocking kurang praktis, tapi aman karena hanya 100ms)
void flashLED(int pin, int durationMs = 100) {
  digitalWrite(pin, HIGH);
  delay(durationMs);
  digitalWrite(pin, LOW);
}

// ======================== KOMUNIKASI SERVER ========================
void sendSensorData(float ppm) {
  if (WiFi.status() != WL_CONNECTED) {
    flashLED(LED_ERROR, 200);  // indikasi gagal
    Serial.println("[WiFi] Tidak terhubung");
    return;
  }
  HTTPClient http;
  String url = apiBase + "/iot/sensor";
  http.begin(url);
  http.addHeader("Content-Type", "application/json");

  StaticJsonDocument<200> doc;
  doc["device_id"] = deviceId;
  doc["kadar_amonia"] = ppm;

  // Timestamp ISO 8601
  time_t now = time(nullptr);
  struct tm* t = localtime(&now);
  char timestamp[30];
  sprintf(timestamp, "%04d-%02d-%02dT%02d:%02d:%02dZ",
          t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
          t->tm_hour, t->tm_min, t->tm_sec);
  doc["recorded_at"] = timestamp;

  String json;
  serializeJson(doc, json);
  
  int code = http.POST(json);
  Serial.printf("[Sensor] POST %s -> %d\n", json.c_str(), code);
  if (code == 200 || code == 201) {
    flashLED(LED_SENSOR);  // Hijau berkedip
  } else {
    flashLED(LED_ERROR);   // Merah berkedip jika gagal
    Serial.printf("[Sensor] Gagal, kode: %d\n", code);
  }
  http.end();
}

bool fetchCommand() {
  if (WiFi.status() != WL_CONNECTED) return false;

  HTTPClient http;
  String url = apiBase + "/iot/command/" + deviceId;
  http.begin(url);
  int code = http.GET();

  if (code == 200) {
    String payload = http.getString();
    Serial.printf("[Command] GET 200: %s\n", payload.c_str());

    StaticJsonDocument<256> doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (!err && doc.containsKey("command_id") && doc.containsKey("perintah")) {
      currentCommandId = doc["command_id"];
      String perintah = doc["perintah"].as<String>();
      pendingCommand = true;
      flashLED(LED_COMMAND);   // Kuning berkedip

      // Eksekusi perintah
      if (perintah == "kuras") {
        digitalWrite(RELAY_PIN, HIGH);
        Serial.println("[Action] Pompa ON (kuras)");
        sendResponse("OK", "Pompa menyala");
      } else if (perintah == "berhenti") {
        digitalWrite(RELAY_PIN, LOW);
        Serial.println("[Action] Pompa OFF (berhenti)");
        sendResponse("OK", "Pompa dimatikan");
      } else {
        sendResponse("OK", "Perintah dikenali tanpa aksi");
      }
      return true;
    } else {
      Serial.println("[Command] JSON tidak valid");
      flashLED(LED_ERROR, 150);
    }
  } else if (code == 204) {
    Serial.println("[Command] Tidak ada perintah (204)");
  } else {
    Serial.printf("[Command] Error HTTP %d\n", code);
    flashLED(LED_ERROR, 150);
  }
  http.end();
  return false;
}

void sendResponse(String status, String message) {
  if (currentCommandId == -1 || !pendingCommand) {
    Serial.println("[Response] Tidak ada command_id");
    return;
  }

  HTTPClient http;
  String url = apiBase + "/iot/response";
  http.begin(url);
  http.addHeader("Content-Type", "application/json");

  StaticJsonDocument<256> doc;
  doc["command_id"] = currentCommandId;
  doc["status"] = status;
  doc["response_message"] = message;

  String json;
  serializeJson(doc, json);

  int code = http.POST(json);
  Serial.printf("[Response] POST %s -> %d\n", json.c_str(), code);
  if (code == 200 || code == 201) {
    flashLED(LED_RESPONSE);   // Cyan berkedip
    Serial.println("[Response] Terkirim");
  } else {
    flashLED(LED_ERROR, 200); // Merah jika gagal
    Serial.printf("[Response] Gagal, kode: %d\n", code);
  }
  http.end();

  // Reset
  currentCommandId = -1;
  pendingCommand = false;
}

// ======================== SETUP ========================
void setup() {
  Serial.begin(115200);
  
  // Inisialisasi pin output
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);
  
  pinMode(LED_WIFI, OUTPUT);
  pinMode(LED_SENSOR, OUTPUT);
  pinMode(LED_COMMAND, OUTPUT);
  pinMode(LED_RESPONSE, OUTPUT);
  pinMode(LED_ERROR, OUTPUT);
  pinMode(LED_HEARTBEAT, OUTPUT);
  
  // Matikan semua LED eksternal, built-in menyala (active low) sebagai tanda start
  digitalWrite(LED_WIFI, LOW);
  digitalWrite(LED_SENSOR, LOW);
  digitalWrite(LED_COMMAND, LOW);
  digitalWrite(LED_RESPONSE, LOW);
  digitalWrite(LED_ERROR, LOW);
  digitalWrite(LED_HEARTBEAT, LOW); // built-in ON (kebalikan)

  // WiFi
  WiFi.begin(ssid, password);
  Serial.print("Menghubungkan ke WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    // LED WiFi berkedip saat connecting
    digitalWrite(LED_WIFI, !digitalRead(LED_WIFI));
  }
  digitalWrite(LED_WIFI, HIGH); // LED WiFi menyala stabil
  Serial.println("\n[WiFi] Terhubung, IP: " + WiFi.localIP().toString());

  // Sinkronisasi waktu NTP
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  Serial.print("Sinkronisasi waktu");
  while (time(nullptr) < 100000) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(" OK");

  // Kalibrasi MQ135
  Serial.println("Kalibrasi MQ135 di udara bersih...");
  digitalWrite(LED_SENSOR, HIGH);  // indikator kalibrasi
  R0 = calibrateMQ135(50, 20);
  digitalWrite(LED_SENSOR, LOW);
  Serial.printf("R0 = %.2f Ohm\n", R0);

  // Inisialisasi timer
  lastSensorSend = millis();
  lastCommandFetch = millis();
  lastHeartbeat = millis();
}

// ======================== LOOP UTAMA ========================
void loop() {
  unsigned long now = millis();

  // Heartbeat LED (built-in) berkedip tiap 1 detik
  if (now - lastHeartbeat >= HEARTBEAT_INTERVAL) {
    lastHeartbeat = now;
    heartbeatState = !heartbeatState;
    digitalWrite(LED_HEARTBEAT, heartbeatState ? LOW : HIGH); // active low
  }

  // 1. Setiap 5 detik minta perintah
  if (now - lastCommandFetch >= COMMAND_INTERVAL) {
    lastCommandFetch = now;
    fetchCommand();
  }

  // 2. Setiap 30 detik kirim data sensor
  if (now - lastSensorSend >= SENSOR_INTERVAL) {
    lastSensorSend = now;
    float ratio = readRsRoRatio();
    float ppm = ammoniaPPM(ratio);
    Serial.printf("[Sensor] Rs/R0=%.2f, PPM=%.3f\n", ratio, ppm);
    sendSensorData(ppm);
  }

  // 3. Jika WiFi putus, matikan LED WiFi
  if (WiFi.status() != WL_CONNECTED) {
    digitalWrite(LED_WIFI, LOW);
  } else {
    digitalWrite(LED_WIFI, HIGH);
  }

  delay(10);
}