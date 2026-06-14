/*
  IoT Simulator untuk ESP32
  - Kirim data sensor setiap INTERVAL_SENSOR_MS (default 30 detik)
  - Minta perintah setiap INTERVAL_COMMAND_MS (default 5 detik)
  - Eksekusi perintah dan kirim response

  Endpoint:
    POST /api/iot/sensor
    GET  /api/iot/command/{device_id}
    POST /api/iot/response

  Konfigurasi:
    - Device ID dan Base URL bisa diubah via Serial Monitor
    - Perintah Serial:
        device=XXXX       : ganti device ID
        url=http://...    : ganti base URL
        sensor=12.34      : ubah nilai sensor amonia
        send              : kirim sensor manual
        fetch             : ambil perintah manual
        ok                : kirim response OK untuk command terakhir
        error             : kirim response ERROR
        status            : tampilkan konfigurasi saat ini
*/

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>

// ========== KONFIGURASI AWAL (ubah sesuai jaringan Anda) ==========
const char* WIFI_SSID     = "YourWiFiSSID";
const char* WIFI_PASSWORD = "YourWiFiPassword";

// Konfigurasi NTP (untuk timestamp recorded_at)
const char* NTP_SERVER   = "pool.ntp.org";
const long  GMT_OFFSET   = 28800;   // WIB (UTC+7) dalam detik, sesuaikan
const int   DAYLIGHT_OFFSET = 0;

// Default device & API (bisa diubah via Serial)
String deviceId = "DEVICE_001";
String baseUrl  = "http://192.168.1.100:8000/api";   // Ganti dengan IP server Anda

// Interval (ms)
const unsigned long INTERVAL_SENSOR_MS   = 30000;   // 30 detik
const unsigned long INTERVAL_COMMAND_MS  = 5000;    // 5 detik

// ========== VARIABEL GLOBAL ==========
float sensorAmonia = 0.65;          // nilai awal, bisa diubah via Serial
unsigned long lastSensorTime = 0;
unsigned long lastCommandTime = 0;

String lastCommandId = "";          // ID perintah yang sedang diproses
String lastCommandText = "";

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

// ========== WIFI ==========
void connectWiFi() {
  Serial.print("Menghubungkan ke WiFi");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi terhubung! IP: " + WiFi.localIP().toString());
}

// ========== HTTP REQUEST GENERIK ==========
// Kirim request dan kembalikan response code, body disimpan ke responseBody (opsional)
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

  String url = baseUrl + "/iot/sensor";
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
  int httpCode = sendHttpRequest("POST", url, payload, response);

  if (httpCode == 200 || httpCode == 201) {
    Serial.printf("[SENSOR] Berhasil, HTTP %d\n", httpCode);
  } else {
    Serial.printf("[SENSOR] Gagal, HTTP %d - %s\n", httpCode, response.c_str());
  }
}

// ========== AMBIL PERINTAH ==========
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
    Serial.println("[COMMAND] Tidak ada perintah menunggu (204)");
    return false;
  }

  if (httpCode != 200) {
    Serial.printf("[COMMAND] Gagal, HTTP %d - %s\n", httpCode, response.c_str());
    return false;
  }

  // Parse JSON response
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
    // Eksekusi simulasi (bisa diimplementasikan sesuai kebutuhan)
    executeSimulatedCommand(lastCommandText);
    return true;
  } else {
    Serial.println("[COMMAND] Respons tidak mengandung command_id/perintah");
    return false;
  }
}

// ========== SIMULASI EKSEKUSI PERINTAH ==========
void executeSimulatedCommand(String perintah) {
  Serial.printf("[EKSEKUSI] Menjalankan perintah: %s\n", perintah.c_str());
  // Di sini Anda bisa memicu aktuator (relay, motor, dll)
  // Contoh: if (perintah == "kuras") digitalWrite(PIN_POMPA, HIGH);
  // Untuk simulasi, kita hanya log dan otomatis response OK (atau bisa error)
  // Terserah logika: jika perintah "mati", response ERROR dll.
  if (perintah == "mati") {
    Serial.println("[EKSEKUSI] Perintah 'mati' -> akan response ERROR");
    sendResponse("ERROR", "Perintah gagal, device error");
  } else {
    // Default response OK setelah simulasi berhasil
    sendResponse("OK", "Perintah berhasil dieksekusi: " + perintah);
  }
}

// ========== KIRIM RESPONSE ==========
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
    // Clear command yang sudah direspon
    lastCommandId = "";
    lastCommandText = "";
  } else {
    Serial.printf("[RESPONSE] Gagal, HTTP %d - %s\n", httpCode, response.c_str());
  }
}

// ========== SERIAL COMMAND HANDLER ==========
void handleSerialCommands() {
  if (!Serial.available()) return;

  String input = Serial.readStringUntil('\n');
  input.trim();
  if (input.length() == 0) return;

  if (input.startsWith("device=")) {
    deviceId = input.substring(7);
    deviceId.trim();
    Serial.println("Device ID diubah menjadi: " + deviceId);
    lastCommandId = "";  // reset perintah lama karena device beda
  }
  else if (input.startsWith("url=")) {
    baseUrl = input.substring(4);
    baseUrl.trim();
    Serial.println("Base URL diubah menjadi: " + baseUrl);
  }
  else if (input.startsWith("sensor=")) {
    float val = input.substring(7).toFloat();
    if (val >= 0) {
      sensorAmonia = val;
      Serial.printf("Nilai sensor amonia diubah menjadi: %.2f ppm\n", sensorAmonia);
    } else {
      Serial.println("Format salah. Gunakan: sensor=0.5");
    }
  }
  else if (input == "send") {
    Serial.println("Manual: mengirim data sensor...");
    sendSensorData();
  }
  else if (input == "fetch") {
    Serial.println("Manual: mengambil perintah...");
    fetchCommand();
  }
  else if (input == "ok") {
    Serial.println("Manual: kirim response OK untuk perintah terakhir");
    sendResponse("OK", "Response OK via Serial");
  }
  else if (input == "error") {
    Serial.println("Manual: kirim response ERROR");
    sendResponse("ERROR", "Response ERROR via Serial");
  }
  else if (input == "status") {
    Serial.println("=== Konfigurasi Saat Ini ===");
    Serial.println("Device ID : " + deviceId);
    Serial.println("Base URL  : " + baseUrl);
    Serial.printf("Sensor    : %.2f ppm\n", sensorAmonia);
    Serial.print("Perintah pending : ");
    if (lastCommandId.isEmpty()) Serial.println("(tidak ada)");
    else Serial.printf("ID=%s, perintah=%s\n", lastCommandId.c_str(), lastCommandText.c_str());
    Serial.println("============================");
  }
  else {
    Serial.println("Perintah tidak dikenal. Gunakan:");
    Serial.println("  device=XXX    - ganti device ID");
    Serial.println("  url=http://... - ganti base URL");
    Serial.println("  sensor=12.34  - ubah nilai sensor");
    Serial.println("  send          - kirim sensor sekarang");
    Serial.println("  fetch         - ambil perintah sekarang");
    Serial.println("  ok / error    - kirim response");
    Serial.println("  status        - lihat konfigurasi");
  }
}

// ========== SETUP ==========
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n\nIoT Simulator ESP32 mulai...");

  connectWiFi();
  initTime();

  Serial.println("Gunakan Serial Monitor untuk mengubah konfigurasi (115200 baud)");
  Serial.println("Ketik 'status' untuk melihat pengaturan saat ini.");

  // Inisialisasi timer
  lastSensorTime = millis();
  lastCommandTime = millis();
}

// ========== LOOP ==========
void loop() {
  handleSerialCommands();

  // Cek koneksi WiFi, reconnect jika perlu
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi terputus, mencoba menyambung...");
    connectWiFi();
  }

  unsigned long now = millis();

  // Kirim sensor setiap INTERVAL_SENSOR_MS
  if (now - lastSensorTime >= INTERVAL_SENSOR_MS) {
    lastSensorTime = now;
    sendSensorData();
  }

  // Ambil perintah setiap INTERVAL_COMMAND_MS
  if (now - lastCommandTime >= INTERVAL_COMMAND_MS) {
    lastCommandTime = now;
    fetchCommand();
  }

  // Loop kecil biar tidak memblokir
  delay(100);
}