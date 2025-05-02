#include <Arduino.h>
#include <DHT.h>
#include <WiFi.h>
#include <HTTPClient.h>

// === WiFi Credentials ===
const char* ssid     = "Jmldnnn";
const char* password = "05202444";

// === DHT11 Setup ===
#define DHTPIN 16
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

// === Soil Moisture Sensor Setup ===
#define MOISTURE_PIN 34  // Pin untuk sensor kelembaban tanah

// === Server Flask ===
const char* serverUrl = "http://172.20.10.4:5000/predict";  // Ganti IP jika perlu

void setup() {
  Serial.begin(115200);
  dht.begin();

  // Koneksi WiFi
  Serial.print("Menghubungkan ke WiFi...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\n✅ Terhubung ke WiFi!");
  Serial.print("📡 IP Address: ");
  Serial.println(WiFi.localIP());
}

void loop() {
  // Baca sensor DHT11
  float suhu = dht.readTemperature();
  float kelembapan = dht.readHumidity();

  // Baca sensor kelembaban tanah
  int moisture = analogRead(MOISTURE_PIN);  // Membaca nilai kelembaban tanah

  if (isnan(suhu) || isnan(kelembapan)) {
    Serial.println("❌ Gagal membaca data dari DHT11!");
    delay(2000);
    return;
  }

  // Tampilkan data ke Serial
  Serial.println("=== Pembacaan Sensor ===");
  Serial.printf("🌡️ Suhu               : %.2f °C\n", suhu);
  Serial.printf("💧 Kelembapan Udara   : %.2f %%\n", kelembapan);
  Serial.printf("🌿 Kelembaban Tanah   : %d\n", moisture);  // Tampilkan kelembaban tanah

  // Susun JSON sesuai kebutuhan Flask
  String jsonData = "{";
  jsonData += "\"suhu\": " + String(suhu, 2) + ",";
  jsonData += "\"kelembapan udara\": " + String(kelembapan, 2) + ",";
  jsonData += "\"kelembaban tanah\": " + String(moisture);
  jsonData += "}";

  // Kirim ke Flask
  HTTPClient http;
  http.begin(serverUrl);
  http.addHeader("Content-Type", "application/json");

  int code = http.POST(jsonData);
  if (code > 0) {
    String response = http.getString();
    Serial.println("--- Respon dari Server Flask ---");
    Serial.println(response);
    Serial.println("================================\n");
  } else {
    Serial.println("❌ Gagal mengirim data! Kode HTTP: " + String(code));
  }

  http.end();
  delay(5000); // Delay agar tidak terlalu sering
}
