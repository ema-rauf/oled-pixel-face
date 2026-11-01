#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ---------- Hardware config ----------
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
#define OLED_ADDR     0x3C      // try 0x3D if the screen stays blank
#define SDA_PIN       21
#define SCL_PIN       22
#define BTN_NEXT      25
#define BTN_SLEEP     26

// ---------- Timing ----------
const uint32_t DEBOUNCE_MS      = 30;
const uint32_t LONG_PRESS_MS    = 700;
const uint32_t AUTO_INTERVAL_MS = 2500;
const uint32_t TOAST_MS         = 900;
const uint32_t BLINK_MS         = 130;

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ---------- Faces ----------
enum Face { HAPPY, SURPRISED, SLEEPY, CURIOUS, FACE_COUNT };

// ---------- Button handling ----------
enum ButtonEvent { EV_NONE, EV_SHORT, EV_LONG };

struct Button {
  uint8_t  pin;
  bool     rawLast   = false;
  bool     pressed   = false;
  uint32_t changedAt = 0;
  uint32_t pressedAt = 0;
  bool     longFired = false;

  Button(uint8_t p) : pin(p) {}

  ButtonEvent update(uint32_t now) {
    bool raw = (digitalRead(pin) == LOW);
    if (raw != rawLast) {
      rawLast = raw;
      changedAt = now;
    }
    if ((now - changedAt) >= DEBOUNCE_MS && raw != pressed) {
      pressed = raw;
      if (pressed) {
        pressedAt = now;
        longFired = false;
      } else if (!longFired) {
        return EV_SHORT;          // released before the long-press threshold
      }
    }
    if (pressed && !longFired && (now - pressedAt) >= LONG_PRESS_MS) {
      longFired = true;
      return EV_LONG;             // fires while still held
    }
    return EV_NONE;
  }
};

Button nextBtn(BTN_NEXT);
Button sleepBtn(BTN_SLEEP);

// ---------- State ----------
Face     face        = HAPPY;
bool     sleeping    = false;
bool     autoMode    = false;
bool     blinking    = false;
bool     toastOn     = false;
bool     toastAuto   = false;   // text shown in toast: AUTO ON / AUTO OFF
bool     needsRedraw = true;
uint32_t lastAutoAt  = 0;
uint32_t blinkEndAt  = 0;
uint32_t nextBlinkAt = 0;
uint32_t toastUntil  = 0;
uint8_t  lastZPhase  = 255;

// ---------- Drawing helpers (16 x 8 grid of 8x8 pixel cells) ----------
static inline void cell(int c, int r, int w = 1, int h = 1) {
  display.fillRect(c * 8, r * 8, w * 8, h * 8, SSD1306_WHITE);
}

static void ringEye(int x, int y, int size) {
  // Solid square with a dark inside and a bright pupil
  display.fillRect(x, y, size, size, SSD1306_WHITE);
  display.fillRect(x + 4, y + 4, size - 8, size - 8, SSD1306_BLACK);
  display.fillRect(x + size / 2 - 1, y + size / 2 - 1, 4, 4, SSD1306_WHITE);
}

// ---------- Faces ----------
void drawHappy() {
  // ^ ^ eyes
  cell(4, 2);  cell(3, 3);  cell(5, 3);
  cell(11, 2); cell(10, 3); cell(12, 3);
  // big smile
  cell(4, 5);  cell(11, 5);
  cell(5, 6, 6, 1);
}

void drawSurprised() {
  // raised brows
  cell(3, 0, 2, 1);
  cell(11, 0, 2, 1);
  if (blinking) {
    display.fillRect(24, 22, 16, 4, SSD1306_WHITE);
    display.fillRect(88, 22, 16, 4, SSD1306_WHITE);
  } else {
    ringEye(24, 16, 16);
    ringEye(88, 16, 16);
  }
  // open mouth "O"
  cell(7, 5, 2, 1);
  cell(6, 6);
  cell(9, 6);
  cell(7, 7, 2, 1);
}

void drawSleepy() {
  // heavy half-closed lids
  display.fillRect(24, 28, 24, 8, SSD1306_WHITE);
  display.fillRect(80, 28, 24, 8, SSD1306_WHITE);
  // small flat mouth
  cell(7, 6, 2, 1);
  // little z
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(2);
  display.setCursor(108, 0);
  display.print("z");
}

void drawCurious() {
  if (blinking) {
    display.fillRect(24, 34, 16, 4, SSD1306_WHITE);
    display.fillRect(80, 28, 24, 4, SSD1306_WHITE);
  } else {
    // small left eye, big right eye with pupil glancing to the side
    cell(3, 3, 2, 2);
    display.fillRect(80, 16, 24, 24, SSD1306_WHITE);
    display.fillRect(88, 24, 8, 8, SSD1306_BLACK);
    display.fillRect(92, 26, 4, 4, SSD1306_WHITE);
  }
  // one low brow, one raised brow
  display.fillRect(24, 16, 16, 4, SSD1306_WHITE);
  cell(10, 0, 3, 1);
  // smirk
  cell(5, 6, 4, 1);
  cell(9, 5);
}

void drawSleeping() {
  // closed eyes: V shapes
  cell(3, 3);  cell(4, 4);  cell(5, 3);
  cell(10, 3); cell(11, 4); cell(12, 3);

  // breathing mouth
  uint8_t zPhase = (millis() / 700) % 4;
  if (zPhase % 2 == 0) cell(7, 6, 2, 1);
  else                 cell(7, 6, 2, 2);

  // floating Zzz
  display.setTextColor(SSD1306_WHITE);
  if (zPhase >= 1) { display.setTextSize(1); display.setCursor(88, 16); display.print("z"); }
  if (zPhase >= 2) { display.setTextSize(2); display.setCursor(98, 4);  display.print("Z"); }
  if (zPhase >= 3) { display.setTextSize(2); display.setCursor(112, 0); display.print("Z"); }
}

void drawToast() {
  display.fillRect(0, 56, 54, 8, SSD1306_BLACK);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 56);
  display.print(toastAuto ? "AUTO ON" : "AUTO OFF");
}

void render() {
  display.clearDisplay();
  if (sleeping) {
    drawSleeping();
  } else {
    switch (face) {
      case HAPPY:     drawHappy();     break;
      case SURPRISED: drawSurprised(); break;
      case SLEEPY:    drawSleepy();    break;
      case CURIOUS:   drawCurious();   break;
      default: break;
    }
  }
  if (toastOn) drawToast();
  display.display();
}

// ---------- Helpers ----------
void scheduleBlink(uint32_t now) {
  nextBlinkAt = now + random(2500, 5000);
}

void changeFace(int dir, uint32_t now) {
  face = (Face)((face + FACE_COUNT + dir) % FACE_COUNT);
  lastAutoAt = now;
  blinking = false;
  needsRedraw = true;
}

// ---------- Arduino ----------
void setup() {
  Serial.begin(115200);
  pinMode(BTN_NEXT, INPUT_PULLUP);
  pinMode(BTN_SLEEP, INPUT_PULLUP);

  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(400000);

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("SSD1306 not found. Check wiring (VCC->3V3, GND, SDA->21, SCL->22) or try address 0x3D.");
    while (true) delay(1000);
  }

  randomSeed(micros());
  uint32_t now = millis();
  lastAutoAt = now;
  scheduleBlink(now);
  render();
}

void loop() {
  uint32_t now = millis();

  ButtonEvent e1 = nextBtn.update(now);
  ButtonEvent e2 = sleepBtn.update(now);

  // --- Sleep / wake button ---
  if (e2 == EV_SHORT) {
    sleeping = !sleeping;
    blinking = false;
    lastAutoAt = now;
    lastZPhase = 255;
    scheduleBlink(now);
    needsRedraw = true;
  } else if (e2 == EV_LONG && !sleeping) {
    autoMode = !autoMode;
    lastAutoAt = now;
    toastAuto = autoMode;
    toastOn = true;
    toastUntil = now + TOAST_MS;
    needsRedraw = true;
  }

  // --- Next-expression button (ignored while asleep) ---
  if (!sleeping) {
    if (e1 == EV_SHORT) {
      autoMode = false;
      changeFace(+1, now);
    } else if (e1 == EV_LONG) {
      autoMode = false;
      changeFace(-1, now);
    }
  }

  // --- Automatic mode ---
  if (!sleeping && autoMode && (now - lastAutoAt) >= AUTO_INTERVAL_MS) {
    changeFace(+1, now);
  }

  // --- Blinking (awake only) ---
  if (!sleeping) {
    if (!blinking && (int32_t)(now - nextBlinkAt) >= 0) {
      blinking = true;
      blinkEndAt = now + BLINK_MS;
      scheduleBlink(now);
      needsRedraw = true;
    } else if (blinking && (int32_t)(now - blinkEndAt) >= 0) {
      blinking = false;
      needsRedraw = true;
    }
  } else {
    // animate Zzz / breathing while asleep
    uint8_t zPhase = (now / 700) % 4;
    if (zPhase != lastZPhase) {
      lastZPhase = zPhase;
      needsRedraw = true;
    }
  }

  // --- Toast expiry ---
  if (toastOn && (int32_t)(now - toastUntil) >= 0) {
    toastOn = false;
    needsRedraw = true;
  }

  if (needsRedraw) {
    needsRedraw = false;
    render();
  }
}
