# Anggota Kelompok
1. Cellia Auzia Nugraha (2309106005)
2. Oktaria Indi Cahyani (2309106015)
3. Al Hajj Fauzan (2309106019)

# Judul Proyek
Sistem Monitoring dan Kontrol Greenhouse

# Deskripsi
Proyek ini merupakan solusi cerdas untuk manajemen lingkungan tanam melalui otomasi aktuator dan pemantauan jarak jauh. Sistem ini dirancang untuk meningkatkan efisiensi operasional serta menjaga kualitas pertumbuhan tanaman dengan meminimalisir intervensi manual yang tidak konsisten.

Fitur Utama Sistem:
- Kontrol Otomatis Berbasis Sensor
  Otomasi Penyiraman: Pompa mini akan aktif secara otomatis ketika sensor kelembapan mendeteksi kondisi tanah yang kering.
  Regulasi Ventilasi: Menggunakan sensor BH1750 untuk membaca intensitas cahaya; jika melebihi ambang batas, servo akan membuka ventilasi secara otomatis sebagai     tindakan preventif suhu berlebih.
- Monitoring dan Kontrol Jarak Jauh:
  Mobile Interface: Integrasi dengan Kodular untuk memantau data sensor secara real-time melalui aplikasi perangkat bergerak.
  Telegram Bot: Digunakan sebagai media notifikasi instan serta kendali manual jarak jauh untuk komponen LED, servo, dan pompa.
- Sistem Peringatan Terpadu: Penggunaan indikator visual (LED) dan notifikasi digital untuk memastikan kondisi lingkungan tetap terpantau, sehingga risiko          kegagalan panen dapat ditekan secara efektif.

# Pembagian Tugas
1. Cellia : Memegang kendali di bagian koding atau logika program sistem. Dan juga mengatur integrasi Telegram Bot.
  
2. Oktaria : Bertugas merancang tampilan dan fungsi aplikasi kodular supaya data sensor bisa dipantau lewat HP dan kontrol jarak jauh bisa berjalan. Dan juga melakukan bug hunting, pengetesan sistem secara berkala, serta memastikan sensor tetap bekerja normal tanpa ada anomali pada data yang dikirim.

3. Al Hajj : Bertanggung jawab untuk urusan merakit komponen perangkat keras dan integrasi ke Antares. Dan memastikan semua komponen tersambung dengan benar dan data dari alat bisa terkirim ke cloud tanpa kendala.

# Komponen yang digunakan
1. ESP32
2. BH1750 (Sensor Intensitas Cahaya)
3. Capacitive Soil Moisture Sensor (Sensor Kelembapan Tanah)
4. Kabel Jumper
5. Servo
6. Relay
7. LED
8. Pompa Mini

# Board Schematic
<img width="1558" height="705" alt="WhatsApp Image 2026-05-15 at 06 01 24" src="https://github.com/user-attachments/assets/19df7092-f850-4f46-ad06-003283584e0f" />
