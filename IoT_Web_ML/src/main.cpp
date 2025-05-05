#include <Arduino.h>
#include <DHT.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>

// Definisi pin
#define DHT_PIN 16
#define SOIL_MOISTURE_PIN 34
#define DHT_TYPE DHT11

// Konstanta untuk konversi nilai sensor
#define MIN_SOIL_VALUE 1000  
#define MAX_SOIL_VALUE 4095  

// Konstanta untuk interpretasi data
#define VERY_DRY_THRESHOLD 0.4
#define DRY_THRESHOLD 0.6
#define WET_THRESHOLD 1.2

// Inisialisasi sensor DHT
DHT dht(DHT_PIN, DHT_TYPE);

// Variabel untuk WiFi dan server Flask
const char* ssid = "Jmldnnn";      // Nama WiFi Anda
const char* password = "05202444";  // Password WiFi Anda
const char* flaskServer = "http://172.20.10.4:5000";  // Alamat IP server Flask Anda

// Variabel untuk NTP
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 25200;  // GMT+7 untuk Indonesia (dalam detik)
const int daylightOffset_sec = 0;

// Variabel untuk menyimpan waktu terakhir pembacaan dan pengiriman data
unsigned long lastReadingTime = 0;
const long readingInterval = 5000;  // Interval pembacaan sensor (5 detik)

// Variabel untuk logging
#define LOG_BUFFER_SIZE 10
struct SensorLog {
  float temperature;
  float humidity;
  float soilHumidity;
  int rawSoilValue;
  time_t timestamp;
};
SensorLog sensorLogs[LOG_BUFFER_SIZE];
int logIndex = 0;

// Struktur untuk menyimpan hasil prediksi
struct PredictionResult {
  bool needsWatering;
  String optimalTime;
  String alternativeTimes[3];  // Array untuk menyimpan waktu alternatif
  int numAlternatives;         // Jumlah waktu alternatif
  float wateringProbability;   // Probabilitas penyiraman
};

// Variabel global untuk menyimpan hasil prediksi terakhir
PredictionResult lastPrediction = {false, "00:00", {"", "", ""}, 0, 0.0};
bool hasPrediction = false;

// Fungsi untuk mengkonversi nilai ADC ke kelembaban tanah (0.0 - 2.0) dan persentase (0-100%)
float convertToSoilHumidity(int adcValue) {
  // Konversi nilai ADC ke persentase kelembaban (terbalik karena nilai ADC lebih tinggi saat kering)
  float percentHumidity = map(adcValue, MAX_SOIL_VALUE, MIN_SOIL_VALUE, 0, 100) / 100.0;
  
  // Konversi ke skala yang digunakan dalam model ML (0.0 - 2.0)
  float scaledHumidity = percentHumidity * 2.0;
  
  // Batasi nilai dalam rentang yang valid (0.0 - 2.0)
  scaledHumidity = constrain(scaledHumidity, 0.0, 2.0);
  
  return scaledHumidity;
}

// Fungsi untuk mendapatkan persentase kelembaban tanah
int getSoilMoisturePercent(int adcValue) {
  // Konversi nilai ADC ke persentase kelembaban (terbalik karena nilai ADC lebih tinggi saat kering)
  int percentHumidity = map(adcValue, MAX_SOIL_VALUE, MIN_SOIL_VALUE, 0, 100);
  
  // Batasi nilai dalam rentang yang valid (0-100%)
  percentHumidity = constrain(percentHumidity, 0, 100);
  
  return percentHumidity;
}

// Fungsi untuk memvalidasi hasil prediksi
bool validatePrediction(PredictionResult result, float soilHumidity) {
  // Validasi logis berdasarkan kelembaban tanah
  if (soilHumidity < VERY_DRY_THRESHOLD && !result.needsWatering) {
    Serial.println("PERINGATAN: Tanah sangat kering tetapi model memprediksi tidak perlu disiram");
    return false;
  }
  
  if (soilHumidity > WET_THRESHOLD && result.needsWatering) {
    Serial.println("PERINGATAN: Tanah basah tetapi model memprediksi perlu disiram");
    return false;
  }
  
  return true;
}

// Fungsi untuk mendapatkan prediksi dari server Flask
PredictionResult getPredictionFromServer(float temperature, float humidity, float soilHumidity) {
  PredictionResult result = {false, "00:00", {"", "", ""}, 0, 0.0};
  
  // Cek koneksi WiFi
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Error: WiFi tidak terhubung");
    return result;
  }
  
  HTTPClient http;
  
  // Siapkan URL endpoint
  String url = String(flaskServer) + "/api/predict";
  
  http.begin(url);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  
  // Dapatkan waktu saat ini untuk dikirim ke server
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)) {
    // Gunakan nilai default jika gagal mendapatkan waktu
    timeinfo.tm_hour = 12;
    timeinfo.tm_mday = 1;
    timeinfo.tm_mon = 0;
    timeinfo.tm_wday = 1;
  }
  
  // Siapkan data yang akan dikirim
  String httpRequestData = "air_temp=" + String(temperature) + 
                          "&air_humidity=" + String(humidity) + 
                          "&soil_humidity=" + String(soilHumidity) +
                          "&hour=" + String(timeinfo.tm_hour) +
                          "&day=" + String(timeinfo.tm_mday) +
                          "&month=" + String(timeinfo.tm_mon + 1) +
                          "&dayofweek=" + String(timeinfo.tm_wday);
  
  // Kirim POST request
  int httpResponseCode = http.POST(httpRequestData);
  
  // Periksa respons
  if (httpResponseCode > 0) {
    String payload = http.getString();
    
    // Parse JSON response
    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, payload);
    
    if (!error) {
      if (doc.containsKey("membutuhkan_siram")) {
        String needsWateringStr = doc["membutuhkan_siram"].as<String>();
        result.needsWatering = (needsWateringStr == "Ya");
      }
      
      if (doc.containsKey("waktu_optimal")) {
        result.optimalTime = doc["waktu_optimal"].as<String>();
      }
      
      // Ambil waktu alternatif jika ada
      if (doc.containsKey("waktu_alternatif") && doc["waktu_alternatif"].is<JsonArray>()) {
        JsonArray alternatives = doc["waktu_alternatif"].as<JsonArray>();
        result.numAlternatives = min((int)alternatives.size(), 3);  // Maksimal 3 alternatif
        
        int i = 0;
        for (JsonVariant value : alternatives) {
          if (i < 3) {  // Batasi maksimal 3 alternatif
            result.alternativeTimes[i] = value.as<String>();
            i++;
          }
        }
      }
      
      // Ambil probabilitas penyiraman jika ada
      if (doc.containsKey("probabilitas_siram")) {
        result.wateringProbability = doc["probabilitas_siram"].as<float>();
      }
    }
  }
  
  // Tutup koneksi
  http.end();
  
  // Validasi hasil prediksi
  validatePrediction(result, soilHumidity);
  
  return result;
}

// Fungsi untuk menambahkan data ke log
void addToSensorLog(float temperature, float humidity, float soilHumidity, int rawSoilValue) {
  time_t now;
  time(&now);
  
  sensorLogs[logIndex].temperature = temperature;
  sensorLogs[logIndex].humidity = humidity;
  sensorLogs[logIndex].soilHumidity = soilHumidity;
  sensorLogs[logIndex].rawSoilValue = rawSoilValue;
  sensorLogs[logIndex].timestamp = now;
  
  logIndex = (logIndex + 1) % LOG_BUFFER_SIZE;
}

// Fungsi untuk menampilkan status kelembaban tanah
String getSoilStatusText(float soilHumidity) {
  if (soilHumidity < VERY_DRY_THRESHOLD) {
    return "SANGAT KERING";
  } else if (soilHumidity < DRY_THRESHOLD) {
    return "KERING";
  } else if (soilHumidity < WET_THRESHOLD) {
    return "NORMAL";
  } else {
    return "BASAH";
  }
}

// Fungsi untuk menampilkan analisis prediksi dengan waktu alternatif
void displayDetailedPrediction(PredictionResult result, float soilHumidity) {
  // Get current time for context
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)) {
    return;
  }
  
  int currentHour = timeinfo.tm_hour;
  int currentMinute = timeinfo.tm_min;
  
  Serial.println("\n=== ANALISIS PREDIKSI DETAIL ===");
  
  // Parse optimal time (format HH:MM)
  int optimalHour = result.optimalTime.substring(0, 2).toInt();
  int optimalMinute = result.optimalTime.substring(3, 5).toInt();
  
  // Determine if optimal time is today or tomorrow
  bool isToday = (optimalHour > currentHour) || 
                 (optimalHour == currentHour && optimalMinute > currentMinute);
  
  Serial.print("Waktu sekarang: ");
  Serial.print(currentHour);
  Serial.print(":");
  if (currentMinute < 10) Serial.print("0");
  Serial.println(currentMinute);
  
  Serial.print("Waktu optimal penyiraman: ");
  Serial.print(result.optimalTime);
  Serial.println(isToday ? " (hari ini)" : " (besok)");
  
  // Calculate minutes until watering
  int minutesUntilWatering;
  if (isToday) {
    minutesUntilWatering = (optimalHour - currentHour) * 60 + (optimalMinute - currentMinute);
  } else {
    minutesUntilWatering = (24 - currentHour + optimalHour) * 60 + (optimalMinute - currentMinute);
  }
  
  // Display time until watering in hours and minutes
  int hoursUntilWatering = minutesUntilWatering / 60;
  int remainingMinutes = minutesUntilWatering % 60;
  
  Serial.print("Waktu hingga penyiraman optimal: ");
  Serial.print(hoursUntilWatering);
  Serial.print(" jam ");
  Serial.print(remainingMinutes);
  Serial.println(" menit");
  
  
  // Tampilkan waktu alternatif jika ada
  if (result.numAlternatives > 0) {
    Serial.println("\nWaktu alternatif penyiraman:");
    for (int i = 0; i < result.numAlternatives; i++) {
      // Parse alternative time
      int altHour = result.alternativeTimes[i].substring(0, 2).toInt();
      int altMinute = result.alternativeTimes[i].substring(3, 5).toInt();
      
      // Determine if alternative time is today or tomorrow
      bool altIsToday = (altHour > currentHour) || 
                        (altHour == currentHour && altMinute > currentMinute);
      
      Serial.print("  - ");
      Serial.print(result.alternativeTimes[i]);
      Serial.println(altIsToday ? " (hari ini)" : " (besok)");
    }
  }
  
  Serial.println("====================================");
}

void setup() {
  // Inisialisasi Serial
  Serial.begin(115200);
  delay(1000); // Tunggu serial monitor siap
  Serial.println("\nSistem Prediksi Penyiraman Tanaman dengan Flask");
  
  // Inisialisasi sensor DHT
  dht.begin();
  
  // Konfigurasi pin sensor kelembaban tanah
  pinMode(SOIL_MOISTURE_PIN, INPUT);
  
  // Koneksi ke WiFi
  Serial.print("Menghubungkan ke ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println("");
  Serial.println("WiFi terhubung");
  Serial.print("Alamat IP: ");
  Serial.println(WiFi.localIP());
  
  // Inisialisasi waktu
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  
  // Reset timer
  lastReadingTime = 0;
  hasPrediction = false;
  
  // Inisialisasi log sensor
  memset(sensorLogs, 0, sizeof(sensorLogs));
}

void loop() {
  unsigned long currentMillis = millis();
  
  // Baca sensor setiap interval tertentu
  if (currentMillis - lastReadingTime >= readingInterval) {
    lastReadingTime = currentMillis;
    
    // Baca suhu dan kelembaban dari DHT11
    float humidity = dht.readHumidity();
    float temperature = dht.readTemperature();
    
    // Baca nilai sensor kelembaban tanah
    int soilValue = analogRead(SOIL_MOISTURE_PIN);
    float soilHumidity = convertToSoilHumidity(soilValue);
    int soilMoisturePercent = getSoilMoisturePercent(soilValue);
    
    // Periksa apakah pembacaan berhasil
    if (isnan(humidity) || isnan(temperature)) {
      Serial.println("Gagal membaca dari sensor DHT!");
      return;
    }
    
    // Tambahkan data ke log
    addToSensorLog(temperature, humidity, soilHumidity, soilValue);
    
    // Dapatkan prediksi dari server Flask
    PredictionResult prediction = getPredictionFromServer(temperature, humidity, soilHumidity);
    lastPrediction = prediction;
    hasPrediction = true;
    
    // Tampilkan hasil pembacaan sensor dengan format yang lebih sederhana
    Serial.println("\n=== DATA SENSOR ===");
    Serial.print("Suhu: ");
    Serial.print(temperature);
    Serial.println(" °C");
    
    Serial.print("Kelembapan Udara: ");
    Serial.print(humidity);
    Serial.println(" %");
    
    Serial.print("Kelembapan Tanah: ");
    Serial.print(soilMoisturePercent);
    Serial.println(" %");
    
    Serial.print("Status Tanah: ");
    Serial.println(getSoilStatusText(soilHumidity));
    
    Serial.print("Membutuhkan Siram: ");
    Serial.println(prediction.needsWatering ? "Ya" : "Tidak");
    Serial.println("====================================");
    
    // Tampilkan analisis prediksi dengan waktu alternatif
    displayDetailedPrediction(prediction, soilHumidity);
  }
}