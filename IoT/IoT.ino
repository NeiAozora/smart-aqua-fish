/*
  IoT Device dengan ESP32 + MQ-135 + 2 Relay (Valve & Pompa)
  - Kirim data sensor (ppm amonia) tiap 30 detik
  - Minta perintah dari server tiap 5 detik
  - Eksekusi perintah: menyalakan/mematikan valve atau pompa
  - Kirim response (OK/ERROR) ke server

  Pin mapping (sesuaikan dengan wiring Anda):
    - MQ-135 Analog Out  -> GPIO34 (ADC1_CH6)
    - Relay Valve        -> GPIO26
    - Relay Pump         -> GPIO27

  Perintah yang dikenali dari server:
    - "valve_on"   : menyalakan valve solenoid
    - "valve_off"  : mematikan valve
    - "pump_on"    : menyalakan pompa air
    - "pump_off"   : mematikan pompa
    - "kuras"      : contoh alias untuk valve_on + pump_on (sesuai kebutuhan)
    - "mati"       : mematikan semua (valve & pump)

  Anda dapat menambah perintah lain sesuai kebutuhan.
*/

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>

// ========== KONFIGURASI WiFi ==========
const char* WIFI_SSID     = "YourWiFiSSID";
const char* WIFI_PASSWORD = "YourWiFiPassword";

// ========== KONFIGURASI SERVER ==========
String deviceId = "DEVICE_001";
String baseUrl  = "http://192.168.1.100:8000/api";   // Ganti dengan IP server

// ========== KONFIGURASI NTP (Waktu) ==========
const char* NTP_SERVER   = "pool.ntp.org";
const long  GMT_OFFSET   = 28800;   // WIB (UTC+7)
const int   DAYLIGHT_OFFSET = 0;

// ========== KONFIGURASI PIN ==========
const int PIN_MQ135_ANALOG = 34;    // ADC input
const int PIN_RELAY_VALVE  = 26;    // kontrol valve solenoid
const int PIN_RELAY_PUMP   = 27;    // kontrol pompa air

// ========== PARAMETER MQ-135 (kalibrasi sederhana) ==========
// Nilai R0 (hambatan sensor di udara bersih) didapat dari kalibrasi awal
float R0 = 1000.0;  // akan dihitung otomatis saat setup (jika perlu)
const float RL = 10000.0;   // Resistor load 10k ohm
const float a = 116.602;    // konstanta untuk amonia (dari datasheet)
const float b = -2.769;     // konstanta untuk amonia

// ========== INTERVAL (ms) ==========
const unsigned long INTERVAL_SENSOR_MS   = 30000;   // 30 detik
const unsigned long INTERVAL_COMMAND_MS  = 5000;    // 5 detik

// ========== VARIABEL GLOBAL ==========
float sensorAmonia = 0.0;
unsigned long lastSensorTime = 0;
unsigned long lastCommandTime = 0;

String lastCommandId = "";
String lastCommandText = "";

// Status relay
bool valveState = false;   // false = OFF, true = ON
bool pumpState  = false;

// ========== FUNGSI WAKTU ==========
void initTime() {
  configTime(GMT_OFFSET, DAYLIGHT_OFFSET, NTP_SERVER);
  Serial.print("Menunggu sinkronisasi waktu NTP...");
  time_t now = time(nullptr);
  while (now < 100000) {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
  }
  Serial.println(" selesai!");
}

String getCurrentISO8601() {
  time_t now = time(nullptr);
  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);
  char buffer[30];
  strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
  return String(buffer);
}

// ========== KONEKSI WiFi ==========
void connectWiFi() {
  Serial.print("Menghubungkan ke WiFi");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi terhubung! IP: " + WiFi.localIP().toString());
}

// ========== FUNGSI MQ-135 ==========
// Membaca rasio Rs/R0 dan menghitung ppm amonia
float readAmmoniaPPM() {
  int raw = analogRead(PIN_MQ135_ANALOG);
  float voltage = raw / 4095.0 * 3.3;
  float Rs = (3.3 - voltage) / voltage * RL;
  float ratio = Rs / R0;
  // Rumus dari datasheet: ppm = a * (ratio)^b
  float ppm = a * pow(ratio, b);
  // Batasi rentang
  if (ppm < 0) ppm = 0;
  if (ppm > 500) ppm = 500;
  return ppm;
}

// Kalibrasi R0 di udara bersih (asumsikan 0 ppm amonia)
void calibrateMQ135() {
  Serial.println("Kalibrasi MQ-135 di udara bersih...");
  float sumRatio = 0;
  const int samples = 50;
  for (int i = 0; i < samples; i++) {
    int raw = analogRead(PIN_MQ135_ANALOG);
    float voltage = raw / 4095.0 * 3.3;
    float Rs = (3.3 - voltage) / voltage * RL;
    // Di udara bersih, asumsikan konsentrasi amonia 0 ppm -> Rs/R0 = 3.6 (nilai tipikal dari datasheet)
    // Atau bisa dengan membaca nilai Rs lalu set R0 = Rs / 3.6
    // Kita gunakan pendekatan rata-rata Rs lalu hitung R0 agar ratio = 3.6
    sumRatio += Rs;
    delay(100);
  }
  float avgRs = sumRatio / samples;
  // Nilai Rs/R0 untuk udara bersih (amonia 0 ppm) biasanya sekitar 3.6
  R0 = avgRs / 3.6;
  Serial.printf("Kalibrasi selesai. R0 = %.2f ohm\n", R0);
}

// ========== KENDALI RELAY ==========
void setValve(bool on) {
  digitalWrite(PIN_RELAY_VALVE, on ? HIGH : LOW);
  valveState = on;
  Serial.printf("[VALVE] %s\n", on ? "ON" : "OFF");
}

void setPump(bool on) {
  digitalWrite(PIN_RELAY_PUMP, on ? HIGH : LOW);
  pumpState = on;
  Serial.printf("[PUMP] %s\n", on ? "ON" : "OFF");
}

void allOff() {
  setValve(false);
  setPump(false);
}

// ========== HTTP REQUEST GENERIK ==========
int sendHttpRequest(const String& method, const String& url, const String& jsonPayload, String& responseBody) {
  HTTPClient http;
  http.begin(url);
  http.setTimeout(5000);
  http.addHeader("Content-Type", "application/json");

  int httpCode;
  if (method == "POST") {
    httpCode = http.POST(jsonPayload);
  } else if (method == "GET") {
    httpCode = http.GET();
  } else {
    http.end();
    return -1;
  }

  if (httpCode > 0) {
    responseBody = http.getString();
  } else {
    responseBody = "";
  }
  http.end();
  return httpCode;
}

// ========== KIRIM DATA SENSOR ==========
void sendSensorData() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[SENSOR] WiFi tidak terhubung, lewati.");
    return;
  }

  // Baca nilai amonia terkini
  sensorAmonia = readAmmoniaPPM();
  String recordedAt = getCurrentISO8601();

  StaticJsonDocument<200> doc;
  doc["device_id"] = deviceId;
  doc["kadar_amonia"] = sensorAmonia;
  doc["recorded_at"] = recordedAt;

  String payload;
  serializeJson(doc, payload);

  Serial.println("[SENSOR] Mengirim data:");
  Serial.println(payload);

  String response;
  int httpCode = sendHttpRequest("POST", baseUrl + "/iot/sensor", payload, response);

  if (httpCode == 200 || httpCode == 201) {
    Serial.printf("[SENSOR] Berhasil, HTTP %d\n", httpCode);
  } else {
    Serial.printf("[SENSOR] Gagal, HTTP %d - %s\n", httpCode, response.c_str());
  }
}

// ========== EKSEKUSI PERINTAH FISIK ==========
bool executePhysicalCommand(String perintah) {
  Serial.printf("[EKSEKUSI] Menjalankan perintah: %s\n", perintah.c_str());

  if (perintah == "valve_on") {
    setValve(true);
    return true;
  }
  else if (perintah == "valve_off") {
    setValve(false);
    return true;
  }
  else if (perintah == "pump_on") {
    setPump(true);
    return true;
  }
  else if (perintah == "pump_off") {
    setPump(false);
    return true;
  }
  else if (perintah == "kuras") {
    // Contoh: nyalakan valve dan pump bersamaan
    setValve(true);
    setPump(true);
    return true;
  }
  else if (perintah == "mati") {
    allOff();
    return true;
  }
  else {
    Serial.println("[EKSEKUSI] Perintah tidak dikenal");
    return false;
  }
}

// ========== AMBIL PERINTAH DARI SERVER ==========
bool fetchCommand() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[COMMAND] WiFi tidak terhubung");
    return false;
  }

  String url = baseUrl + "/iot/command/" + deviceId;
  Serial.println("[COMMAND] GET " + url);

  String response;
  int httpCode = sendHttpRequest("GET", url, "", response);

  if (httpCode == 204) {
    Serial.println("[COMMAND] Tidak ada perintah (204)");
    return false;
  }

  if (httpCode != 200) {
    Serial.printf("[COMMAND] Gagal, HTTP %d - %s\n", httpCode, response.c_str());
    return false;
  }

  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, response);
  if (error) {
    Serial.println("[COMMAND] Gagal parse JSON");
    return false;
  }

  if (doc.containsKey("command_id") && doc.containsKey("perintah")) {
    lastCommandId = doc["command_id"].as<String>();
    lastCommandText = doc["perintah"].as<String>();
    Serial.printf("[COMMAND] Perintah diterima: ID=%s, perintah=%s\n",
                  lastCommandId.c_str(), lastCommandText.c_str());

    // Eksekusi perintah fisik
    bool success = executePhysicalCommand(lastCommandText);
    String status = success ? "OK" : "ERROR";
    String message = success ? "Perintah berhasil dieksekusi" : "Perintah gagal atau tidak dikenal";
    sendResponse(status, message);
    return true;
  } else {
    Serial.println("[COMMAND] Respons tidak mengandung command_id/perintah");
    return false;
  }
}

// ========== KIRIM RESPONSE KE SERVER ==========
void sendResponse(String status, String message) {
  if (lastCommandId.isEmpty()) {
    Serial.println("[RESPONSE] Tidak ada perintah yang sedang diproses, abaikan.");
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[RESPONSE] WiFi tidak terhubung");
    return;
  }

  String url = baseUrl + "/iot/response";
  StaticJsonDocument<200> doc;
  doc["command_id"] = lastCommandId;
  doc["status"] = status;
  doc["response_message"] = message;

  String payload;
  serializeJson(doc, payload);

  Serial.println("[RESPONSE] Mengirim response:");
  Serial.println(payload);

  String response;
  int httpCode = sendHttpRequest("POST", url, payload, response);

  if (httpCode == 200 || httpCode == 201) {
    Serial.printf("[RESPONSE] Berhasil terkirim (HTTP %d)\n", httpCode);
    // Reset perintah yang sudah direspon
    lastCommandId = "";
    lastCommandText = "";
  } else {
    Serial.printf("[RESPONSE] Gagal, HTTP %d - %s\n", httpCode, response.c_str());
  }
}

// ========== SERIAL COMMAND HANDLER (Manual) ==========
void handleSerialCommands() {
  if (!Serial.available()) return;

  String input = Serial.readStringUntil('\n');
  input.trim();
  if (input.length() == 0) return;

  if (input.startsWith("device=")) {
    deviceId = input.substring(7);
    deviceId.trim();
    Serial.println("Device ID diubah menjadi: " + deviceId);
    lastCommandId = "";
  }
  else if (input.startsWith("url=")) {
    baseUrl = input.substring(4);
    baseUrl.trim();
    Serial.println("Base URL diubah menjadi: " + baseUrl);
  }
  else if (input == "valve_on") setValve(true);
  else if (input == "valve_off") setValve(false);
  else if (input == "pump_on") setPump(true);
  else if (input == "pump_off") setPump(false);
  else if (input == "all_off") allOff();
  else if (input == "send") sendSensorData();
  else if (input == "fetch") fetchCommand();
  else if (input == "status") {
    Serial.println("=== STATUS ===");
    Serial.println("Device ID : " + deviceId);
    Serial.println("Base URL  : " + baseUrl);
    Serial.printf("Sensor    : %.2f ppm\n", sensorAmonia);
    Serial.printf("Valve     : %s\n", valveState ? "ON" : "OFF");
    Serial.printf("Pump      : %s\n", pumpState ? "ON" : "OFF");
    Serial.print("Perintah pending: ");
    if (lastCommandId.isEmpty()) Serial.println("(tidak ada)");
    else Serial.printf("ID=%s, perintah=%s\n", lastCommandId.c_str(), lastCommandText.c_str());
    Serial.println("==============");
  }
  else {
    Serial.println("Perintah serial:");
    Serial.println("  device=XXX / url=http://...");
    Serial.println("  valve_on / valve_off / pump_on / pump_off / all_off");
    Serial.println("  send / fetch / status");
  }
}

// ========== SETUP ==========
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\nIoT ESP32 + MQ-135 + Relay dimulai...");

  // Inisialisasi pin relay (asumsikan relay aktif HIGH)
  pinMode(PIN_RELAY_VALVE, OUTPUT);
  pinMode(PIN_RELAY_PUMP, OUTPUT);
  setValve(false);
  setPump(false);

  // Inisialisasi WiFi dan waktu
  connectWiFi();
  initTime();

  // Kalibrasi MQ-135 (pastikan sensor sudah dipanaskan minimal 2 menit sebelumnya)
  Serial.println("Panaskan MQ-135 selama 2 menit...");
  for (int i = 120; i > 0; i--) {
    delay(1000);
    if (i % 10 == 0) Serial.print(i / 10);
    Serial.print(".");
  }
  Serial.println();
  calibrateMQ135();

  Serial.println("Siap. Kirim perintah via Serial Monitor (115200 baud)");
  Serial.println("Ketik 'status' untuk melihat konfigurasi.");

  lastSensorTime = millis();
  lastCommandTime = millis();
}

// ========== LOOP ==========
void loop() {
  handleSerialCommands();

  // Reconnect WiFi jika putus
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi terputus, mencoba menyambung...");
    connectWiFi();
  }

  unsigned long now = millis();

  // Kirim data sensor tiap 30 detik
  if (now - lastSensorTime >= INTERVAL_SENSOR_MS) {
    lastSensorTime = now;
    sendSensorData();
  }

  // Ambil perintah tiap 5 detik
  if (now - lastCommandTime >= INTERVAL_COMMAND_MS) {
    lastCommandTime = now;
    fetchCommand();
  }

  delay(100);
}