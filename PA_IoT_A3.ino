#include <AntaresESPMQTT.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <Wire.h>
#include <BH1750.h>
#include <ESP32Servo.h>
#include <UniversalTelegramBot.h>
#define SSID "Tenda_CE6988"
#define PASSWORD "1234567890"
#define BOT_TOKEN "8225155956:AAGmuizMKiudC_eOAhsjTSIAoc-S21isdnU"
#define CHAT_ID "-5029087510"
const String anggota[] = {
  "6103206656",
  "5750987705",
  "1237536930"
};

#define ACCESSKEY "2ca8a31719edcb10:a1dcb43162553ca5"
#define projectName "Docs"
#define deviceName "greenhouse_cmd"

AntaresESPMQTT antares(ACCESSKEY);
WiFiClientSecure clientTelegram;
UniversalTelegramBot bot(BOT_TOKEN, clientTelegram);
BH1750 lightMeter;
Servo myservo;

#define PIN_LED 2
#define PIN_RELAY 15
#define PIN_SERVO 23
#define PIN_MOISTURE 35
#define I2C_SDA 21
#define I2C_SCL 22
bool pompaOtomatis = false;
bool ledOtomatis = false;
bool modeOtomatis = true;
String lastMode = "";
int kelembapanSekarang = 0;
int luxSekarang = 0;
int soilRaw = 0;
unsigned long lastMQTT = 0;
unsigned long lastTelegram = 0;
unsigned long lastSensor = 0;
void callback(char* topic, byte* payload, unsigned int length) {
  String message = "";
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  Serial.println("===== DATA MASUK =====");
  Serial.println(message);
  StaticJsonDocument<2048> doc;
  DeserializationError error =
    deserializeJson(doc, message);
  if (error) {
    Serial.print("JSON gagal: ");
    Serial.println(error.f_str());
    return;
  }
  String con =
    doc["m2m:rsp"]["pc"]["m2m:cin"]["con"];
  Serial.println("ISI CON:");
  Serial.println(con);
  StaticJsonDocument<256> data;
  error = deserializeJson(data, con);
  if (error) {
    Serial.print("JSON con gagal: ");
    Serial.println(error.f_str());
    return;
  }
 if (data.containsKey("mode")) {
    String mode = data["mode"];
    if (mode == "MANUAL") modeOtomatis = false;
    else if (mode == "OTOMATIS") modeOtomatis = true;
    antares.add("mode", modeOtomatis ? "OTOMATIS" : "MANUAL");
    Serial.println("Mode berubah dan langsung dilaporkan: " + mode);
  }
  if (!modeOtomatis) {
    if (data.containsKey("led")) {
      String led = data["led"];
      digitalWrite(PIN_LED, (led == "ON") ? HIGH : LOW);
      Serial.println("LED: " + led);
    }
    if (data.containsKey("pompa")) {
      String pompa = data["pompa"];
      digitalWrite(PIN_RELAY, (pompa == "ON") ? HIGH : LOW);
      Serial.println("Pompa: " + pompa);
    }
    if (data.containsKey("servo")) {
      String servo = data["servo"];
      if (servo == "BUKA") myservo.write(90);
      else if (servo == "TUTUP") myservo.write(0);
      Serial.println("Atap: " + servo);
    }
  }
}
bool isAllowed(String id) {
  for (int i = 0; i < 3; i++) {
    if (id == anggota[i]) return true;
  }
  return false;
}

String getStatusMessage() {
  String statusPompa =
    digitalRead(PIN_RELAY) ? "ON ✅" : "OFF ❌";
  String statusLED =
    digitalRead(PIN_LED) ? "ON 💡" : "OFF 🌑";
  String statusServo =
  (myservo.read() > 45) ? "BUKA 👐" : "TUTUP 🔒";
String msg = "🌱 *GREENHOUSE STATUS*\n";
msg += "━━━━━━━━━━━━━━━\n";
msg += "💧 Kelembapan : " +
       String(kelembapanSekarang) + "%\n";
msg += "🧪 Soil Raw : " +
       String(soilRaw) + "\n";
msg += "☀️ Cahaya : " +
       String(luxSekarang) + " lux\n";
msg += "🚿 Pompa : " +
       statusPompa + "\n";
msg += "💡 LED : " +
       statusLED + "\n";
msg += "🪟 Atap : " +
       statusServo + "\n";
msg += "🤖 Mode : ";
msg += modeOtomatis ?
       "*OTOMATIS*" :
       "*MANUAL*";
msg += "\n━━━━━━━━━━━━━━━";
return msg;}
void handleTelegram() {
  yield();
  int n = bot.getUpdates(bot.last_message_received + 1);
  if(n > 0){
    for (int i = 0; i < n; i++) {
      String text = bot.messages[i].text;
      String from_id = bot.messages[i].from_id;
      String chat_id = bot.messages[i].chat_id;
      String from_name = bot.messages[i].from_name;
      if (!isAllowed(from_id)) {
        bot.sendMessage(
          chat_id,
          "⚠️ *AKSES DITOLAK*\nID Anda tidak terdaftar.",
          "Markdown"
        );
        continue;
      }
      if (text == "/start") {
        String welcome =
          "Halo " + from_name + "!\n\n";
        welcome +=
          "🌱 Greenhouse Controller\n\n";
        welcome +=
          "📊 /status\n";
        welcome +=
          "🤖 /auto\n";
        welcome +=
          "✋ /manual\n";
        welcome +=
          "👥 /anggota";
        bot.sendMessage(chat_id, welcome, "");
      }
      else if (text == "/status") {
        bot.sendMessage(
          chat_id,
          getStatusMessage(),
          "Markdown"
        );
      }
      else if (text == "/auto") {
        modeOtomatis = true;
        updateLogikaOtomatis();
        bot.sendMessage(
          chat_id,
          "🤖 MODE OTOMATIS AKTIF",
          ""
        );
      }
      else if (text == "/manual") {
        modeOtomatis = false;
        String manualMsg =
          "✋ MODE MANUAL AKTIF\n\n";
        manualMsg +=
          "/pompa_on\n";
        manualMsg +=
          "/pompa_off\n";
        manualMsg +=
          "/led_on\n";
        manualMsg +=
          "/led_off\n";
        manualMsg +=
          "/buka\n";
        manualMsg +=
          "/tutup";
        bot.sendMessage(chat_id, manualMsg, "");
      }
      else if (text == "/anggota") {
        String list =
          "👥 *DAFTAR ANGGOTA*\n";
        for (int j = 0; j < 3; j++) {
          list +=
            String(j + 1) +
            ". `" +
            anggota[j] +
            "`\n";
        }
        bot.sendMessage(
          chat_id,
          list,
          "Markdown"
        );
      }
      else if (
        text == "/pompa_on" ||
        text == "/pompa_off" ||
        text == "/led_on" ||
        text == "/led_off" ||
        text == "/buka" ||
        text == "/tutup"
      ) {
        if (modeOtomatis) {
          bot.sendMessage(
            chat_id,
            "❌ Sistem masih mode OTOMATIS.\nKetik /manual dulu.",
            ""
          );
        }
        else {
          if (text == "/pompa_on")
            digitalWrite(PIN_RELAY, HIGH);
          else if (text == "/pompa_off")
            digitalWrite(PIN_RELAY, LOW);
          else if (text == "/led_on")
            digitalWrite(PIN_LED, HIGH);
          else if (text == "/led_off")
            digitalWrite(PIN_LED, LOW);
          else if (text == "/buka")
            myservo.write(90);
          else if (text == "/tutup")
            myservo.write(0);
          bot.sendMessage(
            chat_id,
            "✅ Perintah berhasil dijalankan",
            ""
          );
        }
      }
      else {
        bot.sendMessage(
          chat_id,
          "❓ Perintah tidak dikenali",
          ""
        );
      }
    }
    n = bot.getUpdates(bot.last_message_received + 1);
  }
}

void updateLogikaOtomatis() {
  if (kelembapanSekarang < 50) pompaOtomatis = true;
  else if (kelembapanSekarang > 80) pompaOtomatis = false;
  if (luxSekarang < 500) ledOtomatis = true;
  else if (luxSekarang > 2000) ledOtomatis = false;
  if (modeOtomatis) {
    digitalWrite(PIN_RELAY, pompaOtomatis);
    digitalWrite(PIN_LED, ledOtomatis);
    if (ledOtomatis) myservo.write(90);
    else myservo.write(0);
  }
}
void setup() {
  Serial.begin(115200);
  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_RELAY, OUTPUT);
  digitalWrite(PIN_LED, LOW);
  digitalWrite(PIN_RELAY, LOW);
  myservo.attach(PIN_SERVO);
  myservo.write(0);
  Wire.begin(I2C_SDA, I2C_SCL);
  lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE);
  WiFi.begin(SSID, PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected");
  clientTelegram.setInsecure();
  antares.setDebug(true);
  antares.wifiConnection(SSID, PASSWORD);
  antares.setMqttServer();
  antares.setCallback(callback);
  bot.sendMessage(CHAT_ID, "🌱 GREENHOUSE SYSTEM ONLINE", "");
}
void loop() {
  antares.checkMqttConnection();
  if (millis() - lastMQTT > 2000) {
    antares.retrieveLastData(projectName, "greenhouse_cmd");
    lastMQTT = millis();
  }
  if (millis() - lastTelegram > 3000) {
    handleTelegram();
    lastTelegram = millis();
  }
  if (millis() - lastSensor > 10000) {
    soilRaw = analogRead(PIN_MOISTURE);
    kelembapanSekarang = constrain(
      map(soilRaw, 3700, 1500, 0, 100),
      0,
      100
    );
    luxSekarang = round(lightMeter.readLightLevel());
    updateLogikaOtomatis();
    if (modeOtomatis) {
  if (kelembapanSekarang < 50)
    pompaOtomatis = true;
  else if (kelembapanSekarang > 80)
    pompaOtomatis = false;
  digitalWrite(PIN_RELAY, pompaOtomatis);
  if (luxSekarang < 500)
    ledOtomatis = true;
  else if (luxSekarang > 2000)
    ledOtomatis = false;
  digitalWrite(PIN_LED, ledOtomatis);
  if (ledOtomatis)
    myservo.write(90);
  else
    myservo.write(0);
}
    String statusSistem = "Aman";
    if (kelembapanSekarang < 50)
      statusSistem = "Bahaya";
    String statusAtap = (myservo.read() > 45) ? "BUKA" : "TUTUP";
    antares.add("kelembapan", kelembapanSekarang);
    antares.add("cahaya", luxSekarang);
    antares.add("status", statusSistem);
    antares.add("pompa",
    digitalRead(PIN_RELAY) ? "ON" : "OFF");
    antares.add("led",
    digitalRead(PIN_LED) ? "ON" : "OFF");
    antares.add("servo", statusAtap);
    antares.add("mode", modeOtomatis ? "OTOMATIS" : "MANUAL");
    Serial.println("PUBLISHING DATA...");
    antares.publish(projectName, "greenhouse_cmd");
    lastSensor = millis();
  }
}