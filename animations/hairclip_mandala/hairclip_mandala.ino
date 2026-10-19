// Hairclip - mandala pattern (LilyGO T-Display-S3, GFX library)
#include <Arduino_GFX_Library.h>
#include <math.h>

#define PIN_POWER_ON 15
#define PIN_LCD_BL   38

Arduino_DataBus *bus = new Arduino_ESP32LCD8(
    7, 6, 8, 9, 39, 40, 41, 42, 45, 46, 47, 48);
Arduino_GFX *gfx = new Arduino_ST7789(
    bus, 5, 3 /* rotation */, true, 170, 320, 35, 0, 35, 0);

Arduino_Canvas *canvas;
const int W = 320, H = 170;

static inline uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}
const uint16_t BG   = rgb(4, 2, 14);       // deep indigo
const uint16_t GOLD = rgb(255, 190, 90);
const uint16_t PINK = rgb(255, 105, 180);
const uint16_t TEAL = rgb(80, 220, 210);
const uint16_t DIM  = rgb(60, 70, 130);
const uint16_t WHT  = 0xFFFF;

// One leaf-shaped petal pointing away from the centre at angle 'ang'.
// r0 = distance of its base from the centre, L = length, w = half-width.
void petal(float cx, float cy, float ang, float r0, float L, float w, uint16_t c) {
  float ca = cosf(ang), sa = sinf(ang);
  const int STEPS = 6;
  for (int side = -1; side <= 1; side += 2) {
    float px = 0, py = 0;
    for (int i = 0; i <= STEPS; i++) {
      float s   = i / (float)STEPS;
      float rad = r0 + L * s;                    // along the petal
      float tan = side * w * sinf(PI * s);       // sideways bulge
      float x = cx + rad * ca - tan * sa;
      float y = cy + rad * sa + tan * ca;
      if (i > 0) canvas->drawLine((int)px, (int)py, (int)x, (int)y, c);
      px = x; py = y;
    }
  }
}

// A full mandala: 3 counter-rotating petal rings, circles, chasing dots.
void mandala(float cx, float cy, float s, float t, bool dots) {
  float breathe = 1.0f + 0.10f * sinf(t * 1.2f);

  for (int n = 0; n < 16; n++)                   // outer ring, gold
    petal(cx, cy, n * 2 * PI / 16 + t * 0.25f, 52 * s, 28 * s * breathe, 7 * s, GOLD);
  for (int n = 0; n < 12; n++)                   // middle ring, pink
    petal(cx, cy, n * 2 * PI / 12 - t * 0.40f, 30 * s, 24 * s * breathe, 9 * s, PINK);
  for (int n = 0; n < 8; n++)                    // inner ring, teal
    petal(cx, cy, n * 2 * PI / 8 + t * 0.60f, 8 * s, 20 * s, 6 * s, TEAL);

  canvas->drawCircle((int)cx, (int)cy, (int)(50 * s), DIM);
  canvas->drawCircle((int)cx, (int)cy, (int)(81 * s), DIM);
  canvas->fillCircle((int)cx, (int)cy, (int)(4 * s + 1), WHT);

  if (dots) {                                    // ring of chasing dots
    for (int n = 0; n < 32; n++) {
      float a = n * 2 * PI / 32 + t * 0.2f;
      uint16_t col = (((n + (int)(t * 4)) % 4) == 0) ? WHT : DIM;
      canvas->fillCircle((int)(cx + 83 * cosf(a)), (int)(cy + 83 * sinf(a)), 1, col);
    }
  }
}

void animTask(void *) {
  TickType_t last = xTaskGetTickCount();
  float t = 0;
  for (;;) {
    canvas->fillScreen(BG);

    mandala(40,  H / 2, 0.42f, -t, false);   // small left, spins the other way
    mandala(280, H / 2, 0.42f, -t, false);   // small right
    mandala(W / 2, H / 2, 1.0f,  t, true);   // big centre

    canvas->flush();
    t += 0.05f;
    vTaskDelayUntil(&last, pdMS_TO_TICKS(33));   // ~30 fps
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_POWER_ON, OUTPUT);
  digitalWrite(PIN_POWER_ON, HIGH);

  canvas = new Arduino_Canvas(W, H, gfx);
  if (!canvas->begin(10000000)) {
    Serial.println("canvas/display begin failed - check PSRAM = OPI PSRAM");
    while (true) delay(1000);
  }
  canvas->fillScreen(BG);
  canvas->flush();

  pinMode(PIN_LCD_BL, OUTPUT);
  digitalWrite(PIN_LCD_BL, HIGH);

  xTaskCreatePinnedToCore(animTask, "anim", 6144, NULL, 2, NULL, 1);
  Serial.println("mandala task started");
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}