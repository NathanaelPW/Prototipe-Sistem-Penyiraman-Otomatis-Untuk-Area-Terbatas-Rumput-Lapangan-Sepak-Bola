# Smart Irrigation System
protoripe sistem penyiraman rumput lapangan sepak bola otomatis menggunakan: ESP3 (mikrokontroller utama), Soil moisture sensor (kelembaban tanah), Sensor pH tanah, DC water pump, Relay (menghidupkan/mematikan pompa), power supply unit (mengubah listrik AC ke DC), Stepdown (menurunkan tegangan agar dapat digunakan ESP32), (peniyraman), Firebase (database), User interface (mobile application, dan sprinkler untuk menyiram (komponen non elektronik)

## Features
- penjadwalan dimana pengguna dapat mengatur jadwal penyiraman pada aplikasi, sehingga penyiraman hanya akan terjadi jika jadwal sesuai dan kelembaban dibawah 70%.
- fitur automode dimana sistem akan otomatis menyiram dan berhenti menyiram. Dimana sensor kelembaban tanah menjadi indikator utama dalam menentukan penyiraman. Penyiraman akan aktif jika kelembaban tanah kurang dari 70% dan akan mati jika kelembaban tanah ≥70%

## How to Run
1. Upload kode ke ESP32
2. Hubungkan ke Firebase
3. Jalankan mobile app

## Tech Stack
- ESP32
- Firebase Realtime Database
- Flutter (mobile app)
