#include <ESP8266WiFi.h>
#include <WiFiManager.h>
#include <Wire.h>
#include <LiquidCrystal_PCF8574.h>
#include <DHT.h>
#include <Adafruit_BMP280.h>
#include <Firebase_ESP_Client.h>
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"

// ---------- Firebase ----------
#define API_KEY "isi dengan API KAMU"
#define DATABASE_URL "ISI DENGAN DATABASE KAMU"

// ---------- Pin Setup ----------
#define TRIG_PIN D6
#define ECHO_PIN D7
#define RAIN_PIN A0
#define DHTPIN D3
#define DHTTYPE DHT11
#define BUZZER_PIN D5

// ---------- Inisialisasi Library ----------
LiquidCrystal_PCF8574 lcd(0x27);
DHT dht(DHTPIN, DHTTYPE);
Adafruit_BMP280 bmp;
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// ---------- Variabel Global ----------
bool signupOK = false;
unsigned long lastSend = 0;
const unsigned long firebaseInterval = 3000;

float batas_aman = 50.0;
float batas_waspada = 85.0;
float batas_bahaya = 100.0;

String status_air_global = "AMAN";

// ---------- Fungsi Prediksi Cuaca ----------
String prediksiCuacaSederhana(float tekanan, float kelembapan) {
  if (kelembapan > 80 && tekanan < 1005) {
    return "HUJAN";
  } else if (kelembapan > 65 || tekanan < 1015) {
    return "BERAWAN";
  } else {
    return "CERAH";
  }
}
void setup() {
  Serial.begin(115200);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  Wire.begin(D2, D1);
  lcd.begin(16, 2);
  lcd.setBacklight(255);
  lcd.clear(); lcd.print("Setup WiFi...");
  dht.begin();
  if (!bmp.begin(0x76)) {
    lcd.clear(); lcd.print("BMP ERROR!");
    while (1);
  }
  WiFiManager wm;
  if (!wm.autoConnect("BanjirMonitorAP")) {
    lcd.clear(); lcd.print("WiFi Gagal");
    delay(3000); ESP.restart();
  }
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  config.token_status_callback = tokenStatusCallback;
  if (Firebase.signUp(&config, &auth, "", "")) {
    Serial.println("Firebase Sign-Up OK");
    signupOK = true;
  } else {
    Serial.printf("Firebase Sign-Up GAGAL: %s\n", fbdo.errorReason().c_str());
  }
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
  lcd.clear(); lcd.print("WiFi Terhubung");
}
void loop() {
  bacaSensor();
  kendaliBuzzer();
  delay(1000);
}
void kendaliBuzzer() {
  if (status_air_global == "BAHAYA") {
    digitalWrite(BUZZER_PIN, HIGH);
  } else if (status_air_global == "WASPADA") {
    static unsigned long lastToggle = 0;
    static bool state = false;
    if (millis() - lastToggle > (state ? 300 : 1000)) {
      state = !state;
      digitalWrite(BUZZER_PIN, state);
      lastToggle = millis();
    }
  } else {
    digitalWrite(BUZZER_PIN, LOW);
  }
}
void bacaSensor() {
  // Sensor Ultrasonik
  digitalWrite(TRIG_PIN, LOW); delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH); delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  long durasi = pulseIn(ECHO_PIN, HIGH, 10000);
  float jarak = durasi * 0.034 / 2.0;
  // Hitung tinggi air (dalam persen)
  float Rp = 30.0;      // Sensor ke dasar ember
  float Ru = 7.0;       // Sensor ke air saat penuh
  float Ra = Rp - Ru;   // Ketinggian maksimum = 23 cm
  float level = ((Rp - jarak) / Ra) * 100.0;
  float tinggi_air = constrain(level, 0, 100); // nilai akhir dalam %
  // Status Air
  String status_air;
  if (tinggi_air <= batas_aman) {
    status_air = "AMAN";
  } else if (tinggi_air <= batas_waspada) {
    status_air = "WASPADA";
  } else {
    status_air = "BAHAYA";
  }
  status_air_global = status_air;
  // Sensor Hujan
  int nilai_hujan = analogRead(RAIN_PIN);
  String status_hujan = (nilai_hujan < 680) ? "HUJAN" : "CERAH";
  // DHT11
  float suhu = dht.readTemperature();
  float kelembapan = dht.readHumidity();
  if (isnan(suhu)) suhu = 0;
  if (isnan(kelembapan)) kelembapan = 0;
  // BMP280
  float tekanan = bmp.readPressure() / 100.0F;
  // Prediksi Cuaca
  String prediksi_cuaca = prediksiCuacaSederhana(tekanan, kelembapan);

  // LCD
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(status_air + " " + String((int)tinggi_air) + "%");
  lcd.setCursor(0, 1);
  lcd.print(prediksi_cuaca + " " + String((int)suhu) + "C");
  // Firebase Upload
  if (Firebase.ready() && signupOK && millis() - lastSend > firebaseInterval) {
    lastSend = millis();
    Firebase.RTDB.setFloat(&fbdo, "/data/jarak", jarak);
    Firebase.RTDB.setFloat(&fbdo, "/data/tinggi_air", tinggi_air);
    Firebase.RTDB.setFloat(&fbdo, "/data/suhu", suhu);
    Firebase.RTDB.setFloat(&fbdo, "/data/kelembapan", kelembapan);
    Firebase.RTDB.setFloat(&fbdo, "/data/tekanan", tekanan);
    Firebase.RTDB.setInt(&fbdo, "/data/nilai_hujan", nilai_hujan);
    Firebase.RTDB.setString(&fbdo, "/data/status_air", status_air);
    Firebase.RTDB.setString(&fbdo, "/data/status_hujan", status_hujan);
    Firebase.RTDB.setString(&fbdo, "/data/prediksi_cuaca", prediksi_cuaca);
  }
  // Debug Serial
  Serial.println("=== DATA ===");
  Serial.println("Jarak ke Air  : " + String(jarak) + " cm");
  Serial.println("Tinggi Air    : " + String(tinggi_air) + " %");
  Serial.println("Status Air    : " + status_air);
  Serial.println("Status Hujan  : " + status_hujan);
  Serial.println("Nilai Hujan   : " + String(nilai_hujan));
  Serial.println("Suhu          : " + String(suhu));
  Serial.println("Kelembapan    : " + String(kelembapan));
  Serial.println("Tekanan       : " + String(tekanan));
  Serial.println("Prediksi Cuaca: " + prediksi_cuaca);
  Serial.println("==================");
}

