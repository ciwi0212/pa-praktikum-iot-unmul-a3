// ============================================================
//  GREENHOUSE CONTROLLER - FINAL v3
//
//  ROOT CAUSE FIX:
//  Antares menyimpan data permanen. Saat ESP32 restart,
//  retrieveLastData() membaca perintah "MANUAL" dari sesi lama
//  → lastModeCmd = "" → "MANUAL" != "" → langsung eksekusi MANUAL
//
//  FIX v3:
//  1. lastModeCmd diinisialisasi "OTOMATIS" (bukan "")
//     → data lama dari Antares tidak dianggap perintah baru
//  2. Flag isFirstRetrieve: skip eksekusi retrieve pertama
//     → sistem stabil dulu sebelum menerima perintah Antares
//  3. publishStatusKeAntares() TIDAK ada di loop sensor
//  4. Guard adaPerubahan ketat (hanya true jika nilai berubah)
//
//  Arsitektur 3 Device:
//  - greenhouse_cmd    : ESP32 BACA saja  → input dari dashboard
//  - greenhouse_sensor : ESP32 TULIS saja → data sensor
//  - greenhouse_status : ESP32 TULIS saja → feedback status aktuator
// ============================================================

#include <AntaresESPMQTT.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <Wire.h>
#include <BH1750.h>
#include <ESP32Servo.h>
#include <UniversalTelegramBot.h>

// ============================================================
//  KONFIGURASI
// ============================================================
#define SSID      "Tenda_CE6988"
#define PASSWORD  "1234567890"

#define BOT_TOKEN "8225155956:AAGmuizMKiudC_eOAhsjTSIAoc-S21isdnU"
#define CHAT_ID   "-5029087510"

const String anggota[] = {
  "6103206656",
  "5750987705",
  "1237536930"
};

#define ACCESSKEY     "2ca8a31719edcb10:a1dcb43162553ca5"
#define projectName   "Docs"
#define deviceCommand "greenhouse_cmd"     // BACA SAJA  - input dari dashboard
#define deviceSensor  "greenhouse_sensor"  // TULIS SAJA - data sensor
#define deviceStatus  "greenhouse_status"  // TULIS SAJA - feedback status aktuator

// ============================================================
//  INTERVAL (ms)
// ============================================================
#define INTERVAL_MQTT      5000
#define INTERVAL_TELEGRAM  3000
#define INTERVAL_SENSOR    10000

// Delay sebelum retrieve pertama setelah boot (30 detik)
// Memberi waktu sistem stabil & menghindari baca data lama Antares
#define DELAY_FIRST_RETRIEVE 30000

// ============================================================
//  PIN
// ============================================================
#define PIN_LED      2
#define PIN_RELAY    15
#define PIN_SERVO    23
#define PIN_MOISTURE 35
#define I2C_SDA      21
#define I2C_SCL      22

// ============================================================
//  OBJEK
// ============================================================
AntaresESPMQTT antares(ACCESSKEY);
WiFiClientSecure clientTelegram;
UniversalTelegramBot bot(BOT_TOKEN, clientTelegram);
BH1750 lightMeter;
Servo myservo;

// ============================================================
//  STATE GLOBAL
// ============================================================
bool servoManual   = false;
bool pompaOtomatis = false;
bool ledOtomatis   = false;
bool modeOtomatis  = true;   // Default: OTOMATIS

// FIX v3: Cache diinisialisasi sesuai state default
// Sehingga data lama dari Antares tidak dianggap perintah baru
String lastModeCmd  = "OTOMATIS"; // ← bukan "" lagi
String lastPompaCmd = "OFF";
String lastLedCmd   = "OFF";
String lastServoCmd = "TUTUP";

// FIX v3: Flag untuk skip eksekusi retrieve pertama setelah boot
bool isFirstRetrieve = true;

int kelembapanSekarang = 0;
int luxSekarang        = 0;
int soilRaw            = 0;

unsigned long lastMQTT     = 0;
unsigned long lastTelegram = 0;
unsigned long lastSensor   = 0;

// ============================================================
//  FORWARD DECLARATIONS
// ============================================================
void updateLogikaOtomatis();
void gerakServoPelan(int targetPos);
void publishSensorKeAntares();
void publishStatusKeAntares();

// ============================================================
//  PUBLISH SENSOR → deviceSensor
//  Dipanggil: tiap INTERVAL_SENSOR di loop()
// ============================================================
void publishSensorKeAntares() {
  String statusTanaman = (kelembapanSekarang < 50) ? "Bahaya" : "Aman";

  antares.add("kelembapan",     kelembapanSekarang);
  antares.add("cahaya",         luxSekarang);
  antares.add("soil_raw",       soilRaw);
  antares.add("status_tanaman", statusTanaman);

  Serial.println("[PUBLISH] Sensor → deviceSensor");
  antares.publish(projectName, deviceSensor);
}

// ============================================================
//  PUBLISH STATUS AKTUATOR → deviceStatus
//  Dipanggil: HANYA saat ada perubahan state nyata
//  TIDAK pernah publish ke deviceCommand
// ============================================================
void publishStatusKeAntares() {
  String statusAtap = (myservo.read() > 45) ? "BUKA" : "TUTUP";

  antares.add("mode",  modeOtomatis           ? "OTOMATIS" : "MANUAL");
  antares.add("pompa", digitalRead(PIN_RELAY) ? "ON"       : "OFF");
  antares.add("led",   digitalRead(PIN_LED)   ? "ON"       : "OFF");
  antares.add("atap",  statusAtap);

  Serial.println("[PUBLISH] Status → deviceStatus");
  antares.publish(projectName, deviceStatus);
}

// ============================================================
//  ANTARES MQTT CALLBACK
// ============================================================
void callback(char* topic, byte* payload, unsigned int length) {

  // FIX v3: Skip eksekusi retrieve pertama
  // Data yang dibaca = data lama dari sesi sebelumnya, bukan perintah baru
  if (isFirstRetrieve) {
    Serial.println("[CALLBACK] Skip retrieve pertama (data lama Antares).");
    isFirstRetrieve = false;
    return;
  }

  String message = "";
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }

  Serial.println("===== DATA MASUK (Antares) =====");

  StaticJsonDocument<2048> doc;
  DeserializationError error = deserializeJson(doc, message);
  if (error) {
    Serial.print("JSON gagal: "); Serial.println(error.f_str());
    return;
  }

  const char* conRaw = doc["m2m:rsp"]["pc"]["m2m:cin"]["con"];
  if (!conRaw) {
    Serial.println("Field 'con' tidak ditemukan.");
    return;
  }

  String con = String(conRaw);
  Serial.println("ISI CON: " + con);

  StaticJsonDocument<256> data;
  error = deserializeJson(data, con);
  if (error) {
    Serial.print("JSON con gagal: "); Serial.println(error.f_str());
    return;
  }

  bool adaPerubahan = false;

  // ---- MODE ----
  if (data.containsKey("mode")) {
    String mode = data["mode"].as<String>();

    // Guard ketat: hanya proses jika nilai benar-benar berbeda
    if (mode != lastModeCmd) {
      lastModeCmd  = mode;
      adaPerubahan = true;

      if (mode == "OTOMATIS") {
        modeOtomatis = true;
        servoManual  = false;
        updateLogikaOtomatis();
        Serial.println("[MODE] → OTOMATIS");
      }
      else if (mode == "MANUAL") {
        modeOtomatis = false;
        Serial.println("[MODE] → MANUAL");
      }
    }
  }

  // ---- Kontrol aktuator (hanya saat MANUAL) ----
  if (!modeOtomatis) {

    if (data.containsKey("led")) {
      String led = data["led"].as<String>();
      if (led != lastLedCmd) {
        lastLedCmd   = led;
        adaPerubahan = true;
        digitalWrite(PIN_LED, (led == "ON") ? HIGH : LOW);
        Serial.println("[LED] → " + led);
      }
    }

    if (data.containsKey("pompa")) {
      String pompa = data["pompa"].as<String>();
      if (pompa != lastPompaCmd) {
        lastPompaCmd = pompa;
        adaPerubahan = true;
        digitalWrite(PIN_RELAY, (pompa == "ON") ? HIGH : LOW);
        Serial.println("[POMPA] → " + pompa);
      }
    }

    if (data.containsKey("servo")) {
      String servo = data["servo"].as<String>();
      if (servo != lastServoCmd) {
        lastServoCmd = servo;
        adaPerubahan = true;

        if (servo == "BUKA") {
          gerakServoPelan(90);
          servoManual = true;
          Serial.println("[SERVO] → BUKA");
        }
        else if (servo == "TUTUP") {
          gerakServoPelan(0);
          servoManual = true;
          Serial.println("[SERVO] → TUTUP");
        }
      }
    }
  }

  if (adaPerubahan) {
    publishStatusKeAntares();
  }
}

// ============================================================
//  CEK APAKAH USER TERDAFTAR
// ============================================================
bool isAllowed(String id) {
  for (int i = 0; i < 3; i++) {
    if (id == anggota[i]) return true;
  }
  return false;
}

// ============================================================
//  PESAN STATUS UNTUK TELEGRAM
// ============================================================
String getStatusMessage() {
  String statusPompa = digitalRead(PIN_RELAY) ? "ON ✅"   : "OFF ❌";
  String statusLED   = digitalRead(PIN_LED)   ? "ON 💡"   : "OFF 🌑";
  String statusServo = (myservo.read() > 45)  ? "BUKA 👐" : "TUTUP 🔒";

  String msg  = "🌱 *GREENHOUSE STATUS*\n";
  msg += "━━━━━━━━━━━━━━━\n";
  msg += "💧 Kelembapan : " + String(kelembapanSekarang) + "%\n";
  msg += "🧪 Soil Raw   : " + String(soilRaw)            + "\n";
  msg += "☀️ Cahaya      : " + String(luxSekarang)        + " lux\n";
  msg += "🚿 Pompa      : " + statusPompa                 + "\n";
  msg += "💡 LED        : " + statusLED                   + "\n";
  msg += "🪟 Atap       : " + statusServo                 + "\n";
  msg += "🤖 Mode       : ";
  msg += modeOtomatis ? "*OTOMATIS*" : "*MANUAL*";
  msg += "\n━━━━━━━━━━━━━━━";
  return msg;
}

// ============================================================
//  HANDLER TELEGRAM
// ============================================================
void handleTelegram() {
  yield();
  int n = bot.getUpdates(bot.last_message_received + 1);
  if (n == 0) return;

  for (int i = 0; i < n; i++) {
    String text      = bot.messages[i].text;
    String from_id   = bot.messages[i].from_id;
    String chat_id   = bot.messages[i].chat_id;
    String from_name = bot.messages[i].from_name;

    Serial.println("Telegram dari " + from_id + ": " + text);

    if (!isAllowed(from_id)) {
      bot.sendMessage(chat_id, "⚠️ *AKSES DITOLAK*\nID Anda tidak terdaftar.", "Markdown");
      continue;
    }

    if (text == "/start") {
      String welcome  = "Halo " + from_name + "!\n\n";
      welcome += "🌱 *Greenhouse Controller*\n\n";
      welcome += "📊 /status  — Lihat status sistem\n";
      welcome += "🤖 /auto    — Mode otomatis\n";
      welcome += "✋ /manual  — Mode manual\n";
      welcome += "👥 /anggota — Daftar anggota";
      bot.sendMessage(chat_id, welcome, "Markdown");
    }

    else if (text == "/status") {
      bot.sendMessage(chat_id, getStatusMessage(), "Markdown");
    }

    else if (text == "/auto") {
      modeOtomatis = true;
      lastModeCmd  = "OTOMATIS";
      servoManual  = false;
      updateLogikaOtomatis();
      publishStatusKeAntares();
      bot.sendMessage(chat_id, "🤖 MODE OTOMATIS AKTIF", "");
    }

    else if (text == "/manual") {
      modeOtomatis = false;
      lastModeCmd  = "MANUAL";
      publishStatusKeAntares();
      String manualMsg  = "✋ *MODE MANUAL AKTIF*\n\n";
      manualMsg += "/pompa\\_on  — Nyalakan pompa\n";
      manualMsg += "/pompa\\_off — Matikan pompa\n";
      manualMsg += "/led\\_on    — Nyalakan LED\n";
      manualMsg += "/led\\_off   — Matikan LED\n";
      manualMsg += "/buka       — Buka atap\n";
      manualMsg += "/tutup      — Tutup atap";
      bot.sendMessage(chat_id, manualMsg, "Markdown");
    }

    else if (text == "/anggota") {
      String list = "👥 *DAFTAR ANGGOTA*\n";
      for (int j = 0; j < 3; j++) {
        list += String(j + 1) + ". `" + anggota[j] + "`\n";
      }
      bot.sendMessage(chat_id, list, "Markdown");
    }

    else if (
      text == "/pompa_on"  || text == "/pompa_off" ||
      text == "/led_on"    || text == "/led_off"   ||
      text == "/buka"      || text == "/tutup"
    ) {
      if (modeOtomatis) {
        bot.sendMessage(chat_id,
          "❌ Sistem masih mode *OTOMATIS*.\nKetik /manual dulu.", "Markdown");
      }
      else {
        if      (text == "/pompa_on")  { digitalWrite(PIN_RELAY, HIGH); lastPompaCmd = "ON";   }
        else if (text == "/pompa_off") { digitalWrite(PIN_RELAY, LOW);  lastPompaCmd = "OFF";  }
        else if (text == "/led_on")    { digitalWrite(PIN_LED,   HIGH); lastLedCmd   = "ON";   }
        else if (text == "/led_off")   { digitalWrite(PIN_LED,   LOW);  lastLedCmd   = "OFF";  }
        else if (text == "/buka")  { gerakServoPelan(90); servoManual = true; lastServoCmd = "BUKA";  }
        else if (text == "/tutup") { gerakServoPelan(0);  servoManual = true; lastServoCmd = "TUTUP"; }

        publishStatusKeAntares();
        bot.sendMessage(chat_id, "✅ Perintah berhasil dijalankan", "");
      }
    }

    else {
      bot.sendMessage(chat_id,
        "❓ Perintah tidak dikenali.\nKetik /start untuk bantuan.", "");
    }
  }
}

// ============================================================
//  GERAK SERVO PELAN
// ============================================================
void gerakServoPelan(int targetPos) {
  int posisiSekarang = myservo.read();
  if (posisiSekarang < targetPos) {
    for (int pos = posisiSekarang; pos <= targetPos; pos++) { myservo.write(pos); delay(20); }
  } else {
    for (int pos = posisiSekarang; pos >= targetPos; pos--) { myservo.write(pos); delay(20); }
  }
}

// ============================================================
//  LOGIKA OTOMATIS
// ============================================================
void updateLogikaOtomatis() {
  if      (kelembapanSekarang < 50) pompaOtomatis = true;
  else if (kelembapanSekarang > 80) pompaOtomatis = false;

  if      (luxSekarang < 500)  ledOtomatis = true;
  else if (luxSekarang > 2000) ledOtomatis = false;

  if (modeOtomatis) {
    digitalWrite(PIN_RELAY, pompaOtomatis ? HIGH : LOW);
    digitalWrite(PIN_LED,   ledOtomatis   ? HIGH : LOW);

    if (!servoManual) {
      if (ledOtomatis) gerakServoPelan(90);
      else             gerakServoPelan(0);
    }
  }
}

// ============================================================
//  SETUP
// ============================================================
void setup() {
  Serial.begin(115200);

  pinMode(PIN_LED,   OUTPUT);
  pinMode(PIN_RELAY, OUTPUT);
  digitalWrite(PIN_LED,   LOW);
  digitalWrite(PIN_RELAY, LOW);

  myservo.attach(PIN_SERVO);
  myservo.write(0);

  Wire.begin(I2C_SDA, I2C_SCL);
  lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE);

  WiFi.begin(SSID, PASSWORD);
  Serial.print("Connecting WiFi");
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println("\nWiFi Connected: " + WiFi.localIP().toString());

  clientTelegram.setInsecure();

  antares.setDebug(true);
  antares.wifiConnection(SSID, PASSWORD);
  antares.setMqttServer();
  antares.setCallback(callback);

  // Set lastMQTT agar retrieve pertama terjadi setelah DELAY_FIRST_RETRIEVE
  // Ini mencegah baca data lama Antares terlalu cepat saat boot
  lastMQTT = millis() - INTERVAL_MQTT + DELAY_FIRST_RETRIEVE;

  // Publish status awal OTOMATIS ke deviceStatus
  publishStatusKeAntares();

  bot.sendMessage(CHAT_ID,
    "🌱 GREENHOUSE SYSTEM ONLINE\n🤖 Mode: OTOMATIS (default)", "");
  Serial.println("Setup selesai. Mode default: OTOMATIS");
}

// ============================================================
//  LOOP
// ============================================================
void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi putus, reconnect...");
    WiFi.reconnect();
    delay(5000);
    return;
  }

  antares.checkMqttConnection();

  // Retrieve command dari Antares
  if (millis() - lastMQTT > INTERVAL_MQTT) {
    antares.retrieveLastData(projectName, deviceCommand);
    lastMQTT = millis();
  }

  // Cek pesan Telegram
  if (millis() - lastTelegram > INTERVAL_TELEGRAM) {
    handleTelegram();
    lastTelegram = millis();
  }

  // Baca sensor & publish ke deviceSensor saja
  if (millis() - lastSensor > INTERVAL_SENSOR) {
    soilRaw = analogRead(PIN_MOISTURE);
    kelembapanSekarang = constrain(map(soilRaw, 3700, 1500, 0, 100), 0, 100);
    luxSekarang = (int)round(lightMeter.readLightLevel());

    Serial.printf("[SENSOR] Kelembapan: %d%%, Lux: %d, SoilRaw: %d\n",
                  kelembapanSekarang, luxSekarang, soilRaw);

    updateLogikaOtomatis();
    publishSensorKeAntares();
    // publishStatusKeAntares() TIDAK dipanggil di sini

    lastSensor = millis();
  }
}
