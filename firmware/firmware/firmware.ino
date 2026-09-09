#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_SDA 4
#define OLED_SCK 5

#define BTN_1      10   // tombol asli -> animasi makan (mulut ngunyah)
#define TOUCH_PIN  0    // TTP223 -> elus = senang
#define BTN_SNOOZE 1    // snooze reminder makan
#define BTN_TODO   3    // tampilkan waktu makan berikutnya
#define BUZZER_PIN 6    // buzzer passive

Adafruit_SH1106G display = Adafruit_SH1106G(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

enum Emotion { NEUTRAL, HAPPY, SAD, ANGRY, EATING };
Emotion currentEmotion = NEUTRAL;

struct EyeShape { float w, h; };
EyeShape shapes[5] = {
  {24, 30}, {24, 22}, {20, 24}, {24, 28}, {24, 30}
};

float curW = 24, curH = 30;

unsigned long lastBlink = 0;
unsigned long blinkInterval = 3000;
bool isBlinking = false;
unsigned long blinkStart = 0;
const int blinkDuration = 150;

unsigned long lastChew = 0;
const int chewInterval = 300;
bool mouthOpen = false;

// ---- animasi makan (BTN_1) ----
bool lastButtonState = HIGH;
unsigned long eatStartTime = 0;
const int eatDuration = 5000;

// ---- elus (TTP223) ----
bool lastTouchState = LOW;
bool petActive = false;
unsigned long petStart = 0;
const unsigned long petDuration = 3000;

// ---- sistem reminder makan ----
const char* mealNames[3] = {"Sarapan", "Makan Siang", "Makan Sore"};
int mealIndex = 0;

unsigned long nextReminderTime;
bool reminderActive = false;
const unsigned long mealResetInterval = 4UL * 60 * 60 * 1000; // 4 jam
const unsigned long snoozeInterval = 30UL * 60 * 1000;        // snooze normal: 30 menit
const unsigned long shortSnoozeInterval = 5UL * 60 * 1000;    // snooze ke-3: 5 menit aja

int snoozeCount = 0;
const int maxSnoozeBeforeAngry = 3;

bool lastSnoozeBtnState = HIGH;
bool lastTodoBtnState = HIGH;

// ---- efek marah (ANGRY) ----
bool angryActive = false;
unsigned long angryStart = 0;
const unsigned long angryDuration = 3000;

// ---- overlay teks ----
bool showOverlay = false;
String overlayText = "";
unsigned long overlayStart = 0;
const unsigned long overlayDuration = 2500;

void setup() {
  Wire.begin(OLED_SDA, OLED_SCK);
  if (!display.begin(0x3C, true)) { for(;;); }
  display.clearDisplay();

  pinMode(BTN_1, INPUT_PULLUP);
  pinMode(BTN_SNOOZE, INPUT_PULLUP);
  pinMode(BTN_TODO, INPUT_PULLUP);
  pinMode(TOUCH_PIN, INPUT);

  nextReminderTime = millis() + mealResetInterval;
}

float ease(float current, float target, float speed = 0.15) {
  return current + (target - current) * speed;
}

void showMessage(String text) {
  showOverlay = true;
  overlayStart = millis();
  overlayText = text;
}

void updateState() {
  unsigned long now = millis();

  // --- BTN_1: animasi makan (mulut ngunyah) ---
  bool buttonState = digitalRead(BTN_1);
  if (lastButtonState == HIGH && buttonState == LOW && currentEmotion != EATING) {
    currentEmotion = EATING;
    eatStartTime = now;
    tone(BUZZER_PIN, 800, 100);
  }
  lastButtonState = buttonState;

  if (currentEmotion == EATING && now - eatStartTime > eatDuration) {
    currentEmotion = NEUTRAL;
  }

  // --- TTP223: elus = senang ---
  bool touchState = digitalRead(TOUCH_PIN);
  if (lastTouchState == LOW && touchState == HIGH && currentEmotion != EATING && !petActive) {
    petActive = true;
    petStart = now;
    currentEmotion = HAPPY;
  }
  lastTouchState = touchState;

  if (petActive && now - petStart > petDuration) {
    petActive = false;
    if (currentEmotion == HAPPY) currentEmotion = NEUTRAL;
  }

  // --- cek waktunya reminder makan ---
  if (!reminderActive && now >= nextReminderTime) {
    reminderActive = true;
    tone(BUZZER_PIN, 400, 500);
    showMessage("Waktunya " + String(mealNames[mealIndex]) + "!");
  }

  // --- BTN_SNOOZE: tunda reminder, 3x berturut-turut = marah ---
  bool snoozeBtnState = digitalRead(BTN_SNOOZE);
  if (lastSnoozeBtnState == HIGH && snoozeBtnState == LOW && reminderActive) {
    reminderActive = false;
    snoozeCount++;

    if (snoozeCount >= maxSnoozeBeforeAngry) {
      // udah kesabaran habis: snooze cuma dikasih 5 menit + marah
      nextReminderTime = now + shortSnoozeInterval;
      currentEmotion = ANGRY;
      angryActive = true;
      angryStart = now;
      tone(BUZZER_PIN, 300, 400);       // nada rendah & panjang = marah
      snoozeCount = 0;                  // reset buat siklus berikutnya
    } else {
      nextReminderTime = now + snoozeInterval;
      tone(BUZZER_PIN, 1200, 150);      // nada konfirmasi biasa
    }

    mealIndex = (mealIndex + 1) % 3;
    showOverlay = false;
  }
  lastSnoozeBtnState = snoozeBtnState;

  // --- balikin dari ANGRY setelah beberapa detik ---
  if (angryActive && now - angryStart > angryDuration) {
    angryActive = false;
    if (currentEmotion == ANGRY) currentEmotion = NEUTRAL;
  }

  // --- BTN_TODO: tampilkan jadwal berikutnya ---
  bool todoBtnState = digitalRead(BTN_TODO);
  if (lastTodoBtnState == HIGH && todoBtnState == LOW) {
    showMessage("Next: " + String(mealNames[mealIndex]));
  }
  lastTodoBtnState = todoBtnState;

  // --- matikan overlay setelah durasinya habis ---
  if (showOverlay && now - overlayStart > overlayDuration) {
    showOverlay = false;
  }

  // --- animasi mata & kedip ---
  EyeShape target = shapes[currentEmotion];
  curW = ease(curW, target.w);
  curH = ease(curH, target.h);

  if (!isBlinking && now - lastBlink > blinkInterval) {
    isBlinking = true;
    blinkStart = now;
  }
  if (isBlinking && now - blinkStart > blinkDuration) {
    isBlinking = false;
    lastBlink = now;
  }

  if (currentEmotion == EATING && now - lastChew > chewInterval) {
    mouthOpen = !mouthOpen;
    lastChew = now;
    if (mouthOpen) {
      tone(BUZZER_PIN, 900, 80);
    } else {
      tone(BUZZER_PIN, 600, 80);
    }
  }
}

void drawEyes() {
  int cx1 = 40, cx2 = 88, cy = 32;
  float h = curH;
  if (isBlinking) {
    float t = (millis() - blinkStart) / (float)blinkDuration;
    float blinkFactor = (t < 0.5) ? (1 - t*2) : (t*2 - 1);
    h = curH * max(blinkFactor, 0.08f);
  }
  drawEye(cx1, cy, curW, h);
  drawEye(cx2, cy, curW, h);
}

void drawEye(int cx, int cy, float w, float h) {
  display.fillRoundRect(cx - w/2, cy - h/2, w, h, 6, SH110X_WHITE);
}

void drawMouth() {
  int mouthH = mouthOpen ? 14 : 4;
  display.fillRoundRect(50, 46, 28, mouthH, 3, SH110X_WHITE);
}

void drawOverlay() {
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(overlayText, 0, 0, &x1, &y1, &w, &h);
  int cx = (SCREEN_WIDTH - w) / 2;
  int cy = (SCREEN_HEIGHT - h) / 2;
  display.setCursor(cx, cy);
  display.print(overlayText);
}

void loop() {
  display.clearDisplay();

  updateState();

  if (showOverlay) {
    drawOverlay();
  } else {
    drawEyes();
    if (currentEmotion == EATING) {
      drawMouth();
    }
  }

  display.display();
  delay(16);
}