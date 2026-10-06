// =====================================================================
//  MUNCHFISH KEYCHAIN v3  -  @nfnailalhusna
//  ESP32-C3 Super Mini + OLED 0.96" (SSD1306, I2C) + buzzer + 4 tombol
//
//  Fitur : ikan berenang, kasih makan, flappy fish, reminder makan,
//          pomodoro, todo list, status ikan, teks berjalan, preview musik.
//  Koneksi: Bluetooth LOW ENERGY (BLE) ke aplikasi Flutter. TANPA WiFi.
//           (ESP32-C3 cuma punya BLE, bukan Bluetooth Classic.)
//
//  Library: Adafruit GFX + Adafruit SSD1306 (install dari Library Manager).
//           BLE & Preferences sudah bawaan core ESP32 (v2.x maupun v3.x).
//  Kalau muncul "Sketch too big": Tools > Partition Scheme > "Huge APP".
//  Kalau mau ngetes tanpa Bluetooth: ubah ENABLE_BLE jadi 0.
//
//  KONTROL TOMBOL
//   btn1  pendek: kasih makan      | panjang: menu (Default / Tulisan / Status)
//   btn2  pendek: tulisan nama     | panjang: todo list
//   btn3  pendek: snooze reminder  | panjang: mini game (saat game: loncat)
//   btn4  pendek: elus ikan        | panjang: buka/tutup pomodoro
//         (saat pomodoro tampil: pendek = mulai / jeda)
//   Di menu   : btn1 atas, btn3 bawah, btn2 pilih, btn4 kembali
//   Di todo   : btn1 atas, btn3 bawah, btn2 ceklis, btn2 tahan / btn4 keluar
//   Di status : tombol apa saja untuk keluar
//
//  PROTOKOL BLE ringkas (detail lengkap ada di BLE_PROTOCOL.md):
//   Nama perangkat "Munchfish", semua angka little-endian.
// =====================================================================
#define ENABLE_BLE 1

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Preferences.h>
#if ENABLE_BLE
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#endif

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_SDA 8
#define OLED_SCK 9
#define BUZZER_PIN 6

// ---- tombol (semua ke GND, pakai INPUT_PULLUP) ----
// Urutan array = btn 1, btn 2, btn 3, btn 4.
// CEK & SESUAIKAN nomor GPIO-nya dengan schematic/PCB lo!
const uint8_t BTN_PIN[4] = { 7, 3, 1, 0 };
enum { BTN_FEED = 0, BTN_TEXT = 1, BTN_GAME = 2, BTN_PET = 3 };

// ---- posisi tujuan ikan saat tombol ditekan ----
// (x, y) = pojok kiri-atas bitmap ikan. Sesuaikan dengan tata letak tombol di PCB.
const int BTN_TARGET_X[4] = {  0, 18, 67, 92 };
const int BTN_TARGET_Y[4] = { 24, 44, 44, 44 };

// ---- teks ----
const char* TEXT_LINE_1 = "@nfnailalhusna";
const char* TEXT_LINE_2 = "munchfish keychain";
#define DEFAULT_TEXT "@nfnailalhusna munchfish keychain"   // teks berjalan bawaan (bisa diganti dari HP)

// ---- pomodoro ----
const unsigned long POMO_FOCUS_MS = 25UL * 60 * 1000;
const unsigned long POMO_BREAK_MS = 5UL * 60 * 1000;
const unsigned long POMO_LONG_MS  = 15UL * 60 * 1000;
const int POMO_SETS = 4;                       // setelah 4 fokus -> istirahat panjang

// ---- UUID BLE (jangan diubah kalau aplikasinya sudah dibikin) ----
#define SVC_UUID      "5f3a0001-7b6e-4c1d-9a2f-6d756e636831"
#define CH_TODO_CMD   "5f3a0002-7b6e-4c1d-9a2f-6d756e636831"  // WRITE  : perintah todo
#define CH_TODO_LIST  "5f3a0003-7b6e-4c1d-9a2f-6d756e636831"  // READ   : isi todo list
#define CH_TEXT       "5f3a0004-7b6e-4c1d-9a2f-6d756e636831"  // R/W    : teks berjalan
#define CH_MODE       "5f3a0005-7b6e-4c1d-9a2f-6d756e636831"  // R/W    : 0 default, 1 tulisan
#define CH_TIME       "5f3a0006-7b6e-4c1d-9a2f-6d756e636831"  // WRITE  : jam dari HP
#define CH_MEDIA      "5f3a0007-7b6e-4c1d-9a2f-6d756e636831"  // WRITE  : lagu yang diputar
#define CH_STATS      "5f3a0008-7b6e-4c1d-9a2f-6d756e636831"  // READ   : streak/eat/week
#define CH_EVENT      "5f3a0009-7b6e-4c1d-9a2f-6d756e636831"  // NOTIFY : "ada yang berubah"

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ================= BITMAP IKAN 36x18 (1-bit, horizontal) =================
// fish{R,L}_{normal,happy,angry,eat}_{0,1} : 2 frame (ekor goyang + sirip dada kepak)
// mask{R,L}_{0,1} : siluet + outline 1 px supaya ornamen gak tembus ke badan ikan.
#define FISH_W 36
#define FISH_H 18

const unsigned char fishR_normal_0[] PROGMEM = {
  0xC0, 0x00, 0x7C, 0xE0, 0x00,
  0xE0, 0x00, 0xFF, 0xF0, 0x00,
  0xE0, 0x00, 0xFF, 0xFC, 0x00,
  0xF0, 0x03, 0xFF, 0xFF, 0x00,
  0xF8, 0x07, 0xFF, 0xFF, 0x80,
  0xFC, 0x0F, 0xFF, 0xF3, 0xC0,
  0xFE, 0x1F, 0xFF, 0xE9, 0xE0,
  0xFF, 0xFF, 0xFF, 0xE1, 0xE0,
  0xFF, 0xFF, 0xFF, 0xF3, 0xE0,
  0xFF, 0xFF, 0xFF, 0xFF, 0xF0,
  0xFF, 0xFF, 0xF3, 0xD7, 0xE0,
  0xFC, 0x1F, 0xCB, 0xEE, 0xC0,
  0xF0, 0x1F, 0x3B, 0xFF, 0x20,
  0x80, 0x0C, 0x3B, 0xFF, 0xC0,
  0x00, 0x07, 0xC3, 0xFF, 0x80,
  0x00, 0x03, 0xFF, 0xFF, 0x00,
  0x00, 0x00, 0xFF, 0xFC, 0x00,
  0x00, 0x00, 0x0F, 0xC0, 0x00,
};

const unsigned char fishR_normal_1[] PROGMEM = {
  0x00, 0x00, 0x7C, 0xE0, 0x00,
  0x00, 0x00, 0xFF, 0xF0, 0x00,
  0x00, 0x00, 0xFF, 0xFC, 0x00,
  0x00, 0x03, 0xFF, 0xFF, 0x00,
  0x80, 0x07, 0xFF, 0xFF, 0x80,
  0xF0, 0x0F, 0xFF, 0xF3, 0xC0,
  0xFC, 0x1F, 0xFF, 0xE9, 0xE0,
  0xFF, 0xFF, 0xFF, 0xE1, 0xE0,
  0xFF, 0xFF, 0xFF, 0xF3, 0xE0,
  0xFF, 0xFC, 0x3F, 0xFF, 0xF0,
  0xFF, 0xFE, 0x43, 0xD7, 0xE0,
  0xFE, 0x1F, 0xBB, 0xEE, 0xC0,
  0xFC, 0x1F, 0xDB, 0xFF, 0x20,
  0xF8, 0x0F, 0xE3, 0xFF, 0xC0,
  0xF0, 0x07, 0xFB, 0xFF, 0x80,
  0xE0, 0x03, 0xFF, 0xFF, 0x00,
  0xE0, 0x00, 0xFF, 0xFC, 0x00,
  0xC0, 0x00, 0x0F, 0xC0, 0x00,
};

const unsigned char fishR_happy_0[] PROGMEM = {
  0xC0, 0x00, 0x7C, 0xE0, 0x00,
  0xE0, 0x00, 0xFF, 0xF0, 0x00,
  0xE0, 0x00, 0xFF, 0xFC, 0x00,
  0xF0, 0x03, 0xFF, 0xFF, 0x00,
  0xF8, 0x07, 0xFF, 0xFF, 0x80,
  0xFC, 0x0F, 0xFF, 0xFB, 0xC0,
  0xFE, 0x1F, 0xFF, 0xF5, 0xE0,
  0xFF, 0xFF, 0xFF, 0xEE, 0xE0,
  0xFF, 0xFF, 0xFF, 0xFF, 0xE0,
  0xFF, 0xFF, 0xFF, 0xFF, 0xF0,
  0xFF, 0xFF, 0xF3, 0xD7, 0xE0,
  0xFC, 0x1F, 0xCB, 0xEE, 0x00,
  0xF0, 0x1F, 0x3B, 0xFF, 0x20,
  0x80, 0x0C, 0x3B, 0xFF, 0xC0,
  0x00, 0x07, 0xC3, 0xFF, 0x80,
  0x00, 0x03, 0xFF, 0xFF, 0x00,
  0x00, 0x00, 0xFF, 0xFC, 0x00,
  0x00, 0x00, 0x0F, 0xC0, 0x00,
};

const unsigned char fishR_happy_1[] PROGMEM = {
  0x00, 0x00, 0x7C, 0xE0, 0x00,
  0x00, 0x00, 0xFF, 0xF0, 0x00,
  0x00, 0x00, 0xFF, 0xFC, 0x00,
  0x00, 0x03, 0xFF, 0xFF, 0x00,
  0x80, 0x07, 0xFF, 0xFF, 0x80,
  0xF0, 0x0F, 0xFF, 0xFB, 0xC0,
  0xFC, 0x1F, 0xFF, 0xF5, 0xE0,
  0xFF, 0xFF, 0xFF, 0xEE, 0xE0,
  0xFF, 0xFF, 0xFF, 0xFF, 0xE0,
  0xFF, 0xFC, 0x3F, 0xFF, 0xF0,
  0xFF, 0xFE, 0x43, 0xD7, 0xE0,
  0xFE, 0x1F, 0xBB, 0xEE, 0x00,
  0xFC, 0x1F, 0xDB, 0xFF, 0x20,
  0xF8, 0x0F, 0xE3, 0xFF, 0xC0,
  0xF0, 0x07, 0xFB, 0xFF, 0x80,
  0xE0, 0x03, 0xFF, 0xFF, 0x00,
  0xE0, 0x00, 0xFF, 0xFC, 0x00,
  0xC0, 0x00, 0x0F, 0xC0, 0x00,
};

const unsigned char fishR_angry_0[] PROGMEM = {
  0xC0, 0x00, 0x7C, 0xE0, 0x00,
  0xE0, 0x00, 0xFF, 0xF0, 0x00,
  0xE0, 0x00, 0xFF, 0xBC, 0x00,
  0xF0, 0x03, 0xFF, 0xCF, 0x00,
  0xF8, 0x07, 0xFF, 0xF3, 0x80,
  0xFC, 0x0F, 0xFF, 0xF0, 0xC0,
  0xFE, 0x1F, 0xFF, 0xE1, 0xE0,
  0xFF, 0xFF, 0xFF, 0xE1, 0xE0,
  0xFF, 0xFF, 0xFF, 0xF3, 0xE0,
  0xFF, 0xFF, 0xFF, 0xFF, 0xF0,
  0xFF, 0xFF, 0xF3, 0xFF, 0xE0,
  0xFC, 0x1F, 0xCB, 0xFF, 0x20,
  0xF0, 0x1F, 0x3B, 0xFE, 0xC0,
  0x80, 0x0C, 0x3B, 0xFF, 0xC0,
  0x00, 0x07, 0xC3, 0xFF, 0x80,
  0x00, 0x03, 0xFF, 0xFF, 0x00,
  0x00, 0x00, 0xFF, 0xFC, 0x00,
  0x00, 0x00, 0x0F, 0xC0, 0x00,
};

const unsigned char fishR_angry_1[] PROGMEM = {
  0x00, 0x00, 0x7C, 0xE0, 0x00,
  0x00, 0x00, 0xFF, 0xF0, 0x00,
  0x00, 0x00, 0xFF, 0xBC, 0x00,
  0x00, 0x03, 0xFF, 0xCF, 0x00,
  0x80, 0x07, 0xFF, 0xF3, 0x80,
  0xF0, 0x0F, 0xFF, 0xF0, 0xC0,
  0xFC, 0x1F, 0xFF, 0xE1, 0xE0,
  0xFF, 0xFF, 0xFF, 0xE1, 0xE0,
  0xFF, 0xFF, 0xFF, 0xF3, 0xE0,
  0xFF, 0xFC, 0x3F, 0xFF, 0xF0,
  0xFF, 0xFE, 0x43, 0xFF, 0xE0,
  0xFE, 0x1F, 0xBB, 0xFF, 0x20,
  0xFC, 0x1F, 0xDB, 0xFE, 0xC0,
  0xF8, 0x0F, 0xE3, 0xFF, 0xC0,
  0xF0, 0x07, 0xFB, 0xFF, 0x80,
  0xE0, 0x03, 0xFF, 0xFF, 0x00,
  0xE0, 0x00, 0xFF, 0xFC, 0x00,
  0xC0, 0x00, 0x0F, 0xC0, 0x00,
};

const unsigned char fishR_eat_0[] PROGMEM = {
  0xC0, 0x00, 0x7C, 0xE0, 0x00,
  0xE0, 0x00, 0xFF, 0xF0, 0x00,
  0xE0, 0x00, 0xFF, 0xFC, 0x00,
  0xF0, 0x03, 0xFF, 0xFF, 0x00,
  0xF8, 0x07, 0xFF, 0xFF, 0x80,
  0xFC, 0x0F, 0xFF, 0xF3, 0xC0,
  0xFE, 0x1F, 0xFF, 0xE9, 0xE0,
  0xFF, 0xFF, 0xFF, 0xE1, 0xE0,
  0xFF, 0xFF, 0xFF, 0xF3, 0xE0,
  0xFF, 0xFF, 0xFF, 0xFF, 0xF0,
  0xFF, 0xFF, 0xF3, 0xD7, 0x00,
  0xFC, 0x1F, 0xCB, 0xEF, 0x00,
  0xF0, 0x1F, 0x3B, 0xFF, 0x00,
  0x80, 0x0C, 0x3B, 0xFF, 0x00,
  0x00, 0x07, 0xC3, 0xFF, 0x80,
  0x00, 0x03, 0xFF, 0xFF, 0x00,
  0x00, 0x00, 0xFF, 0xFC, 0x00,
  0x00, 0x00, 0x0F, 0xC0, 0x00,
};

const unsigned char fishR_eat_1[] PROGMEM = {
  0x00, 0x00, 0x7C, 0xE0, 0x00,
  0x00, 0x00, 0xFF, 0xF0, 0x00,
  0x00, 0x00, 0xFF, 0xFC, 0x00,
  0x00, 0x03, 0xFF, 0xFF, 0x00,
  0x80, 0x07, 0xFF, 0xFF, 0x80,
  0xF0, 0x0F, 0xFF, 0xF3, 0xC0,
  0xFC, 0x1F, 0xFF, 0xE9, 0xE0,
  0xFF, 0xFF, 0xFF, 0xE1, 0xE0,
  0xFF, 0xFF, 0xFF, 0xF3, 0xE0,
  0xFF, 0xFC, 0x3F, 0xFF, 0xF0,
  0xFF, 0xFE, 0x43, 0xD7, 0x00,
  0xFE, 0x1F, 0xBB, 0xEF, 0x00,
  0xFC, 0x1F, 0xDB, 0xFF, 0x00,
  0xF8, 0x0F, 0xE3, 0xFF, 0x00,
  0xF0, 0x07, 0xFB, 0xFF, 0x80,
  0xE0, 0x03, 0xFF, 0xFF, 0x00,
  0xE0, 0x00, 0xFF, 0xFC, 0x00,
  0xC0, 0x00, 0x0F, 0xC0, 0x00,
};

const unsigned char maskR_0[] PROGMEM = {
  0xF0, 0x01, 0xFF, 0xF8, 0x00,
  0xF0, 0x01, 0xFF, 0xFE, 0x00,
  0xF8, 0x07, 0xFF, 0xFF, 0x80,
  0xFC, 0x0F, 0xFF, 0xFF, 0xC0,
  0xFE, 0x1F, 0xFF, 0xFF, 0xE0,
  0xFF, 0x3F, 0xFF, 0xFF, 0xF0,
  0xFF, 0xFF, 0xFF, 0xFF, 0xF0,
  0xFF, 0xFF, 0xFF, 0xFF, 0xF0,
  0xFF, 0xFF, 0xFF, 0xFF, 0xF0,
  0xFF, 0xFF, 0xFF, 0xFF, 0xF0,
  0xFF, 0xFF, 0xFF, 0xFF, 0xF0,
  0xFF, 0xFF, 0xFF, 0xFF, 0xF0,
  0xFE, 0x3F, 0xFF, 0xFF, 0xF0,
  0xF8, 0x3F, 0xFF, 0xFF, 0xF0,
  0xC0, 0x1F, 0xFF, 0xFF, 0xE0,
  0x00, 0x0F, 0xFF, 0xFF, 0xC0,
  0x00, 0x07, 0xFF, 0xFF, 0x80,
  0x00, 0x01, 0xFF, 0xFE, 0x00,
};

const unsigned char maskR_1[] PROGMEM = {
  0x00, 0x01, 0xFF, 0xF8, 0x00,
  0x00, 0x01, 0xFF, 0xFE, 0x00,
  0x00, 0x07, 0xFF, 0xFF, 0x80,
  0xC0, 0x0F, 0xFF, 0xFF, 0xC0,
  0xF8, 0x1F, 0xFF, 0xFF, 0xE0,
  0xFE, 0x3F, 0xFF, 0xFF, 0xF0,
  0xFF, 0xFF, 0xFF, 0xFF, 0xF0,
  0xFF, 0xFF, 0xFF, 0xFF, 0xF0,
  0xFF, 0xFF, 0xFF, 0xFF, 0xF0,
  0xFF, 0xFF, 0xFF, 0xFF, 0xF0,
  0xFF, 0xFF, 0xFF, 0xFF, 0xF0,
  0xFF, 0xFF, 0xFF, 0xFF, 0xF0,
  0xFF, 0x3F, 0xFF, 0xFF, 0xF0,
  0xFE, 0x3F, 0xFF, 0xFF, 0xF0,
  0xFC, 0x1F, 0xFF, 0xFF, 0xE0,
  0xF8, 0x0F, 0xFF, 0xFF, 0xC0,
  0xF0, 0x07, 0xFF, 0xFF, 0x80,
  0xF0, 0x01, 0xFF, 0xFE, 0x00,
};

const unsigned char fishL_normal_0[] PROGMEM = {
  0x00, 0x73, 0xE0, 0x00, 0x30,
  0x00, 0xFF, 0xF0, 0x00, 0x70,
  0x03, 0xFF, 0xF0, 0x00, 0x70,
  0x0F, 0xFF, 0xFC, 0x00, 0xF0,
  0x1F, 0xFF, 0xFE, 0x01, 0xF0,
  0x3C, 0xFF, 0xFF, 0x03, 0xF0,
  0x79, 0x7F, 0xFF, 0x87, 0xF0,
  0x78, 0x7F, 0xFF, 0xFF, 0xF0,
  0x7C, 0xFF, 0xFF, 0xFF, 0xF0,
  0xFF, 0xFF, 0xFF, 0xFF, 0xF0,
  0x7E, 0xBC, 0xFF, 0xFF, 0xF0,
  0x37, 0x7D, 0x3F, 0x83, 0xF0,
  0x4F, 0xFD, 0xCF, 0x80, 0xF0,
  0x3F, 0xFD, 0xC3, 0x00, 0x10,
  0x1F, 0xFC, 0x3E, 0x00, 0x00,
  0x0F, 0xFF, 0xFC, 0x00, 0x00,
  0x03, 0xFF, 0xF0, 0x00, 0x00,
  0x00, 0x3F, 0x00, 0x00, 0x00,
};

const unsigned char fishL_normal_1[] PROGMEM = {
  0x00, 0x73, 0xE0, 0x00, 0x00,
  0x00, 0xFF, 0xF0, 0x00, 0x00,
  0x03, 0xFF, 0xF0, 0x00, 0x00,
  0x0F, 0xFF, 0xFC, 0x00, 0x00,
  0x1F, 0xFF, 0xFE, 0x00, 0x10,
  0x3C, 0xFF, 0xFF, 0x00, 0xF0,
  0x79, 0x7F, 0xFF, 0x83, 0xF0,
  0x78, 0x7F, 0xFF, 0xFF, 0xF0,
  0x7C, 0xFF, 0xFF, 0xFF, 0xF0,
  0xFF, 0xFF, 0xC3, 0xFF, 0xF0,
  0x7E, 0xBC, 0x27, 0xFF, 0xF0,
  0x37, 0x7D, 0xDF, 0x87, 0xF0,
  0x4F, 0xFD, 0xBF, 0x83, 0xF0,
  0x3F, 0xFC, 0x7F, 0x01, 0xF0,
  0x1F, 0xFD, 0xFE, 0x00, 0xF0,
  0x0F, 0xFF, 0xFC, 0x00, 0x70,
  0x03, 0xFF, 0xF0, 0x00, 0x70,
  0x00, 0x3F, 0x00, 0x00, 0x30,
};

const unsigned char fishL_happy_0[] PROGMEM = {
  0x00, 0x73, 0xE0, 0x00, 0x30,
  0x00, 0xFF, 0xF0, 0x00, 0x70,
  0x03, 0xFF, 0xF0, 0x00, 0x70,
  0x0F, 0xFF, 0xFC, 0x00, 0xF0,
  0x1F, 0xFF, 0xFE, 0x01, 0xF0,
  0x3D, 0xFF, 0xFF, 0x03, 0xF0,
  0x7A, 0xFF, 0xFF, 0x87, 0xF0,
  0x77, 0x7F, 0xFF, 0xFF, 0xF0,
  0x7F, 0xFF, 0xFF, 0xFF, 0xF0,
  0xFF, 0xFF, 0xFF, 0xFF, 0xF0,
  0x7E, 0xBC, 0xFF, 0xFF, 0xF0,
  0x07, 0x7D, 0x3F, 0x83, 0xF0,
  0x4F, 0xFD, 0xCF, 0x80, 0xF0,
  0x3F, 0xFD, 0xC3, 0x00, 0x10,
  0x1F, 0xFC, 0x3E, 0x00, 0x00,
  0x0F, 0xFF, 0xFC, 0x00, 0x00,
  0x03, 0xFF, 0xF0, 0x00, 0x00,
  0x00, 0x3F, 0x00, 0x00, 0x00,
};

const unsigned char fishL_happy_1[] PROGMEM = {
  0x00, 0x73, 0xE0, 0x00, 0x00,
  0x00, 0xFF, 0xF0, 0x00, 0x00,
  0x03, 0xFF, 0xF0, 0x00, 0x00,
  0x0F, 0xFF, 0xFC, 0x00, 0x00,
  0x1F, 0xFF, 0xFE, 0x00, 0x10,
  0x3D, 0xFF, 0xFF, 0x00, 0xF0,
  0x7A, 0xFF, 0xFF, 0x83, 0xF0,
  0x77, 0x7F, 0xFF, 0xFF, 0xF0,
  0x7F, 0xFF, 0xFF, 0xFF, 0xF0,
  0xFF, 0xFF, 0xC3, 0xFF, 0xF0,
  0x7E, 0xBC, 0x27, 0xFF, 0xF0,
  0x07, 0x7D, 0xDF, 0x87, 0xF0,
  0x4F, 0xFD, 0xBF, 0x83, 0xF0,
  0x3F, 0xFC, 0x7F, 0x01, 0xF0,
  0x1F, 0xFD, 0xFE, 0x00, 0xF0,
  0x0F, 0xFF, 0xFC, 0x00, 0x70,
  0x03, 0xFF, 0xF0, 0x00, 0x70,
  0x00, 0x3F, 0x00, 0x00, 0x30,
};

const unsigned char fishL_angry_0[] PROGMEM = {
  0x00, 0x73, 0xE0, 0x00, 0x30,
  0x00, 0xFF, 0xF0, 0x00, 0x70,
  0x03, 0xDF, 0xF0, 0x00, 0x70,
  0x0F, 0x3F, 0xFC, 0x00, 0xF0,
  0x1C, 0xFF, 0xFE, 0x01, 0xF0,
  0x30, 0xFF, 0xFF, 0x03, 0xF0,
  0x78, 0x7F, 0xFF, 0x87, 0xF0,
  0x78, 0x7F, 0xFF, 0xFF, 0xF0,
  0x7C, 0xFF, 0xFF, 0xFF, 0xF0,
  0xFF, 0xFF, 0xFF, 0xFF, 0xF0,
  0x7F, 0xFC, 0xFF, 0xFF, 0xF0,
  0x4F, 0xFD, 0x3F, 0x83, 0xF0,
  0x37, 0xFD, 0xCF, 0x80, 0xF0,
  0x3F, 0xFD, 0xC3, 0x00, 0x10,
  0x1F, 0xFC, 0x3E, 0x00, 0x00,
  0x0F, 0xFF, 0xFC, 0x00, 0x00,
  0x03, 0xFF, 0xF0, 0x00, 0x00,
  0x00, 0x3F, 0x00, 0x00, 0x00,
};

const unsigned char fishL_angry_1[] PROGMEM = {
  0x00, 0x73, 0xE0, 0x00, 0x00,
  0x00, 0xFF, 0xF0, 0x00, 0x00,
  0x03, 0xDF, 0xF0, 0x00, 0x00,
  0x0F, 0x3F, 0xFC, 0x00, 0x00,
  0x1C, 0xFF, 0xFE, 0x00, 0x10,
  0x30, 0xFF, 0xFF, 0x00, 0xF0,
  0x78, 0x7F, 0xFF, 0x83, 0xF0,
  0x78, 0x7F, 0xFF, 0xFF, 0xF0,
  0x7C, 0xFF, 0xFF, 0xFF, 0xF0,
  0xFF, 0xFF, 0xC3, 0xFF, 0xF0,
  0x7F, 0xFC, 0x27, 0xFF, 0xF0,
  0x4F, 0xFD, 0xDF, 0x87, 0xF0,
  0x37, 0xFD, 0xBF, 0x83, 0xF0,
  0x3F, 0xFC, 0x7F, 0x01, 0xF0,
  0x1F, 0xFD, 0xFE, 0x00, 0xF0,
  0x0F, 0xFF, 0xFC, 0x00, 0x70,
  0x03, 0xFF, 0xF0, 0x00, 0x70,
  0x00, 0x3F, 0x00, 0x00, 0x30,
};

const unsigned char fishL_eat_0[] PROGMEM = {
  0x00, 0x73, 0xE0, 0x00, 0x30,
  0x00, 0xFF, 0xF0, 0x00, 0x70,
  0x03, 0xFF, 0xF0, 0x00, 0x70,
  0x0F, 0xFF, 0xFC, 0x00, 0xF0,
  0x1F, 0xFF, 0xFE, 0x01, 0xF0,
  0x3C, 0xFF, 0xFF, 0x03, 0xF0,
  0x79, 0x7F, 0xFF, 0x87, 0xF0,
  0x78, 0x7F, 0xFF, 0xFF, 0xF0,
  0x7C, 0xFF, 0xFF, 0xFF, 0xF0,
  0xFF, 0xFF, 0xFF, 0xFF, 0xF0,
  0x0E, 0xBC, 0xFF, 0xFF, 0xF0,
  0x0F, 0x7D, 0x3F, 0x83, 0xF0,
  0x0F, 0xFD, 0xCF, 0x80, 0xF0,
  0x0F, 0xFD, 0xC3, 0x00, 0x10,
  0x1F, 0xFC, 0x3E, 0x00, 0x00,
  0x0F, 0xFF, 0xFC, 0x00, 0x00,
  0x03, 0xFF, 0xF0, 0x00, 0x00,
  0x00, 0x3F, 0x00, 0x00, 0x00,
};

const unsigned char fishL_eat_1[] PROGMEM = {
  0x00, 0x73, 0xE0, 0x00, 0x00,
  0x00, 0xFF, 0xF0, 0x00, 0x00,
  0x03, 0xFF, 0xF0, 0x00, 0x00,
  0x0F, 0xFF, 0xFC, 0x00, 0x00,
  0x1F, 0xFF, 0xFE, 0x00, 0x10,
  0x3C, 0xFF, 0xFF, 0x00, 0xF0,
  0x79, 0x7F, 0xFF, 0x83, 0xF0,
  0x78, 0x7F, 0xFF, 0xFF, 0xF0,
  0x7C, 0xFF, 0xFF, 0xFF, 0xF0,
  0xFF, 0xFF, 0xC3, 0xFF, 0xF0,
  0x0E, 0xBC, 0x27, 0xFF, 0xF0,
  0x0F, 0x7D, 0xDF, 0x87, 0xF0,
  0x0F, 0xFD, 0xBF, 0x83, 0xF0,
  0x0F, 0xFC, 0x7F, 0x01, 0xF0,
  0x1F, 0xFD, 0xFE, 0x00, 0xF0,
  0x0F, 0xFF, 0xFC, 0x00, 0x70,
  0x03, 0xFF, 0xF0, 0x00, 0x70,
  0x00, 0x3F, 0x00, 0x00, 0x30,
};

const unsigned char maskL_0[] PROGMEM = {
  0x01, 0xFF, 0xF8, 0x00, 0xF0,
  0x07, 0xFF, 0xF8, 0x00, 0xF0,
  0x1F, 0xFF, 0xFE, 0x01, 0xF0,
  0x3F, 0xFF, 0xFF, 0x03, 0xF0,
  0x7F, 0xFF, 0xFF, 0x87, 0xF0,
  0xFF, 0xFF, 0xFF, 0xCF, 0xF0,
  0xFF, 0xFF, 0xFF, 0xFF, 0xF0,
  0xFF, 0xFF, 0xFF, 0xFF, 0xF0,
  0xFF, 0xFF, 0xFF, 0xFF, 0xF0,
  0xFF, 0xFF, 0xFF, 0xFF, 0xF0,
  0xFF, 0xFF, 0xFF, 0xFF, 0xF0,
  0xFF, 0xFF, 0xFF, 0xFF, 0xF0,
  0xFF, 0xFF, 0xFF, 0xC7, 0xF0,
  0xFF, 0xFF, 0xFF, 0xC1, 0xF0,
  0x7F, 0xFF, 0xFF, 0x80, 0x30,
  0x3F, 0xFF, 0xFF, 0x00, 0x00,
  0x1F, 0xFF, 0xFE, 0x00, 0x00,
  0x07, 0xFF, 0xF8, 0x00, 0x00,
};

const unsigned char maskL_1[] PROGMEM = {
  0x01, 0xFF, 0xF8, 0x00, 0x00,
  0x07, 0xFF, 0xF8, 0x00, 0x00,
  0x1F, 0xFF, 0xFE, 0x00, 0x00,
  0x3F, 0xFF, 0xFF, 0x00, 0x30,
  0x7F, 0xFF, 0xFF, 0x81, 0xF0,
  0xFF, 0xFF, 0xFF, 0xC7, 0xF0,
  0xFF, 0xFF, 0xFF, 0xFF, 0xF0,
  0xFF, 0xFF, 0xFF, 0xFF, 0xF0,
  0xFF, 0xFF, 0xFF, 0xFF, 0xF0,
  0xFF, 0xFF, 0xFF, 0xFF, 0xF0,
  0xFF, 0xFF, 0xFF, 0xFF, 0xF0,
  0xFF, 0xFF, 0xFF, 0xFF, 0xF0,
  0xFF, 0xFF, 0xFF, 0xCF, 0xF0,
  0xFF, 0xFF, 0xFF, 0xC7, 0xF0,
  0x7F, 0xFF, 0xFF, 0x83, 0xF0,
  0x3F, 0xFF, 0xFF, 0x01, 0xF0,
  0x1F, 0xFF, 0xFE, 0x00, 0xF0,
  0x07, 0xFF, 0xF8, 0x00, 0xF0,
};

enum Expr { EX_NORMAL = 0, EX_HAPPY = 1, EX_ANGRY = 2, EX_EAT = 3 };

// [arah: 0 kanan, 1 kiri][ekspresi][frame]
const unsigned char* const FISH_BMP[2][4][2] = {
  { {fishR_normal_0, fishR_normal_1}, {fishR_happy_0, fishR_happy_1},
    {fishR_angry_0,  fishR_angry_1},  {fishR_eat_0,   fishR_eat_1} },
  { {fishL_normal_0, fishL_normal_1}, {fishL_happy_0, fishL_happy_1},
    {fishL_angry_0,  fishL_angry_1},  {fishL_eat_0,   fishL_eat_1} }
};
const unsigned char* const FISH_MASK[2][2] = {
  { maskR_0, maskR_1 },
  { maskL_0, maskL_1 }
};

// batas gerak ikan (batas atas dinamis: lihat fishMinY())
#define FISH_MIN_X 0
#define FISH_MAX_X (SCREEN_WIDTH - FISH_W)
#define FISH_MAX_Y (SCREEN_HEIGHT - FISH_H - 2)
#define FOOD_FLOOR 54

// ================= STATE: IKAN & GAME =================
enum Mode { SWIM, HAPPY, ANGRY, EATING, GAME_READY, GAME_PLAY, GAME_OVER };
Mode mode = SWIM;
unsigned long modeStart = 0;

inline bool inGame() { return mode == GAME_READY || mode == GAME_PLAY || mode == GAME_OVER; }

struct Fish {
  float x, y;      // posisi kiri-atas bitmap
  float tx, ty;    // target tujuan
  int dir;         // 1 = kanan, -1 = kiri
};
Fish fish = {46, 22, 46, 22, 1};

unsigned long nextTargetTime = 0;
unsigned long lastFishFrame = 0;
uint8_t fishFrame = 0;
unsigned long lastFrameTime = 0;

#define MAX_BUBBLES 6
struct Bubble { float x, y, speed; uint8_t r; bool active; };
Bubble bubbles[MAX_BUBBLES];
unsigned long lastHappyBubble = 0;

#define MAX_FOOD 4
struct Food { float x, y; bool active; };
Food foods[MAX_FOOD];

unsigned long lastChew = 0;
const int chewInterval = 250;
bool mouthOpen = false;
const unsigned long eatDuration = 800;
const unsigned long happyDuration = 3000;
const unsigned long angryDuration = 3000;

// ---- mini game: flappy fish (1 unit waktu = 16 ms) ----
#define MAX_PIPES 3
struct Pipe { float x; int gapY; bool passed; bool active; };
Pipe pipes[MAX_PIPES];

const int   GAME_FISH_X  = 20;
const float GAME_G       = 0.16f;   // gravitasi
const float GAME_FLAP    = -1.8f;   // dorongan ke atas tiap tekan
const float GAME_VMAX    = 3.5f;    // kecepatan jatuh maksimum
const float GAME_SPEED   = 0.95f;   // kecepatan pipa
const int   GAME_GAP     = 38;      // tinggi celah pipa
const int   PIPE_W       = 12;
const int   PIPE_SPACING = 80;      // jarak antar pipa
const int   GAME_FLOOR   = 61;
const int HB_X0 = 12, HB_X1 = 32, HB_Y0 = 4, HB_Y1 = 14;   // hitbox (koordinat bitmap)

float gameY = 22, gameVy = 0;
int   score = 0, bestScore = 0;

// ================= STATE: TOMBOL =================
bool btnDown[4]   = {false, false, false, false};
bool btnPrev[4]   = {false, false, false, false};
bool evPress[4], evShort[4], evLong[4];       // event per frame
bool longFired[4] = {false, false, false, false};
unsigned long btnPressStart[4] = {0, 0, 0, 0};
const unsigned long LONG_PRESS_MS = 900;
int pressedIdx = -1;             // tombol yang lagi ditekan (-1 = gak ada)

// ================= STATE: UI =================
enum Ui { UI_AQUARIUM, UI_MENU, UI_STATUS, UI_TODO };
Ui ui = UI_AQUARIUM;
unsigned long uiLastInput = 0;

enum DisplayMode { DM_DEFAULT = 0, DM_TEXT = 1 };
uint8_t displayMode = DM_DEFAULT;
char scrollText[81] = DEFAULT_TEXT;

#define MENU_COUNT 3
const char* MENU_ITEMS[MENU_COUNT] = { "Default", "Tulisan berjalan", "Status ikan" };
int menuCursor = 0;

int topRes = 0;                  // tinggi area atas yang dipakai strip (pomodoro/musik/tulisan)

// ---- overlay teks (maks 2 baris) ----
bool showOverlay = false;
String overlayLine1 = "";
String overlayLine2 = "";
unsigned long overlayStart = 0;
const unsigned long overlayDuration = 2500;

// ---- reminder makan ----
const char* mealNames[3] = {"Sarapan", "Makan Siang", "Makan Sore"};
int mealIndex = 0;
unsigned long nextReminderTime;
bool reminderActive = false;
const unsigned long mealResetInterval = 4UL * 60 * 60 * 1000;   // 4 jam
const unsigned long snoozeInterval = 30UL * 60 * 1000;          // snooze normal: 30 menit
const unsigned long shortSnoozeInterval = 5UL * 60 * 1000;      // snooze ke-3: 5 menit aja
int snoozeCount = 0;
const int maxSnoozeBeforeAngry = 3;

// ================= STATE: TODO =================
#define MAX_TODO 8
#define TODO_TEXT_MAX 20
struct Todo { uint8_t id; bool done; char text[TODO_TEXT_MAX + 1]; };
Todo todos[MAX_TODO];
int todoN = 0;
uint8_t nextTodoId = 1;
uint8_t todoVersion = 0;
int todoCursor = 0, todoScroll = 0;
#define TODO_ROWS 5

// ================= STATE: STATISTIK =================
#define STATS_MAGIC 0x4D4E4348UL
struct Stats {
  uint32_t magic;
  uint32_t lastDay;        // hari terakhir yang tercatat (epoch/86400), 0 = belum pernah sinkron
  uint32_t lastPomoDay;    // hari terakhir ada pomodoro selesai
  uint32_t eatTotal;
  uint32_t pomoTotal;
  uint16_t streak;         // beruntun hari dengan >= 1 pomodoro
  uint16_t eatToday;       // pelet yang dimakan ikan hari ini
  uint16_t pomoWeek[7];    // [0] = hari ini ... [6] = 6 hari lalu
};
Stats stats;
bool statsDirty = false;
unsigned long lastStatsFlush = 0;
unsigned long lastDayCheck = 0;

bool timeSynced = false;
uint32_t epochAtSync = 0;        // jam lokal (detik sejak 1970) saat sinkron
unsigned long msAtSync = 0;

// ================= STATE: POMODORO =================
enum PomoPhase { PH_FOCUS = 0, PH_BREAK = 1, PH_LONG = 2 };
bool pomoView = false;           // tampilan pomodoro lagi kebuka
PomoPhase pomoPhase = PH_FOCUS;
bool pomoRunning = false;
unsigned long pomoEndMs = 0;     // kapan fase berakhir (kalau lagi jalan)
unsigned long pomoLeftMs = POMO_FOCUS_MS;   // sisa waktu (kalau lagi jeda)
int pomoCycle = 0;               // jumlah fokus selesai di set ini (0..POMO_SETS-1)

// ================= STATE: MUSIK =================
struct Media { uint8_t state; uint16_t pos; uint16_t dur; char title[80]; unsigned long recvMs; };
Media media = {0, 0, 0, "", 0};   // state: 0 berhenti, 1 main, 2 jeda

// ================= STATE: NADA BUZZER =================
struct Note { uint16_t f; uint16_t ms; };
const Note MEL_POMO_DONE[] = { {784, 120}, {988, 120}, {1175, 120}, {1568, 320} };
const Note MEL_BREAK_END[] = { {1175, 150}, {988, 150}, {784, 260} };
const Note* melody = nullptr;
uint8_t melodyLen = 0, melodyIdx = 0;
unsigned long melodyNext = 0;

// ================= STATE: BLE =================
volatile bool bleConnected = false;
bool lastBleConnected = false;

#define CMD_TODO  1
#define CMD_TEXT  2
#define CMD_MODE  3
#define CMD_TIME  4
#define CMD_MEDIA 5
struct Cmd { uint8_t type; uint8_t len; uint8_t data[96]; };
#define CMD_Q 8
Cmd cmdQ[CMD_Q];
volatile uint8_t qHead = 0, qTail = 0;   // callback BLE nulis, loop() yang baca

Preferences prefs;

// ================= HELPER UMUM =================
void showMessage(String line1, String line2 = "") {
  showOverlay = true;
  overlayStart = millis();
  overlayLine1 = line1;
  overlayLine2 = line2;
}

void setMode(Mode m, unsigned long now);     // forward declaration (dipakai pomodoro)

// Font bawaan cuma ASCII. Karakter non-ASCII (UTF-8 multi-byte) diganti satu '?'.
size_t sanitizeAscii(char* dst, const uint8_t* src, size_t n, size_t maxOut) {
  size_t o = 0;
  bool lastQ = false;
  for (size_t i = 0; i < n && o < maxOut; i++) {
    uint8_t c = src[i];
    if (c >= 32 && c <= 126) { dst[o++] = (char)c; lastQ = false; }
    else if (c >= 0x80)      { if (!lastQ) { dst[o++] = '?'; lastQ = true; } }
    // karakter kontrol dibuang
  }
  dst[o] = 0;
  return o;
}

void put16(uint8_t* p, uint16_t v) { p[0] = v & 0xFF; p[1] = v >> 8; }
void put32(uint8_t* p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }

void playMelody(const Note* m, uint8_t n) { melody = m; melodyLen = n; melodyIdx = 0; melodyNext = 0; }

void updateMelody(unsigned long now) {
  if (!melody || now < melodyNext) return;
  if (melodyIdx >= melodyLen) { melody = nullptr; return; }
  tone(BUZZER_PIN, melody[melodyIdx].f, melody[melodyIdx].ms);
  melodyNext = now + melody[melodyIdx].ms + 40;
  melodyIdx++;
}

// ================= TODO: DATA =================
size_t buildTodoBlob(uint8_t* out) {           // [jumlah][id, done, len, teks...]*
  size_t p = 1;
  for (int i = 0; i < todoN; i++) {
    uint8_t l = strlen(todos[i].text);
    out[p++] = todos[i].id;
    out[p++] = todos[i].done ? 1 : 0;
    out[p++] = l;
    memcpy(out + p, todos[i].text, l);
    p += l;
  }
  out[0] = todoN;
  return p;
}

void parseTodoBlob(const uint8_t* b, size_t n) {
  todoN = 0;
  nextTodoId = 1;
  if (n < 1) return;
  size_t p = 1;
  for (int i = 0; i < b[0] && i < MAX_TODO; i++) {
    if (p + 3 > n) break;
    Todo& t = todos[todoN];
    t.id = b[p++];
    t.done = b[p++] != 0;
    uint8_t l = b[p++];
    if (l > TODO_TEXT_MAX || p + l > n) break;
    memcpy(t.text, b + p, l);
    t.text[l] = 0;
    p += l;
    if (t.id >= nextTodoId) nextTodoId = t.id + 1;
    if (nextTodoId == 0) nextTodoId = 1;
    todoN++;
  }
}

int findTodo(uint8_t id) {
  for (int i = 0; i < todoN; i++) if (todos[i].id == id) return i;
  return -1;
}

// ================= STATISTIK & WAKTU =================
uint32_t nowDay() { return (epochAtSync + (millis() - msAtSync) / 1000) / 86400UL; }

uint16_t displayStreak() {
  if (timeSynced && stats.lastPomoDay != 0 && nowDay() > stats.lastPomoDay + 1) return 0;   // udah bolong
  return stats.streak;
}

size_t buildStatsBlob(uint8_t* out) {          // 25 byte
  out[0] = timeSynced ? 1 : 0;
  put16(out + 1, displayStreak());
  put16(out + 3, stats.pomoWeek[0]);
  put16(out + 5, stats.eatToday);
  put32(out + 7, stats.pomoTotal);
  for (int i = 0; i < 7; i++) put16(out + 11 + i * 2, stats.pomoWeek[i]);
  return 25;
}

// geser data mingguan kalau hari sudah berganti
void rollDays(uint32_t newDay) {
  if (stats.lastDay == 0) { stats.lastDay = newDay; statsDirty = true; return; }
  if (newDay <= stats.lastDay) return;                       // jam HP mundur -> abaikan
  uint32_t n = newDay - stats.lastDay;
  if (n > 7) n = 7;
  for (uint32_t i = 0; i < n; i++) {
    for (int k = 6; k > 0; k--) stats.pomoWeek[k] = stats.pomoWeek[k - 1];
    stats.pomoWeek[0] = 0;
    stats.eatToday = 0;
  }
  stats.lastDay = newDay;
  statsDirty = true;
}

// ================= PERSISTENSI (NVS) =================
void saveTodos() { uint8_t buf[200]; size_t n = buildTodoBlob(buf); prefs.putBytes("todo", buf, n); }
void saveText()  { prefs.putString("text", scrollText); }
void saveMode()  { prefs.putUChar("mode", displayMode); }
void saveStats() { prefs.putBytes("stats", &stats, sizeof(stats)); statsDirty = false; }
void saveBest()  { prefs.putUShort("best", (uint16_t)bestScore); }

void loadAll() {
  prefs.begin("munchfish", false);
  displayMode = prefs.getUChar("mode", DM_DEFAULT);
  if (displayMode > DM_TEXT) displayMode = DM_DEFAULT;

  String t = prefs.getString("text", DEFAULT_TEXT);
  strncpy(scrollText, t.c_str(), 80);
  scrollText[80] = 0;

  uint8_t buf[200];
  size_t n = prefs.getBytesLength("todo");
  if (n > 0 && n <= sizeof(buf)) { prefs.getBytes("todo", buf, n); parseTodoBlob(buf, n); }

  memset(&stats, 0, sizeof(stats));
  size_t sn = prefs.getBytesLength("stats");
  if (sn == sizeof(stats)) prefs.getBytes("stats", &stats, sn);
  if (stats.magic != STATS_MAGIC) { memset(&stats, 0, sizeof(stats)); stats.magic = STATS_MAGIC; }

  bestScore = prefs.getUShort("best", 0);
}

// ================= BLE =================
#if ENABLE_BLE
BLEServer* pServer = nullptr;
BLECharacteristic *chTodoCmd, *chTodoList, *chText, *chMode, *chTime, *chMedia, *chStats, *chEvent;

void pushCmd(uint8_t type, const uint8_t* d, size_t n) {
  uint8_t next = (qHead + 1) % CMD_Q;
  if (next == qTail || d == nullptr) return;                 // antrian penuh -> buang
  Cmd& c = cmdQ[qHead];
  c.type = type;
  c.len = (n > sizeof(c.data)) ? sizeof(c.data) : n;
  memcpy(c.data, d, c.len);
  qHead = next;
}

class ServerCb : public BLEServerCallbacks {
  void onConnect(BLEServer*) override    { bleConnected = true; }
  void onDisconnect(BLEServer*) override { bleConnected = false; BLEDevice::startAdvertising(); }
};

class WriteCb : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* c) override {
    uint8_t type = 0;
    if      (c == chTodoCmd) type = CMD_TODO;
    else if (c == chText)    type = CMD_TEXT;
    else if (c == chMode)    type = CMD_MODE;
    else if (c == chTime)    type = CMD_TIME;
    else if (c == chMedia)   type = CMD_MEDIA;
    if (type) pushCmd(type, c->getData(), c->getLength());
  }
};
static WriteCb writeCb;

void setBleValue(BLECharacteristic* c, const uint8_t* d, size_t n) { c->setValue((uint8_t*)d, n); }

void updateTodoValue()  { uint8_t b[200]; size_t n = buildTodoBlob(b); setBleValue(chTodoList, b, n); }
void updateStatsValue() { uint8_t b[32]; size_t n = buildStatsBlob(b); setBleValue(chStats, b, n); }
void updateTextValue()  { setBleValue(chText, (const uint8_t*)scrollText, strlen(scrollText)); }
void updateModeValue()  { setBleValue(chMode, &displayMode, 1); }

// notifikasi cuma kecil ("ada yang berubah"), datanya dibaca aplikasi lewat READ
// 1 = todo berubah (nilai = versi) | 2 = mode berubah | 3 = statistik berubah | 4 = pomodoro
void sendEvent(uint8_t type, uint8_t val) {
  if (!chEvent) return;
  uint8_t b[2] = { type, val };
  chEvent->setValue(b, 2);
  if (bleConnected) chEvent->notify();
}

void initBle() {
  BLEDevice::init("Munchfish");
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new ServerCb());
  BLEService* svc = pServer->createService(BLEUUID(SVC_UUID), 40);

  chTodoCmd  = svc->createCharacteristic(CH_TODO_CMD,  BLECharacteristic::PROPERTY_WRITE);
  chTodoList = svc->createCharacteristic(CH_TODO_LIST, BLECharacteristic::PROPERTY_READ);
  chText     = svc->createCharacteristic(CH_TEXT,      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE);
  chMode     = svc->createCharacteristic(CH_MODE,      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE);
  chTime     = svc->createCharacteristic(CH_TIME,      BLECharacteristic::PROPERTY_WRITE);
  chMedia    = svc->createCharacteristic(CH_MEDIA,     BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  chStats    = svc->createCharacteristic(CH_STATS,     BLECharacteristic::PROPERTY_READ);
  chEvent    = svc->createCharacteristic(CH_EVENT,     BLECharacteristic::PROPERTY_NOTIFY);
  chEvent->addDescriptor(new BLE2902());

  chTodoCmd->setCallbacks(&writeCb);
  chText->setCallbacks(&writeCb);
  chMode->setCallbacks(&writeCb);
  chTime->setCallbacks(&writeCb);
  chMedia->setCallbacks(&writeCb);

  updateTodoValue();
  updateStatsValue();
  updateTextValue();
  updateModeValue();

  svc->start();
  BLEAdvertising* adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(SVC_UUID);
  adv->setScanResponse(true);
  adv->setMinPreferred(0x06);
  BLEDevice::startAdvertising();
}
#else
void pushCmd(uint8_t, const uint8_t*, size_t) {}
void updateTodoValue() {}
void updateStatsValue() {}
void updateTextValue() {}
void updateModeValue() {}
void sendEvent(uint8_t, uint8_t) {}
void initBle() {}
#endif

// ================= PERINTAH DARI HP (diproses di loop) =================
void todoChanged() {
  saveTodos();
  updateTodoValue();
  todoVersion++;
  sendEvent(1, todoVersion);
  if (todoCursor >= todoN) todoCursor = todoN - 1;
  if (todoCursor < 0) todoCursor = 0;
}

// [0x01, teks...] tambah | [0x02, id] hapus | [0x03, id, done(0/1/2=biarkan), teks...] ubah | [0x04] hapus semua
void handleTodoCmd(const uint8_t* d, uint8_t n) {
  if (n < 1) return;
  switch (d[0]) {
    case 0x01: {
      if (todoN >= MAX_TODO || n < 2) return;
      Todo& t = todos[todoN];
      sanitizeAscii(t.text, d + 1, n - 1, TODO_TEXT_MAX);
      if (t.text[0] == 0) return;
      t.id = nextTodoId++;
      if (nextTodoId == 0) nextTodoId = 1;
      t.done = false;
      todoN++;
      todoChanged();
      break;
    }
    case 0x02: {
      if (n < 2) return;
      int i = findTodo(d[1]);
      if (i < 0) return;
      for (int k = i; k < todoN - 1; k++) todos[k] = todos[k + 1];
      todoN--;
      todoChanged();
      break;
    }
    case 0x03: {
      if (n < 3) return;
      int i = findTodo(d[1]);
      if (i < 0) return;
      if (d[2] <= 1) todos[i].done = d[2] == 1;
      if (n > 3) {
        char tmp[TODO_TEXT_MAX + 1];
        sanitizeAscii(tmp, d + 3, n - 3, TODO_TEXT_MAX);
        if (tmp[0]) strcpy(todos[i].text, tmp);
      }
      todoChanged();
      break;
    }
    case 0x04:
      todoN = 0;
      todoChanged();
      break;
  }
}

void setDisplayMode(uint8_t m) {
  if (m > DM_TEXT) return;
  displayMode = m;
  saveMode();
  updateModeValue();
  sendEvent(2, displayMode);
}

void syncTime(uint32_t localEpoch) {
  epochAtSync = localEpoch;
  msAtSync = millis();
  timeSynced = true;
  rollDays(localEpoch / 86400UL);
  statsDirty = true;
}

// [state 0/1/2][pos u16 detik][dur u16 detik][judul - artis ...]
void handleMedia(const uint8_t* d, uint8_t n, unsigned long now) {
  if (n < 5 || d[0] > 2) return;
  media.state = d[0];
  media.pos = d[1] | (d[2] << 8);
  media.dur = d[3] | (d[4] << 8);
  sanitizeAscii(media.title, d + 5, n - 5, sizeof(media.title) - 1);
  media.recvMs = now;
}

void processCmds(unsigned long now) {
  while (qTail != qHead) {
    Cmd& c = cmdQ[qTail];
    switch (c.type) {
      case CMD_TODO:  handleTodoCmd(c.data, c.len); break;
      case CMD_TEXT:
        sanitizeAscii(scrollText, c.data, c.len, sizeof(scrollText) - 1);
        saveText();
        updateTextValue();
        break;
      case CMD_MODE:  if (c.len >= 1) setDisplayMode(c.data[0]); break;
      case CMD_TIME:
        if (c.len >= 4) syncTime((uint32_t)c.data[0] | ((uint32_t)c.data[1] << 8) | ((uint32_t)c.data[2] << 16) | ((uint32_t)c.data[3] << 24));
        break;
      case CMD_MEDIA: handleMedia(c.data, c.len, now); break;
    }
    qTail = (qTail + 1) % CMD_Q;
  }
}

bool mediaVisible(unsigned long now) {
  if (!bleConnected || media.state == 0 || media.title[0] == 0) return false;
  if (media.state == 1 && now - media.recvMs > 90000UL) return false;   // aplikasi berhenti ngirim
  return true;
}

uint16_t mediaPos(unsigned long now) {
  uint32_t p = media.pos;
  if (media.state == 1) p += (now - media.recvMs) / 1000;
  if (media.dur > 0 && p > media.dur) p = media.dur;
  if (p > 65535) p = 65535;
  return (uint16_t)p;
}

// ================= POMODORO =================
unsigned long pomoTotalMs(PomoPhase p) {
  if (p == PH_FOCUS) return POMO_FOCUS_MS;
  if (p == PH_BREAK) return POMO_BREAK_MS;
  return POMO_LONG_MS;
}

unsigned long pomoRemaining(unsigned long now) {
  if (!pomoRunning) return pomoLeftMs;
  long left = (long)(pomoEndMs - now);
  return left > 0 ? (unsigned long)left : 0;
}

void pomoEvent() { sendEvent(4, (pomoView ? 1 : 0) | (pomoRunning ? 2 : 0) | ((int)pomoPhase << 2)); }

void pomoEnter(unsigned long now) {
  pomoView = true;
  pomoPhase = PH_FOCUS;
  pomoCycle = 0;
  pomoRunning = false;
  pomoLeftMs = POMO_FOCUS_MS;
  tone(BUZZER_PIN, 1200, 60);
  pomoEvent();
}

void pomoExit() {
  pomoView = false;
  pomoRunning = false;
  tone(BUZZER_PIN, 500, 80);
  pomoEvent();
}

void pomoStartPause(unsigned long now) {
  if (!pomoRunning) {
    pomoEndMs = now + pomoLeftMs;
    pomoRunning = true;
    tone(BUZZER_PIN, 1000, 60);
  } else {
    pomoLeftMs = pomoRemaining(now);
    pomoRunning = false;
    tone(BUZZER_PIN, 600, 60);
  }
  pomoEvent();
}

void addPomodoro() {
  stats.pomoWeek[0]++;
  stats.pomoTotal++;
  if (timeSynced) {
    uint32_t day = nowDay();
    if (stats.lastPomoDay != day) {
      if (stats.lastPomoDay != 0 && stats.lastPomoDay + 1 == day) stats.streak++;
      else stats.streak = 1;
      stats.lastPomoDay = day;
    }
  } else if (stats.streak == 0) {
    stats.streak = 1;                      // belum ada jam dari HP: cuma bisa hitung minimal 1
  }
  saveStats();
  updateStatsValue();
  sendEvent(3, 0);
}

void onPomoPhaseEnd(unsigned long now) {
  if (pomoPhase == PH_FOCUS) {
    addPomodoro();
    pomoCycle++;
    pomoPhase = (pomoCycle >= POMO_SETS) ? PH_LONG : PH_BREAK;
    pomoEndMs = now + pomoTotalMs(pomoPhase);
    pomoRunning = true;                    // istirahat langsung jalan
    playMelody(MEL_POMO_DONE, 4);
    setMode(HAPPY, now);
    showMessage("Fokus selesai!", pomoPhase == PH_LONG ? "Istirahat 15 menit" : "Istirahat 5 menit");
  } else {
    if (pomoPhase == PH_LONG) pomoCycle = 0;
    pomoPhase = PH_FOCUS;
    pomoLeftMs = POMO_FOCUS_MS;
    pomoRunning = false;                   // fokus berikutnya nunggu btn4
    playMelody(MEL_BREAK_END, 3);
    showMessage("Istirahat selesai", "btn4: mulai fokus");
  }
  pomoEvent();
}

void updatePomodoro(unsigned long now) {
  if (pomoView && pomoRunning && (long)(pomoEndMs - now) <= 0) onPomoPhaseEnd(now);
}

// ================= IKAN: HELPER =================
int fishMinY() { int y = topRes + 2; return y < 3 ? 3 : y; }

void spawnBubble(float x, float y) {
  for (int i = 0; i < MAX_BUBBLES; i++) {
    if (!bubbles[i].active) {
      bubbles[i].x = x;
      bubbles[i].y = y;
      bubbles[i].speed = 0.3f + random(0, 5) / 10.0f;
      bubbles[i].r = random(1, 3);
      bubbles[i].active = true;
      return;
    }
  }
}

// keluarin 3 pelet berurutan dari sisi tombol makan
void spawnFood() {
  float dispenserX = BTN_TARGET_X[BTN_FEED] + FISH_W / 2.0f;
  int spawned = 0;
  for (int i = 0; i < MAX_FOOD && spawned < 3; i++) {
    if (!foods[i].active) {
      foods[i].x = constrain(dispenserX + random(-10, 11), 4, SCREEN_WIDTH - 4);
      foods[i].y = -spawned * 9.0f;          // ditunda bertahap biar jatuhnya gantian
      foods[i].active = true;
      spawned++;
    }
  }
}

int nearestFood() {
  float cx = fish.x + FISH_W / 2.0f;
  float cy = fish.y + FISH_H / 2.0f;
  int best = -1;
  float bestDist = 1e9;
  for (int i = 0; i < MAX_FOOD; i++) {
    if (!foods[i].active) continue;
    float dx = foods[i].x - cx;
    float dy = foods[i].y - cy;
    float d = sqrt(dx * dx + dy * dy);
    if (d < bestDist) { bestDist = d; best = i; }
  }
  return best;
}

void pickNewTarget(unsigned long now) {
  fish.tx = random(FISH_MIN_X, FISH_MAX_X + 1);
  fish.ty = random(fishMinY(), FISH_MAX_Y + 1);
  if (mode == SWIM) nextTargetTime = now + random(1500, 4500);
  else              nextTargetTime = now + random(400, 900);   // lagi heboh: sering ganti arah
}

void setMode(Mode m, unsigned long now) {
  mode = m;
  modeStart = now;
  if (m == SWIM || m == HAPPY || m == ANGRY) pickNewTarget(now);
}

// ================= MINI GAME =================
void spawnPipe(float x) {
  for (int i = 0; i < MAX_PIPES; i++) {
    if (!pipes[i].active) {
      pipes[i].x = x;
      pipes[i].gapY = random(GAME_GAP / 2 + 5, GAME_FLOOR - GAME_GAP / 2);
      pipes[i].passed = false;
      pipes[i].active = true;
      return;
    }
  }
}

void startGame(unsigned long now) {
  for (int i = 0; i < MAX_FOOD; i++) foods[i].active = false;
  for (int i = 0; i < MAX_BUBBLES; i++) bubbles[i].active = false;
  for (int i = 0; i < MAX_PIPES; i++) pipes[i].active = false;
  mode = GAME_READY;
  modeStart = now;
  gameY = 22;
  gameVy = 0;
  score = 0;
  fish.dir = 1;
  tone(BUZZER_PIN, 1000, 80);
  showMessage("Flappy Fish!", "Tekan tombol 3");
}

void flap() {
  gameVy = GAME_FLAP;
  fishFrame = 1;
  tone(BUZZER_PIN, 700, 25);
}

void crash(unsigned long now) {
  mode = GAME_OVER;
  modeStart = now;
  if (score > bestScore) { bestScore = score; saveBest(); }
  tone(BUZZER_PIN, 250, 400);
  showMessage("Game Over!", "Skor " + String(score) + "  Best " + String(bestScore));
}

void endGame(unsigned long now) {
  fish.x = GAME_FISH_X;
  fish.y = constrain(gameY, fishMinY(), FISH_MAX_Y);
  fish.dir = 1;
  showOverlay = false;
  setMode(SWIM, now);
}

void updateGame(unsigned long now, float dt) {
  if (mode == GAME_READY) {
    gameY = 22 + sin(now / 300.0) * 3;      // ikan melayang nunggu tombol
    if (now - lastFishFrame > 220) { fishFrame ^= 1; lastFishFrame = now; }
    return;
  }

  if (mode == GAME_OVER) {
    gameVy = min(GAME_VMAX, gameVy + GAME_G * dt);
    gameY += gameVy * dt;
    if (gameY > FISH_MAX_Y) gameY = FISH_MAX_Y;
    if (now - modeStart > overlayDuration) endGame(now);
    return;
  }

  // ---- GAME_PLAY ----
  gameVy = min(GAME_VMAX, gameVy + GAME_G * dt);
  gameY += gameVy * dt;

  if (now - lastFishFrame > 130) { fishFrame ^= 1; lastFishFrame = now; }

  float rightmost = -1000;
  bool any = false;
  for (int i = 0; i < MAX_PIPES; i++) {
    if (!pipes[i].active) continue;
    pipes[i].x -= GAME_SPEED * dt;
    if (pipes[i].x < -PIPE_W - 4) { pipes[i].active = false; continue; }
    any = true;
    if (pipes[i].x > rightmost) rightmost = pipes[i].x;
  }
  if (!any || rightmost < SCREEN_WIDTH - PIPE_SPACING) spawnPipe(SCREEN_WIDTH);

  float hx0 = GAME_FISH_X + HB_X0, hx1 = GAME_FISH_X + HB_X1;
  float hy0 = gameY + HB_Y0,       hy1 = gameY + HB_Y1;
  if (hy0 <= 0 || hy1 >= GAME_FLOOR) { crash(now); return; }

  for (int i = 0; i < MAX_PIPES; i++) {
    if (!pipes[i].active) continue;
    float px = pipes[i].x;
    if (hx1 > px && hx0 < px + PIPE_W) {
      if (hy0 < pipes[i].gapY - GAME_GAP / 2 || hy1 > pipes[i].gapY + GAME_GAP / 2) {
        crash(now);
        return;
      }
    }
    if (!pipes[i].passed && px + PIPE_W < hx0) {
      pipes[i].passed = true;
      score++;
      tone(BUZZER_PIN, 1400, 40);
    }
  }
}

// ================= REMINDER MAKAN =================
void doSnooze(unsigned long now) {
  if (!reminderActive) return;
  reminderActive = false;
  snoozeCount++;

  if (snoozeCount >= maxSnoozeBeforeAngry) {
    nextReminderTime = now + shortSnoozeInterval;
    setMode(ANGRY, now);
    tone(BUZZER_PIN, 300, 400);
    snoozeCount = 0;
  } else {
    nextReminderTime = now + snoozeInterval;
    tone(BUZZER_PIN, 1200, 150);
  }

  mealIndex = (mealIndex + 1) % 3;
  showOverlay = false;
}

void updateReminder(unsigned long now) {
  if (!reminderActive && now >= nextReminderTime) {
    reminderActive = true;
    tone(BUZZER_PIN, 400, 500);
    showMessage("Waktunya makan:", String(mealNames[mealIndex]) + "!");
  }
}

// ================= LOGIKA IKAN (IDLE) =================
void updateFish(unsigned long now, float dt) {
  // habis durasi -> balik berenang santai
  if (mode == HAPPY && now - modeStart > happyDuration) setMode(SWIM, now);
  if (mode == ANGRY && now - modeStart > angryDuration) setMode(SWIM, now);
  if (mode == EATING && now - modeStart > eatDuration) setMode(SWIM, now);

  // pelet jatuh
  for (int i = 0; i < MAX_FOOD; i++) {
    if (!foods[i].active) continue;
    foods[i].y += 0.5f * dt;
    if (foods[i].y > FOOD_FLOOR) foods[i].y = FOOD_FLOOR;
  }

  int minY = fishMinY();
  int foodIdx = nearestFood();
  bool chasingFood = false;

  if (mode == EATING) {
    // lagi ngunyah, diem dulu
  } else if (pressedIdx >= 0) {
    // ada tombol ditekan -> ikan ngarah ke tombol itu
    fish.tx = BTN_TARGET_X[pressedIdx];
    fish.ty = constrain(BTN_TARGET_Y[pressedIdx], minY, FISH_MAX_Y);
    nextTargetTime = now + 1500;             // habis dilepas, ikan masih nongkrong sebentar
  } else if (foodIdx >= 0) {
    chasingFood = true;
    float fx = foods[foodIdx].x;
    // pilih sisi menghadap yang paling dekat (mulut kanan = x+33, mulut kiri = x+2)
    float costR = (fx >= FISH_W - 3 + FISH_MIN_X) ? fabs((fx - (FISH_W - 3)) - fish.x) : 1e9f;
    float costL = (fx - 2 <= FISH_MAX_X)          ? fabs((fx - 2) - fish.x)             : 1e9f;
    if (fish.dir > 0) costR -= 6; else costL -= 6;      // condong tetap menghadap arah sekarang
    fish.dir = (costR < costL) ? 1 : -1;                // ikan beneran menghadap pelet
    float mouthOffset = (fish.dir > 0) ? (FISH_W - 3) : 2;
    fish.tx = constrain(fx - mouthOffset, FISH_MIN_X, FISH_MAX_X);
    fish.ty = constrain(foods[foodIdx].y - FISH_H * 0.55f, minY, FISH_MAX_Y);
  } else if (now >= nextTargetTime) {
    pickNewTarget(now);
  }

  // kecepatan sesuai mode
  float speed = 0.55f;
  if (mode == HAPPY) speed = 1.4f;
  if (mode == ANGRY) speed = 2.2f;
  if (pressedIdx >= 0 && mode == SWIM) speed = 1.3f;
  if (chasingFood) speed = 1.2f;
  if (mode == EATING) speed = 0;

  // gerak ke target
  float dx = fish.tx - fish.x;
  float dy = fish.ty - fish.y;
  float dist = sqrt(dx * dx + dy * dy);
  float step = speed * dt;
  if (speed > 0) {
    if (dist > step) {
      fish.x += dx / dist * step;
      fish.y += dy / dist * step;
    } else {
      fish.x = fish.tx;
      fish.y = fish.ty;
    }
  }
  if (!chasingFood && fabs(dx) > 3) fish.dir = (dx > 0) ? 1 : -1;

  fish.x = constrain(fish.x, FISH_MIN_X, FISH_MAX_X);
  if (fish.y < minY) fish.y = min((float)minY, fish.y + 1.5f * dt);   // strip baru muncul: geser turun pelan
  if (fish.y > FISH_MAX_Y) fish.y = FISH_MAX_Y;

  // mulut ketemu pelet -> makan
  if (mode != EATING) {
    float mx = fish.x + ((fish.dir > 0) ? (FISH_W - 3) : 2);
    float my = fish.y + FISH_H * 0.55f;
    for (int i = 0; i < MAX_FOOD; i++) {
      if (!foods[i].active || foods[i].y < 0) continue;
      float fx = foods[i].x - mx;
      float fy = foods[i].y - my;
      if (sqrt(fx * fx + fy * fy) < 7) {
        foods[i].active = false;
        setMode(EATING, now);
        lastChew = 0;
        stats.eatToday++;
        stats.eatTotal++;
        statsDirty = true;
        break;
      }
    }
  }

  // ekor & sirip goyang (lebih cepat kalau heboh)
  unsigned long frameInterval = 220;
  if (mode == HAPPY) frameInterval = 110;
  if (mode == ANGRY) frameInterval = 70;
  if (mode == EATING) frameInterval = 150;
  if (now - lastFishFrame > frameInterval) {
    fishFrame ^= 1;
    lastFishFrame = now;
  }

  // kunyah + bunyi saat makan
  if (mode == EATING && now - lastChew > chewInterval) {
    mouthOpen = !mouthOpen;
    lastChew = now;
    tone(BUZZER_PIN, mouthOpen ? 900 : 600, 80);
  }

  // gelembung: dari ikan pas senang, dan sesekali dari dasar
  if (mode == HAPPY && now - lastHappyBubble > 250) {
    lastHappyBubble = now;
    float bx = (fish.dir > 0) ? fish.x + FISH_W : fish.x - 2;
    spawnBubble(bx, fish.y + 4);
  }
  if (random(0, 200) == 0) spawnBubble(random(4, SCREEN_WIDTH - 4), SCREEN_HEIGHT - 4);

  for (int i = 0; i < MAX_BUBBLES; i++) {
    if (!bubbles[i].active) continue;
    bubbles[i].y -= bubbles[i].speed * dt;
    if (bubbles[i].y < 0) bubbles[i].active = false;
  }
}

// ================= TOMBOL =================
// pendek = dilepas sebelum LONG_PRESS_MS | panjang = ditahan sampai LONG_PRESS_MS (langsung nembak)
void readButtons(unsigned long now) {
  for (int i = 0; i < 4; i++) {
    btnDown[i] = (digitalRead(BTN_PIN[i]) == LOW);
    bool pressed  = btnDown[i] && !btnPrev[i];
    bool released = !btnDown[i] && btnPrev[i];
    evPress[i] = pressed;
    evShort[i] = false;
    evLong[i]  = false;
    if (pressed) { btnPressStart[i] = now; longFired[i] = false; }
    if (btnDown[i] && !longFired[i] && now - btnPressStart[i] >= LONG_PRESS_MS) { longFired[i] = true; evLong[i] = true; }
    if (released && !longFired[i]) evShort[i] = true;
    btnPrev[i] = btnDown[i];
  }
}

bool anyButtonEvent() {
  for (int i = 0; i < 4; i++) if (evPress[i] || evShort[i] || evLong[i]) return true;
  return false;
}

// ================= INPUT PER TAMPILAN =================
void openMenu(unsigned long now)   { ui = UI_MENU;   menuCursor = displayMode; uiLastInput = now; tone(BUZZER_PIN, 1000, 40); }
void openTodo(unsigned long now)   { ui = UI_TODO;   todoCursor = 0; todoScroll = 0; uiLastInput = now; tone(BUZZER_PIN, 1000, 40); }
void openStatus(unsigned long now) { ui = UI_STATUS; uiLastInput = now; tone(BUZZER_PIN, 1000, 40); }
void closeUi(unsigned long now)    { ui = UI_AQUARIUM; uiLastInput = now; tone(BUZZER_PIN, 600, 40); }

void handleMenuInput(unsigned long now) {
  if (evShort[BTN_FEED]) menuCursor = (menuCursor + MENU_COUNT - 1) % MENU_COUNT;     // atas
  if (evShort[BTN_GAME]) menuCursor = (menuCursor + 1) % MENU_COUNT;                  // bawah
  if (evShort[BTN_TEXT]) {                                                            // pilih
    if (menuCursor == 0)      { setDisplayMode(DM_DEFAULT); closeUi(now); showMessage("Mode", "Default"); }
    else if (menuCursor == 1) { setDisplayMode(DM_TEXT);    closeUi(now); showMessage("Mode", "Tulisan berjalan"); }
    else                      { openStatus(now); }
  }
  if (evShort[BTN_PET] || now - uiLastInput > 12000UL) closeUi(now);                  // kembali
}

void handleTodoInput(unsigned long now) {
  if (evShort[BTN_FEED] && todoCursor > 0) todoCursor--;                              // atas
  if (evShort[BTN_GAME] && todoCursor < todoN - 1) todoCursor++;                      // bawah
  if (evShort[BTN_TEXT] && todoN > 0) {                                               // ceklis
    todos[todoCursor].done = !todos[todoCursor].done;
    tone(BUZZER_PIN, todos[todoCursor].done ? 1300 : 800, 50);
    todoChanged();
  }
  if (evLong[BTN_TEXT] || evShort[BTN_PET] || now - uiLastInput > 30000UL) { closeUi(now); return; }

  if (todoCursor < todoScroll) todoScroll = todoCursor;
  if (todoCursor >= todoScroll + TODO_ROWS) todoScroll = todoCursor - TODO_ROWS + 1;
}

void handleStatusInput(unsigned long now) {
  if (evShort[BTN_FEED] || evShort[BTN_TEXT] || evShort[BTN_GAME] || evShort[BTN_PET] || now - uiLastInput > 10000UL) closeUi(now);
}

void handleAquariumInput(unsigned long now) {
  pressedIdx = -1;
  for (int i = 0; i < 4; i++) if (btnDown[i]) { pressedIdx = i; break; }

  // btn1: pendek = kasih makan | panjang = menu
  if (evShort[BTN_FEED]) { spawnFood(); tone(BUZZER_PIN, 800, 100); }
  if (evLong[BTN_FEED])  { openMenu(now); return; }

  // btn2: pendek = tulisan nama | panjang = todo list
  if (evShort[BTN_TEXT]) showMessage(TEXT_LINE_1, TEXT_LINE_2);
  if (evLong[BTN_TEXT])  { openTodo(now); return; }

  // btn3: pendek = snooze reminder | panjang = mini game
  if (evShort[BTN_GAME]) doSnooze(now);
  if (evLong[BTN_GAME])  { startGame(now); return; }

  // btn4: pendek = elus (atau mulai/jeda pomodoro) | panjang = buka/tutup pomodoro
  if (evShort[BTN_PET]) {
    if (pomoView) pomoStartPause(now);
    else if (mode != EATING && mode != ANGRY) { setMode(HAPPY, now); tone(BUZZER_PIN, 1500, 80); }
  }
  if (evLong[BTN_PET]) {
    if (pomoView) pomoExit(); else pomoEnter(now);
  }
}

void handleInputs(unsigned long now) {
  if (inGame()) {                              // mini game: cuma btn3 yang dipakai
    pressedIdx = -1;
    if (evPress[BTN_GAME]) {
      if (mode == GAME_READY) {
        mode = GAME_PLAY;
        modeStart = now;
        showOverlay = false;
        spawnPipe(SCREEN_WIDTH);
        flap();
      } else if (mode == GAME_PLAY) {
        flap();
      }
    }
    return;
  }

  if (ui != UI_AQUARIUM && anyButtonEvent()) { /* uiLastInput diperbarui di bawah */ }
  switch (ui) {
    case UI_MENU:   pressedIdx = -1; handleMenuInput(now);   break;
    case UI_TODO:   pressedIdx = -1; handleTodoInput(now);   break;
    case UI_STATUS: pressedIdx = -1; handleStatusInput(now); break;
    default:        handleAquariumInput(now);                break;
  }
  if (ui != UI_AQUARIUM && anyButtonEvent()) uiLastInput = now;
}

// ================= GAMBAR: ORNAMEN AKUARIUM =================
void drawSand() {
  display.drawFastHLine(0, 63, SCREEN_WIDTH, SSD1306_WHITE);
  for (int x = 1; x < SCREEN_WIDTH; x += 3) display.drawPixel(x, 62, SSD1306_WHITE);
  const int dots[] = {7, 29, 53, 77, 101, 123};
  for (int i = 0; i < 6; i++) display.drawPixel(dots[i], 61, SSD1306_WHITE);
}

void drawRock(int cx, int r) {
  display.fillCircle(cx, 63, r, SSD1306_WHITE);
  display.drawPixel(cx - 1, 63 - r + 2, SSD1306_BLACK);   // kilau kecil
  display.drawPixel(cx,     63 - r + 2, SSD1306_BLACK);
}

void drawSeaweed(int baseX, int h, float phase, unsigned long now) {
  float t = now / 450.0f;
  int px = baseX, py = 62;
  for (int i = 1; i <= h; i += 3) {
    float sway = sin(t + phase + i * 0.22f) * (1.0f + i * 0.09f);
    int nx = baseX + (int)(sway + (sway >= 0 ? 0.5f : -0.5f));
    int ny = 62 - i;
    display.drawLine(px, py, nx, ny, SSD1306_WHITE);
    display.drawLine(px + 1, py, nx + 1, ny, SSD1306_WHITE);
    if ((i / 3) % 2 == 0) display.drawLine(nx + 1, ny, nx + 4, ny - 2, SSD1306_WHITE);   // daun
    else                  display.drawLine(nx, ny, nx - 3, ny - 2, SSD1306_WHITE);
    px = nx; py = ny;
  }
}

void drawStarfish(int cx, int cy) {
  display.drawLine(cx, cy, cx,     cy - 3, SSD1306_WHITE);
  display.drawLine(cx, cy, cx + 3, cy - 1, SSD1306_WHITE);
  display.drawLine(cx, cy, cx + 2, cy + 2, SSD1306_WHITE);
  display.drawLine(cx, cy, cx - 2, cy + 2, SSD1306_WHITE);
  display.drawLine(cx, cy, cx - 3, cy - 1, SSD1306_WHITE);
}

void drawAquarium(unsigned long now) {
  drawSand();
  drawSeaweed(8,   26, 0.0f, now);
  drawSeaweed(92,  17, 3.1f, now);
  drawSeaweed(119, 22, 1.7f, now);
  drawRock(18, 5);
  drawRock(25, 3);
  drawRock(106, 4);
  drawStarfish(62, 59);
}

// ================= GAMBAR: ELEMEN =================
void drawBubbles() {
  for (int i = 0; i < MAX_BUBBLES; i++) {
    if (bubbles[i].active) {
      display.drawCircle((int)bubbles[i].x, (int)bubbles[i].y, bubbles[i].r, SSD1306_WHITE);
    }
  }
}

void drawFood() {
  for (int i = 0; i < MAX_FOOD; i++) {
    if (foods[i].active && foods[i].y >= 0) {
      display.fillCircle((int)foods[i].x, (int)foods[i].y, 2, SSD1306_WHITE);
    }
  }
}

void drawPipes() {
  for (int i = 0; i < MAX_PIPES; i++) {
    if (!pipes[i].active) continue;
    int x = (int)pipes[i].x;
    int topH = pipes[i].gapY - GAME_GAP / 2;      // tinggi tiang atas
    int botY = pipes[i].gapY + GAME_GAP / 2;      // awal tiang bawah
    display.drawRect(x, -1, PIPE_W, topH + 1, SSD1306_WHITE);
    display.drawRect(x - 2, topH - 4, PIPE_W + 4, 4, SSD1306_WHITE);
    display.drawRect(x, botY, PIPE_W, 64 - botY, SSD1306_WHITE);
    display.drawRect(x - 2, botY, PIPE_W + 4, 4, SSD1306_WHITE);
    for (int y = 3; y < topH - 4; y += 4) display.drawPixel(x + 3, y, SSD1306_WHITE);
    for (int y = botY + 7; y < 62; y += 4) display.drawPixel(x + 3, y, SSD1306_WHITE);
  }
}

void drawFishSprite(int x, int y, int dir, Expr ex) {
  int d = (dir > 0) ? 0 : 1;
  display.drawBitmap(x, y, FISH_MASK[d][fishFrame], FISH_W, FISH_H, SSD1306_BLACK);
  display.drawBitmap(x, y, FISH_BMP[d][ex][fishFrame], FISH_W, FISH_H, SSD1306_WHITE);
}

void drawFish() {
  int x = (int)fish.x;
  int y = (int)fish.y;
  Expr ex = EX_NORMAL;

  if (mode == EATING) {
    y += mouthOpen ? 1 : 0;                    // goyang kecil pas ngunyah
    ex = mouthOpen ? EX_EAT : EX_NORMAL;
  } else {
    y += (int)(sin(millis() / 350.0) * 1.5);   // melayang naik-turun pelan
  }
  if (mode == HAPPY) ex = EX_HAPPY;
  if (mode == ANGRY) {
    ex = EX_ANGRY;
    x += random(-1, 2);                        // gemetar marah
    y += random(-1, 2);
  }
  drawFishSprite(x, y, fish.dir, ex);
}

void drawGameFish() {
  Expr ex = (mode == GAME_OVER) ? EX_ANGRY : EX_NORMAL;
  drawFishSprite(GAME_FISH_X, (int)gameY, 1, ex);
}

void drawScore() {
  char s[8];
  snprintf(s, sizeof(s), "%d", score);
  int16_t x1, y1;
  uint16_t w, h;
  display.setTextSize(1);
  display.getTextBounds(String(s), 0, 0, &x1, &y1, &w, &h);
  int bx = (SCREEN_WIDTH - w) / 2 - 3;
  display.fillRect(bx, 0, w + 6, h + 4, SSD1306_BLACK);
  display.drawRect(bx, 0, w + 6, h + 4, SSD1306_WHITE);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(bx + 3, 2);
  display.print(s);
}

void drawOverlay() {
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  int16_t x1, y1;
  uint16_t w1 = 0, h1 = 0, w2 = 0, h2 = 0;
  display.getTextBounds(overlayLine1, 0, 0, &x1, &y1, &w1, &h1);
  bool twoLines = (overlayLine2.length() > 0);
  if (twoLines) display.getTextBounds(overlayLine2, 0, 0, &x1, &y1, &w2, &h2);

  int boxW = max((int)w1, (int)w2) + 8;
  int boxH = twoLines ? (h1 + h2 + 2 + 8) : (h1 + 8);
  int boxX = (SCREEN_WIDTH - boxW) / 2;
  int boxY = (SCREEN_HEIGHT - boxH) / 2;

  display.fillRect(boxX, boxY, boxW, boxH, SSD1306_BLACK);
  display.drawRect(boxX, boxY, boxW, boxH, SSD1306_WHITE);

  display.setCursor((SCREEN_WIDTH - w1) / 2, boxY + 4);
  display.print(overlayLine1);
  if (twoLines) {
    display.setCursor((SCREEN_WIDTH - w2) / 2, boxY + 4 + h1 + 2);
    display.print(overlayLine2);
  }
}

// ================= GAMBAR: STRIP ATAS (teks / musik / pomodoro) =================
// teks yang lebih panjang dari layar berjalan ke kiri terus-menerus
void drawMarquee(const char* txt, int y, unsigned long now) {
  int w = strlen(txt) * 6;
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  if (w <= SCREEN_WIDTH) {
    display.setCursor((SCREEN_WIDTH - w) / 2, y);
    display.print(txt);
    return;
  }
  int period = w + 24;
  int off = (int)((now / 35) % period);       // ~28 px/detik
  display.setCursor(-off, y);
  display.print(txt);
  display.setCursor(-off + period, y);
  display.print(txt);
}

void fmtTime(char* out, uint16_t sec) { snprintf(out, 8, "%u:%02u", (unsigned)(sec / 60), (unsigned)(sec % 60)); }

void drawTextStrip(unsigned long now) {
  display.fillRect(0, 0, SCREEN_WIDTH, 9, SSD1306_BLACK);
  drawMarquee(scrollText, 0, now);
}

void drawMusicStrip(unsigned long now) {
  display.fillRect(0, 0, SCREEN_WIDTH, 17, SSD1306_BLACK);
  drawMarquee(media.title, 0, now);

  uint16_t pos = mediaPos(now);
  char a[8], b[8];
  fmtTime(a, pos);
  fmtTime(b, media.dur);
  display.setTextColor(SSD1306_WHITE);
  if (!(media.state == 2 && ((now / 500) % 2))) {       // lagu dijeda: waktu berkedip
    display.setCursor(0, 9);
    display.print(a);
  }
  if (media.dur > 0) {
    display.setCursor(SCREEN_WIDTH - (int)strlen(b) * 6, 9);
    display.print(b);
  }
  display.drawRect(33, 11, 62, 4, SSD1306_WHITE);        // garis progres lagu
  if (media.dur > 0) {
    int fill = (int)((uint32_t)60 * pos / media.dur);
    if (fill > 0) display.fillRect(34, 12, fill, 2, SSD1306_WHITE);
  }
}

void drawPomoStrip(unsigned long now) {
  display.fillRect(0, 0, SCREEN_WIDTH, 21, SSD1306_BLACK);
  unsigned long left = pomoRemaining(now);
  unsigned long total = pomoTotalMs(pomoPhase);
  char buf[10];
  snprintf(buf, sizeof(buf), "%02u:%02u", (unsigned)(left / 60000UL), (unsigned)((left / 1000UL) % 60));

  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(2);
  display.setCursor(2, 1);
  display.print(buf);
  display.setTextSize(1);

  const char* label;
  if (!pomoRunning) label = (pomoPhase == PH_FOCUS && left == POMO_FOCUS_MS) ? "SIAP" : "JEDA";
  else if (pomoPhase == PH_FOCUS) label = "FOKUS";
  else if (pomoPhase == PH_BREAK) label = "REHAT";
  else label = "REHAT+";
  display.setCursor(76, 1);
  display.print(label);

  for (int i = 0; i < POMO_SETS; i++) {                 // bulatan = fokus selesai di set ini
    int cx = 80 + i * 12;
    if (i < pomoCycle) display.fillCircle(cx, 14, 3, SSD1306_WHITE);
    else               display.drawCircle(cx, 14, 3, SSD1306_WHITE);
  }

  display.drawRect(2, 18, 124, 3, SSD1306_WHITE);       // progres fase
  unsigned long done = (total > left) ? (total - left) : 0;
  int fill = (int)((uint32_t)122 * done / total);
  if (fill > 0) display.fillRect(3, 19, fill, 1, SSD1306_WHITE);
}

// ================= GAMBAR: MENU / TODO / STATUS =================
void drawMenu() {
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, SSD1306_WHITE);
  display.setCursor(37, 4);
  display.print("== MENU ==");
  for (int i = 0; i < MENU_COUNT; i++) {
    int y = 18 + i * 12;
    if (i == menuCursor) {
      display.fillRect(4, y - 2, 120, 11, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
    } else {
      display.setTextColor(SSD1306_WHITE);
    }
    display.setCursor(8, y);
    display.print((i == displayMode && i < 2) ? "* " : "  ");
    display.print(MENU_ITEMS[i]);
  }
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(6, 54);
  display.print("1^ 3v 2ok 4back");
}

void drawTodo() {
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  int doneCount = 0;
  for (int i = 0; i < todoN; i++) if (todos[i].done) doneCount++;
  char head[24];
  snprintf(head, sizeof(head), "TODO %d/%d", doneCount, todoN);
  display.setCursor(0, 0);
  display.print(head);
  if (bleConnected) { display.setCursor(110, 0); display.print("BT"); }
  display.drawFastHLine(0, 9, SCREEN_WIDTH, SSD1306_WHITE);

  if (todoN == 0) {
    display.setCursor(18, 24);
    display.print("Belum ada tugas");
    display.setCursor(6, 36);
    display.print("Tambah dari aplikasi");
  } else {
    for (int r = 0; r < TODO_ROWS; r++) {
      int idx = todoScroll + r;
      if (idx >= todoN) break;
      int y = 12 + r * 9;
      char line[32];
      snprintf(line, sizeof(line), "%c[%c] %.16s", idx == todoCursor ? '>' : ' ', todos[idx].done ? 'x' : ' ', todos[idx].text);
      display.setCursor(0, y);
      display.print(line);
      if (todos[idx].done) {                            // dicoret kalau sudah selesai
        int len = strlen(todos[idx].text); if (len > 16) len = 16;
        display.drawFastHLine(30, y + 3, len * 6, SSD1306_WHITE);
      }
    }
  }
  display.setCursor(2, 56);
  display.print("1:^ 3:v 2:ok 4:keluar");
}

void drawStatus(unsigned long now) {
  (void)now;
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(31, 0);
  display.print("STATUS IKAN");
  display.drawFastHLine(0, 9, SCREEN_WIDTH, SSD1306_WHITE);

  uint16_t weekTotal = 0, maxV = 1;
  for (int i = 0; i < 7; i++) { weekTotal += stats.pomoWeek[i]; if (stats.pomoWeek[i] > maxV) maxV = stats.pomoWeek[i]; }

  char line[32];
  snprintf(line, sizeof(line), "Streak: %u hari", (unsigned)displayStreak());
  display.setCursor(0, 12); display.print(line);
  snprintf(line, sizeof(line), "Eat   : %u pelet", (unsigned)stats.eatToday);
  display.setCursor(0, 21); display.print(line);
  snprintf(line, sizeof(line), "Week  : %u pomodoro", (unsigned)weekTotal);
  display.setCursor(0, 30); display.print(line);

  const char DOW[8] = "SSRKJSM";                        // Sen Sel Rab Kam Jum Sab Min
  for (int i = 0; i < 7; i++) {
    int back = 6 - i;                                    // kiri = 6 hari lalu, kanan = hari ini
    int x = 4 + i * 18;
    int h = (int)((uint32_t)stats.pomoWeek[back] * 13 / maxV);
    if (stats.pomoWeek[back] > 0 && h < 1) h = 1;
    if (h > 0) display.fillRect(x, 53 - h, 12, h, SSD1306_WHITE);
    else       display.drawFastHLine(x, 53, 12, SSD1306_WHITE);
    display.setCursor(x + 3, 56);
    if (timeSynced) {
      uint32_t day = stats.lastDay - back;
      display.print(DOW[(day + 3) % 7]);                 // 1 Jan 1970 = Kamis
    } else if (back == 0) {
      display.print("*");
    }
  }
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  Wire.begin(OLED_SDA, OLED_SCK);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED gagal init, cek kabel SDA/SCL/VCC/GND");
    for(;;);
  }
  display.setTextWrap(false);
  display.clearDisplay();

  loadAll();
  for (int i = 0; i < 4; i++) pinMode(BTN_PIN[i], INPUT_PULLUP);

  randomSeed(micros());
  for (int i = 0; i < MAX_BUBBLES; i++) bubbles[i].active = false;
  for (int i = 0; i < MAX_FOOD; i++) foods[i].active = false;
  for (int i = 0; i < MAX_PIPES; i++) pipes[i].active = false;

  initBle();

  unsigned long now = millis();
  lastFrameTime = now;
  uiLastInput = now;
  pickNewTarget(now);
  nextReminderTime = now + mealResetInterval;
}

// ================= LOOP =================
void loop() {
  unsigned long now = millis();
  float dt = (now - lastFrameTime) / 16.0f;     // 1 unit = 16 ms
  if (dt > 2.0f) dt = 2.0f;                     // jaga-jaga kalau ada frame yang lag
  lastFrameTime = now;

  processCmds(now);                             // perintah dari HP (BLE)
  readButtons(now);
  updatePomodoro(now);
  updateMelody(now);

  bool musicOn = mediaVisible(now);
  topRes = pomoView ? 21 : (musicOn ? 17 : (displayMode == DM_TEXT ? 10 : 0));

  handleInputs(now);
  if (inGame())                 updateGame(now, dt);
  else if (ui == UI_AQUARIUM) { updateReminder(now); updateFish(now, dt); }

  // status koneksi Bluetooth
  if (bleConnected != lastBleConnected) {
    lastBleConnected = bleConnected;
    if (!inGame()) showMessage("Bluetooth", bleConnected ? "terhubung" : "terputus");
    tone(BUZZER_PIN, bleConnected ? 1500 : 500, 80);
  }

  // simpan statistik & cek pergantian hari
  if (statsDirty && now - lastStatsFlush > 15000UL) {
    saveStats(); updateStatsValue(); sendEvent(3, 0);
    lastStatsFlush = now;
  }
  if (timeSynced && now - lastDayCheck > 30000UL) { lastDayCheck = now; rollDays(nowDay()); }

  if (showOverlay && mode != GAME_OVER && now - overlayStart > overlayDuration) showOverlay = false;

  // ---- gambar ----
  display.clearDisplay();
  display.setTextWrap(false);
  if (inGame()) {
    drawSand();
    drawPipes();
    drawGameFish();
    if (mode == GAME_PLAY) drawScore();
  } else if (ui == UI_MENU) {
    drawMenu();
  } else if (ui == UI_TODO) {
    drawTodo();
  } else if (ui == UI_STATUS) {
    drawStatus(now);
  } else {
    drawAquarium(now);
    drawBubbles();
    drawFood();
    drawFish();
    if (pomoView)                    drawPomoStrip(now);
    else if (musicOn)                drawMusicStrip(now);
    else if (displayMode == DM_TEXT) drawTextStrip(now);
  }
  if (showOverlay) drawOverlay();
  display.display();

  delay(10);
}
