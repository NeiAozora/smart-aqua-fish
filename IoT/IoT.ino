#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>

// ======================== KONFIGURASI ========================
const char* ssid = "W.E.L.Sw@";
const char* password = "RCPRTSeason3";
String apiBase = "http://ppl.neiaozora.my.id/api";
String deviceId = "DEVICE_001";

// ======================== PIN (GPIO) ========================
#define MQ135_DO_PIN  5     // D1
#define RELAY_PIN     4     // D2

#define LED_WIFI      14    // D5
#define LED_SENSOR    2     // D4 (built-in, active LOW)
#define LED_COMMAND   12    // D6
#define LED_RESPONSE  13    // D7
#define LED_ERROR     15    // D8 (pin sensitif boot)

// ======================== AMONIA (tipe paling awal) ========================
enum AmoniaLevel { AMAN, WASPADA, BERBAHAYA, KRITIS };
const float PPM_AMAN = 0.01, PPM_WASPADA = 0.1, PPM_BERBAHAYA = 0.35, PPM_KRITIS = 0.75;

// ======================== SERIAL DEBUG ========================
// Set DEBUG 0 untuk mematikan SEMUA log (hemat, untuk produksi).
#define DEBUG       1
#define DEBUG_BAUD  115200

void dbgStamp() {
  unsigned long ms = millis();
  Serial.printf("[%7lu.%03lu]", ms / 1000UL, ms % 1000UL);
}

void dbg(const char* tag, const char* fmt, ...) {
#if DEBUG
  dbgStamp();
  Serial.printf("[%-6s] ", tag);
  char buf[180];
  va_list args; va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  Serial.println(buf);
#else
  (void)tag; (void)fmt;
#endif
}

// ======================== KONFIG RELAY ========================
// true  = modul active-LOW  (mayoritas modul relay biru: LOW=nyala, HIGH=mati)
// false = modul active-HIGH (HIGH=nyala, LOW=mati)
// Cek: kalau relay nyala saat ESP baru hidup tanpa perintah -> active-LOW -> biarkan true.
#define RELAY_ACTIVE_LOW  true

bool relayState = false;   // true = pompa nyala (untuk log status)

void relayOn() {
  digitalWrite(RELAY_PIN, RELAY_ACTIVE_LOW ? LOW : HIGH);
  relayState = true;
  dbg("RELAY", ">>> POMPA ON  | pin D2 ditulis %s (mode %s)",
      RELAY_ACTIVE_LOW ? "LOW" : "HIGH",
      RELAY_ACTIVE_LOW ? "active-LOW" : "active-HIGH");
}

void relayOff() {
  digitalWrite(RELAY_PIN, RELAY_ACTIVE_LOW ? HIGH : LOW);
  relayState = false;
  dbg("RELAY", "<<< POMPA OFF | pin D2 ditulis %s (mode %s)",
      RELAY_ACTIVE_LOW ? "HIGH" : "LOW",
      RELAY_ACTIVE_LOW ? "active-LOW" : "active-HIGH");
}

// ======================== INTERVAL (ms) ========================
const unsigned long SENSOR_INTERVAL  = 5000;
const unsigned long COMMAND_INTERVAL = 3000;
const unsigned long PROSES_INTERVAL  = 1000;
const unsigned long STATUS_INTERVAL  = 5000;   // heartbeat status ke Serial
const unsigned long HTTP_TIMEOUT_MS  = 4000;
const unsigned long WIFI_RETRY_MS    = 10000;

// ======================== COOLDOWN / KURAS ========================
#define KURAS_DURATION_DETIK   3
#define COOLDOWN_EXTRA_DETIK   10

// ======================== VARIABEL ========================
unsigned long lastSensorSend = 0, lastCommandFetch = 0;
unsigned long lastWifiRetry = 0, lastStatusPrint = 0;
int currentCommandId = -1;
bool kurasActive = false;
unsigned long kurasStart = 0, lastProses = 0;
unsigned long cooldownUntil = 0;
AmoniaLevel currentLevel = AMAN, lastLevel = AMAN;
bool lastWifiUp = false;

// ======================== FUNGSI BANTU ========================
void flashLED(int pin, int ms = 100) {
  if (pin == LED_SENSOR) {
    digitalWrite(pin, LOW); delay(ms); digitalWrite(pin, HIGH);
  } else {
    digitalWrite(pin, HIGH); delay(ms); digitalWrite(pin, LOW);
  }
}

bool isGasAboveThreshold() { return digitalRead(MQ135_DO_PIN) == HIGH; }

AmoniaLevel determineLevel() {
  return isGasAboveThreshold() ? (random(0, 100) < 70 ? BERBAHAYA : KRITIS) : AMAN;
}

float getPPMByLevel(AmoniaLevel lv) {
  switch (lv) {
    case AMAN:      return PPM_AMAN + random(-5, 5) / 1000.0;
    case WASPADA:   return PPM_WASPADA + random(-50, 50) / 1000.0;
    case BERBAHAYA: return PPM_BERBAHAYA + random(-100, 100) / 1000.0;
    case KRITIS:    return PPM_KRITIS + random(-250, 250) / 1000.0;
    default:        return 0.01;
  }
}

String levelToString(AmoniaLevel lv) {
  switch (lv) {
    case AMAN:      return "AMAN";
    case WASPADA:   return "WASPADA";
    case BERBAHAYA: return "BERBAHAYA";
    case KRITIS:    return "KRITIS";
    default:        return "UNKNOWN";
  }
}

// ======================== HTTP ========================
void sendResponse(int cmdId, String status, String msg) {
  if (WiFi.status() != WL_CONNECTED) {
    dbg("HTTP", "SKIP response (WiFi down) status=%s", status.c_str());
    return;
  }
  WiFiClient w; HTTPClient h;
  h.begin(w, apiBase + "/iot/response");
  h.setTimeout(HTTP_TIMEOUT_MS);
  h.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  h.addHeader("User-Agent", "ESP8266");
  h.addHeader("Content-Type", "application/json");

  StaticJsonDocument<256> doc;
  doc["command_id"] = cmdId;
  doc["status"] = status;
  doc["response_message"] = msg;
  String json; serializeJson(doc, json);

  unsigned long t0 = millis();
  int code = h.POST(json);
  unsigned long dt = millis() - t0;

  if (code == 200 || code == 201) {
    dbg("HTTP", "POST /iot/response -> %d (%lums) | cmd=%d status=%s msg=\"%s\"",
        code, dt, cmdId, status.c_str(), msg.c_str());
    flashLED(LED_RESPONSE);
  } else {
    dbg("ERR", "POST /iot/response GAGAL -> %d (%lums) | cmd=%d status=%s",
        code, dt, cmdId, status.c_str());
    flashLED(LED_ERROR);
  }
  h.end();
}

void sendSensorData(float ppm, AmoniaLevel lv) {
  if (WiFi.status() != WL_CONNECTED) {
    dbg("SENSOR", "SKIP kirim (WiFi down) level=%s", levelToString(lv).c_str());
    return;
  }
  WiFiClient w; HTTPClient h;
  h.begin(w, apiBase + "/iot/sensor");
  h.setTimeout(HTTP_TIMEOUT_MS);
  h.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  h.addHeader("User-Agent", "ESP8266");
  h.addHeader("Content-Type", "application/json");

  StaticJsonDocument<300> doc;
  doc["device_id"] = deviceId;
  doc["kadar_amonia"] = ppm;
  doc["status"] = levelToString(lv);
  doc["threshold_ppm"] = 0.2;
  time_t now = time(nullptr);
  struct tm* t = gmtime(&now);
  char ts[30];
  sprintf(ts, "%04d-%02d-%02dT%02d:%02d:%02dZ",
          t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
          t->tm_hour, t->tm_min, t->tm_sec);
  doc["recorded_at"] = ts;
  String json; serializeJson(doc, json);

  unsigned long t0 = millis();
  int code = h.POST(json);
  unsigned long dt = millis() - t0;

  if (code == 200 || code == 201) {
    dbg("SENSOR", "POST /iot/sensor -> %d (%lums) | %s ppm=%.3f",
        code, dt, levelToString(lv).c_str(), ppm);
    flashLED(LED_SENSOR);
  } else {
    dbg("ERR", "POST /iot/sensor GAGAL -> %d (%lums) | %s ppm=%.3f",
        code, dt, levelToString(lv).c_str(), ppm);
    flashLED(LED_ERROR);
  }
  h.end();
}

void fetchCommand() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (kurasActive) { dbg("CMD", "skip polling (kuras sedang berjalan)"); return; }
  if (millis() < cooldownUntil) {
    dbg("CMD", "skip polling (cooldown sisa %lds)", (long)(cooldownUntil - millis()) / 1000);
    return;
  }

  WiFiClient w; HTTPClient h;
  h.begin(w, apiBase + "/iot/command/" + deviceId);
  h.setTimeout(HTTP_TIMEOUT_MS);
  h.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  h.addHeader("User-Agent", "ESP8266");

  unsigned long t0 = millis();
  int code = h.GET();
  unsigned long dt = millis() - t0;

  if (code == 200) {
    String payload = h.getString();
    dbg("CMD", "GET /iot/command -> 200 (%lums) | %s", dt, payload.c_str());
    StaticJsonDocument<256> doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (err) {
      dbg("ERR", "JSON parse gagal: %s", err.c_str());
    } else if (doc.containsKey("command_id") && doc.containsKey("perintah")) {
      int cmdId = doc["command_id"];
      String perintah = doc["perintah"].as<String>();
      flashLED(LED_COMMAND);
      dbg("CMD", "Perintah diterima: id=%d perintah=\"%s\"", cmdId, perintah.c_str());

      if (perintah == "kuras") {
        currentCommandId = cmdId;
        kurasActive = true;
        kurasStart = millis();
        lastProses = millis();
        dbg("KURAS", "MULAI | cmd=%d durasi=%ds", cmdId, KURAS_DURATION_DETIK);
        relayOn();
      } else {
        dbg("CMD", "Perintah tidak dikenal, dibalas OK");
        sendResponse(cmdId, "OK", "Cooldown:0");
      }
    } else {
      dbg("ERR", "Payload tidak punya command_id/perintah");
    }
  } else if (code == 204) {
    dbg("CMD", "GET /iot/command -> 204 (tidak ada perintah)");
  } else {
    dbg("ERR", "GET /iot/command -> %d (%lums)", code, dt);
    flashLED(LED_ERROR, 150);
  }
  h.end();
}

// ======================== WIFI ========================
void ensureWifi() {
  bool up = (WiFi.status() == WL_CONNECTED);
  if (up != lastWifiUp) {
    lastWifiUp = up;
    if (up) dbg("WIFI", "TERHUBUNG | IP=%s RSSI=%ddBm", WiFi.localIP().toString().c_str(), WiFi.RSSI());
    else    dbg("WIFI", "TERPUTUS!");
  }
  if (up) return;
  if (millis() - lastWifiRetry < WIFI_RETRY_MS) return;
  lastWifiRetry = millis();
  dbg("WIFI", "Mencoba reconnect...");
  WiFi.reconnect();
}

// ======================== STATUS HEARTBEAT ========================
void printStatus() {
  long rem = (long)(cooldownUntil - millis());
  if (rem < 0) rem = 0;
  dbg("STATUS",
      "up=%lus | wifi=%s rssi=%lddBm | level=%s | RELAY=%s | kuras=%s | cooldown=%lds",
      millis() / 1000UL,
      WiFi.status() == WL_CONNECTED ? "OK" : "DOWN",
      WiFi.status() == WL_CONNECTED ? (long)WiFi.RSSI() : 0L,
      levelToString(currentLevel).c_str(),
      relayState ? "ON " : "OFF",
      kurasActive ? "YA" : "tidak",
      rem / 1000);
}

// ======================== SETUP ========================
void setup() {
  Serial.begin(DEBUG_BAUD);
  delay(200);

  pinMode(MQ135_DO_PIN, INPUT);
  pinMode(RELAY_PIN, OUTPUT);
  relayOff();   // pastikan pompa mati sebelum apa pun (anti glitch boot)

  pinMode(LED_WIFI, OUTPUT); pinMode(LED_SENSOR, OUTPUT); pinMode(LED_COMMAND, OUTPUT);
  pinMode(LED_RESPONSE, OUTPUT); pinMode(LED_ERROR, OUTPUT);
  digitalWrite(LED_SENSOR, HIGH); // built-in LED mati

  // ----- Banner -----
  Serial.println();
  Serial.println(F("=================================================="));
  Serial.println(F("   SMART FISH FARM - MONITOR AMONIA (ESP8266)"));
  Serial.println(F("=================================================="));
  dbg("SYS", "Device ID  : %s", deviceId.c_str());
  dbg("SYS", "API Base   : %s", apiBase.c_str());
  dbg("SYS", "Relay mode : %s (pin D2)", RELAY_ACTIVE_LOW ? "ACTIVE-LOW" : "ACTIVE-HIGH");
  dbg("SYS", "Kuras      : %ds + cooldown %ds", KURAS_DURATION_DETIK, COOLDOWN_EXTRA_DETIK);
  dbg("SYS", "Debug      : %s", DEBUG ? "ON" : "OFF");
  Serial.println(F("=================================================="));

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  dbg("WIFI", "Menghubungkan ke SSID \"%s\" ...", ssid);
  while (WiFi.status() != WL_CONNECTED) { delay(500); digitalWrite(LED_WIFI, !digitalRead(LED_WIFI)); }
  digitalWrite(LED_WIFI, HIGH);
  lastWifiUp = true;
  dbg("WIFI", "TERHUBUNG | IP=%s RSSI=%ddBm", WiFi.localIP().toString().c_str(), WiFi.RSSI());

  dbg("SYS", "Sinkronisasi waktu (NTP)...");
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  while (time(nullptr) < 100000) delay(500);
  dbg("SYS", "Waktu tersinkron.");

  currentLevel = determineLevel();
  lastLevel = currentLevel;
  lastSensorSend = lastCommandFetch = millis();
  dbg("SYS", "Siap. Level awal: %s", levelToString(currentLevel).c_str());
}

// ======================== LOOP ========================
void loop() {
  unsigned long now = millis();

  ensureWifi();

  // ---------- State kuras ----------
  if (kurasActive) {
    unsigned long elapsed = (now - kurasStart) / 1000;

    // Matikan relay DULU sebelum POST apa pun (jamin pompa berhenti tepat waktu)
    if (elapsed >= KURAS_DURATION_DETIK) {
      relayOff();
      kurasActive = false;
      unsigned long cooldown = KURAS_DURATION_DETIK + COOLDOWN_EXTRA_DETIK;
      cooldownUntil = now + cooldown * 1000UL;
      dbg("KURAS", "SELESAI | berjalan %lus, cooldown %lus aktif", elapsed, cooldown);
      sendResponse(currentCommandId, "OK", "Cooldown:" + String(cooldown));
      currentCommandId = -1;
    }
    else if (now - lastProses >= PROSES_INTERVAL) {
      lastProses = now;
      dbg("KURAS", "proses... %lus berjalan", elapsed);
      sendResponse(currentCommandId, "proses", "Pompa berjalan " + String(elapsed) + " detik");
    }
  }

  // ---------- Level amonia ----------
  currentLevel = determineLevel();
  if (currentLevel != lastLevel) {
    dbg("SENSOR", "Level berubah: %s -> %s", levelToString(lastLevel).c_str(), levelToString(currentLevel).c_str());
    lastLevel = currentLevel;
    if (currentLevel == KRITIS) {
      for (int i = 0; i < 5; i++) { flashLED(LED_ERROR, 200); delay(100); }
    } else if (currentLevel == BERBAHAYA) {
      flashLED(LED_ERROR, 500); flashLED(LED_ERROR, 500);
    }
    sendSensorData(getPPMByLevel(currentLevel), currentLevel);
  }

  // ---------- Polling ----------
  if (now - lastCommandFetch >= COMMAND_INTERVAL) {
    lastCommandFetch = now;
    fetchCommand();
  }
  if (now - lastSensorSend >= SENSOR_INTERVAL) {
    lastSensorSend = now;
    sendSensorData(getPPMByLevel(currentLevel), currentLevel);
  }

  // ---------- Status heartbeat ke Serial ----------
  if (now - lastStatusPrint >= STATUS_INTERVAL) {
    lastStatusPrint = now;
    printStatus();
  }

  digitalWrite(LED_WIFI, WiFi.status() == WL_CONNECTED ? HIGH : LOW);
  delay(10);
}
