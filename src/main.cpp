#include <Arduino.h>
#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <NTPClient.h>
#include <WiFiUdp.h> 

// === Definisi Pin ===
const int SOIL_SENSOR_PIN = 32;
#define DMSpin 13
#define indikator 2
#define adcPin 34
#define relayPin 25
#define indikatorFirebase 4

// === WiFi dan Firebase ===
#define WIFI_SSID "Susi's Fam"
#define WIFI_PASSWORD "30150003"
#define API_KEY "AIzaSyBdUDAbeUCMrET8f-O-ieyTK6D0BN8ksio"
#define DATABASE_URL "https://penyiraman-otomatis-ce65e-default-rtdb.asia-southeast1.firebasedatabase.app/"
#define USER_EMAIL "firza305@gmail.com"
#define USER_PASSWORD "firza2003"

// Firebase Configuration
FirebaseData fbdo;
FirebaseData fbdo2; // Tambahkan fbdo2 untuk task Firebase kedua jika diperlukan
FirebaseData fbdo3; // Tambahkan fbdo3 untuk task Firebase ketiga jika diperlukan

FirebaseConfig config;
FirebaseAuth auth;

// Variabel Global
int soilMoisturePercent = 0;
float pH = 0.0;
bool isPumpOn = false;
bool isManualControl = false; // True jika pompa dikontrol secara manual
bool isScheduleRunning = false; // True jika penyiraman terjadwal sedang berjalan
String lastPumpCommand = ""; // Menyimpan perintah pompa terakhir

// Tambahkan di global
bool scheduleWateringActive = false;
unsigned long scheduleStartMillis = 0;

// Task Handles
TaskHandle_t TaskSensor;
TaskHandle_t TaskFirebase;
TaskHandle_t TaskScheduleCommand;
TaskHandle_t TaskScheduleWatering;

// Fungsi untuk mengontrol pompa
void controlPump(bool state) {
  isPumpOn = state;
  digitalWrite(relayPin, isPumpOn ? LOW : HIGH);
  Serial.print("Pump is now ");
  Serial.println(isPumpOn ? "ON" : "OFF");
  Serial.print("Pompa dikontrol oleh: ");
  Serial.println((uintptr_t)xTaskGetCurrentTaskHandle(), HEX);
}

// Task: Membaca Data Sensor
void TaskSensorCode(void *pvParameters) {
  for (;;) {

    // Membaca Sensor Kelembaban Tanah
     analogReadResolution(12);
    int soilMoistureValue = analogRead(SOIL_SENSOR_PIN);
    soilMoisturePercent = map(soilMoistureValue, 4095, 0, 0, 100);

    // Membaca Sensor pH 
    analogReadResolution(10);
    int adcValue = 0;
    digitalWrite(DMSpin, LOW);      // Aktifkan DMS
    digitalWrite(indikator, HIGH); // LED indikator menyala
    adcValue = analogRead(adcPin);
    pH = (-0.0233 * adcValue) + 12.698; // Kalibrasi pH
    digitalWrite(DMSpin, HIGH);
    digitalWrite(indikator, LOW);
    

    // Menentukan Kondisi Tanah
    String soilCondition;
    if (soilMoisturePercent < 40) {
      soilCondition = "Dry";
    } else if (soilMoisturePercent >= 70) {
      soilCondition = "Wet";
    } else {
      soilCondition = "Normal";
    }

    // Log Data Sensor ke Serial Monitor
    Serial.println("=== Sensor Data ===");
    Serial.print("Soil Moisture: ");
    Serial.print(soilMoisturePercent);
    Serial.println("%");
    Serial.print("Soil Condition: ");
    Serial.println(soilCondition);
    Serial.print("pH: ");
    Serial.println(pH, 3); // Menampilkan pH dengan 3 angka desimal
    Serial.print("ADC pH Value: ");
    Serial.println(adcValue);
    Serial.print("Pump Status: ");
    Serial.println(isPumpOn ? "ON" : "OFF");
    Serial.println("===================");

    vTaskDelay(10000 / portTICK_PERIOD_MS); // Delay 10 detik
  }
}

// Task: Mengirim Data ke Firebase
void TaskFirebaseCode(void *pvParameters) {
  for (;;) {
    FirebaseJson json;
    json.set("soilMoisture", soilMoisturePercent);
    json.set("pH", pH);
    json.set("pumpStatus", isPumpOn ? "ON" : "OFF");
    json.set("soilCondition", (soilMoisturePercent < 60) ? "Dry" : (soilMoisturePercent >= 70) ? "Wet" : "Normal");

    if (Firebase.RTDB.pushJSON(&fbdo, "/sensorData", &json)) {
      Serial.println("Data sent to Firebase!");
    } else {
      Serial.println("Failed to send data to Firebase.");
      Serial.println("Error: " + fbdo.errorReason());
    }

    vTaskDelay(60000 / portTICK_PERIOD_MS); // Delay 1 menit
  }
}

int scheduleDay = 1;      // 1 = Senin (default)
int scheduleHour = 6;     // 6 pagi (default)
int scheduleMinute = 0;   // 00 menit (default)
int scheduleDuration = 5; // 5 menit (default)

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 25200, 60000); // GMT+7

// === TaskScheduleCommandCode ===
void TaskScheduleCommandCode(void *pvParameters) {
  for (;;) {
    // --- Ambil perintah dari Firebase ---
    if (Firebase.RTDB.getString(&fbdo2, "/pumpControl/command")) {
      String command = fbdo2.stringData();
      if (command != lastPumpCommand) {
        lastPumpCommand = command;
        if (command == "TOGGLE_PUMP") {
          isManualControl = true;
          controlPump(!isPumpOn);
          Serial.println("TOGGLE_PUMP from Firebase");
        } else if (command == "PUMP_ON") {
          isManualControl = true;
          controlPump(true);
          Serial.println("PUMP_ON from Firebase");
        } else if (command == "PUMP_OFF") {
          isManualControl = true;
          controlPump(false);
          Serial.println("PUMP_OFF from Firebase");
        } else if (command == "AUTO_MODE") {
          isManualControl = false;
          Serial.println("AUTO_MODE from Firebase");
        } else {
          Serial.println("Unknown command from Firebase.");
        }
      }
    }

    yield(); // Beri kesempatan task lain untuk berjalan
    // --- Cek status pompa di Firebase --
    vTaskDelay(3000 / portTICK_PERIOD_MS); // Cek perintah setiap 0.5 detik
  }
}

// === TaskScheduleWateringCode ===
void TaskScheduleWateringCode(void *pvParameters) {
  timeClient.begin();
  static bool alreadyWatered = false;

  for (;;) {
    // Cek kelembaban tanah sebelum menjalankan schedule
    if (soilMoisturePercent >= 70) {
      // Tanah sudah cukup lembab, tidak perlu menjalankan schedule
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue; // Lewati iterasi ini, cek lagi nanti
    }

    if (isManualControl) {
      timeClient.update();
      int currentDay = timeClient.getDay();
      int currentHour = timeClient.getHours();
      int currentMinute = timeClient.getMinutes();

      // Ambil jadwal terbaru dari Firebase setiap 1 menit
      if (Firebase.RTDB.getInt(&fbdo3, "/schedule/day")) scheduleDay = fbdo3.intData();
      if (Firebase.RTDB.getInt(&fbdo3, "/schedule/hour")) scheduleHour = fbdo3.intData();
      if (Firebase.RTDB.getInt(&fbdo3, "/schedule/minute")) scheduleMinute = fbdo3.intData();
      if (Firebase.RTDB.getInt(&fbdo3, "/schedule/duration")) scheduleDuration = fbdo3.intData();

      // --- Logika schedule ---
      if (currentDay == scheduleDay && currentHour == scheduleHour && currentMinute == scheduleMinute && !isScheduleRunning) {  
        Serial.println("Scheduled watering started.");
        scheduleWateringActive = true;
        size_t i = 0;

        for (i = 0; i < 2; i++) {
          controlPump(true);
          vTaskDelay(pdMS_TO_TICKS(1*60000)); // Tunggu selama durasi penyiraman
          controlPump(false);
          if (soilMoisturePercent >= 70) {
            Serial.println("Soil is wet enough, stopping scheduled watering.");
            break;
          }
        }
        if (i == 2) {
          Serial.println("sensor or pump error.");
        }
      }
    } else {
      if (soilMoisturePercent < 70) {
        controlPump(true);
      } else {
        controlPump(false);
      }
    }
     

    // if (currentDay == scheduleDay && currentHour == scheduleHour && currentMinute == scheduleMinute+1 && isScheduleRunning) {  
    //   isScheduleRunning = false;
    // }
    // // Matikan pompa jika sudah cukup lama (misal 5 menit) atau tanah sudah lembab
    // if (scheduleWateringActive) {
    //   if (millis() - scheduleStartMillis >= scheduleDuration * 60000UL || soilMoisturePercent >= 80) {
    //     controlPump(false);
    //     isScheduleRunning = false;
    //     scheduleWateringActive = false;
    //     Serial.println("Scheduled watering finished.");
    //   }
    // }

    // --- Logika auto mode kelembaban (jika tidak manual dan tidak sedang schedule) ---
    if (!isManualControl && !isScheduleRunning && !scheduleWateringActive) {

  if (soilMoisturePercent < 70) {
    if (!isPumpOn) {
      controlPump(true);
      Serial.println("AUTO_MODE: Pompa ON, kelembaban = " + String(soilMoisturePercent));
    }
  } else {
    if (isPumpOn) {
      controlPump(false);
      Serial.println("AUTO_MODE: Pompa OFF, kelembaban = " + String(soilMoisturePercent));
    }
  }
}
    vTaskDelay(pdMS_TO_TICKS(1000)); // Delay 1 detik untuk menghindari pembacaan berlebihan
  }
}

void connectToWiFi() {
  Serial.print("Connecting to WiFi");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  pinMode(indikator, OUTPUT); // Pastikan pin indikator diinisialisasi
  digitalWrite(indikator, LOW); // Matikan LED indikator saat belum connect

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.println("Connected to WiFi!");
  digitalWrite(indikator, HIGH); // Nyalakan LED indikator saat sudah connect
}

void setup() {
  Serial.begin(115200);

  // Inisialisasi Pin
  pinMode(SOIL_SENSOR_PIN, INPUT);
  pinMode(adcPin, INPUT);
  pinMode(DMSpin, OUTPUT);
  pinMode(indikator, OUTPUT);
  pinMode(indikatorFirebase, OUTPUT);
  pinMode(relayPin, OUTPUT);

  // Pastikan pompa dan indikator mati saat awal
  digitalWrite(DMSpin, HIGH);  // Nonaktifkan DMS
  digitalWrite(indikator, LOW); // LED WiFi mati
  digitalWrite(indikatorFirebase, LOW); // LED Firebase mati
  delay(100); // Tunggu 100ms untuk memastikan relay stabil

  // *** Tambahkan baris ini untuk memastikan pompa benar-benar OFF ***
  controlPump(false);

  // Koneksi WiFi
  connectToWiFi();

  // Konfigurasi Firebase
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

   Firebase.reconnectWiFi(true);


  // Periksa koneksi Firebase
  if (Firebase.ready()) {
    Serial.println("Firebase connected!");
    digitalWrite(indikatorFirebase, HIGH); // Nyalakan LED indikator Firebase
  } else {
    Serial.println("Failed to connect to Firebase.");
    Serial.println("Error: " + fbdo.errorReason());
    digitalWrite(indikatorFirebase, LOW); // Pastikan LED indikator Firebase mati
  }

  // Pastikan status pompa di Firebase adalah OFF saat perangkat menyala
  if (Firebase.RTDB.setString(&fbdo, "/pumpStatus", "OFF")) {
    Serial.println("Pump status set to OFF in Firebase.");
  } else {
    Serial.println("Failed to set pump status in Firebase.");
    Serial.println("Error: " + fbdo.errorReason());
  }

  // *** Tambahkan kode berikut untuk menulis node /schedule jika belum ada ***
  FirebaseJson scheduleJson;
  scheduleJson.set("day", scheduleDay);
  scheduleJson.set("hour", scheduleHour);
  scheduleJson.set("minute", scheduleMinute);
  scheduleJson.set("duration", scheduleDuration);

  if (Firebase.RTDB.setJSON(&fbdo, "/schedule", &scheduleJson)) {
    Serial.println("Default schedule node written to Firebase.");
  } else {
    Serial.println("Failed to write default schedule node.");
    Serial.println("Error: " + fbdo.errorReason());
  }

  // Membuat Tasks dengan error handling
  BaseType_t result;

  result = xTaskCreatePinnedToCore(TaskSensorCode, "TaskSensor", 2048, NULL, 1, &TaskSensor, 0);
  if (result != pdPASS) {
    Serial.println("Error: Failed to create TaskSensorCode!");
  }

  result = xTaskCreatePinnedToCore(TaskFirebaseCode, "TaskFirebase", 6144, NULL, 1, &TaskFirebase, 1);
  if (result != pdPASS) {
    Serial.println("Error: Failed to create TaskFirebaseCode!");
  }

  // Tambahkan dua task baru, hapus TaskScheduleCode lama!
  result = xTaskCreatePinnedToCore(TaskScheduleCommandCode, "TaskScheduleCmd", 6144, NULL, 1, &TaskScheduleCommand, 0);
  if (result != pdPASS) {
    Serial.println("Error: Failed to create TaskScheduleCommandCode!");
  }

  result = xTaskCreatePinnedToCore(TaskScheduleWateringCode, "TaskScheduleWater", 6144, NULL, 1, &TaskScheduleWatering, 1);
  if (result != pdPASS) {
    Serial.println("Error: Failed to create TaskScheduleWateringCode!");
  }
}

void loop() {
  // Tidak ada yang dilakukan di loop utama, semua berjalan di tasks
}
