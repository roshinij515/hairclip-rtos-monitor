// Hairclip - neon heart with heartbeat + ECG (LilyGO T-Display-S3, GFX library)
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
const int CX = W / 2, CY = H / 2;

static inline uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}
static inline uint16_t fade(uint8_t r, uint8_t g, uint8_t b, float f) {
  if (f < 0) f = 0;
  return rgb((uint8_t)(r * f), (uint8_t)(g * f), (uint8_t)(b * f));
}

const uint16_t BG     = rgb(6, 4, 14);
const uint16_t HALO2  = rgb(30, 6, 24);
const uint16_t HALO1  = rgb(62, 10, 44);
const uint16_t LAYER[4] = { rgb(200, 20, 90), rgb(235, 45, 115),
                            rgb(255, 95, 155), rgb(255, 150, 190) };
const uint16_t NEON   = rgb(255, 130, 190);
const uint16_t WHT    = 0xFFFF;

// ---- heart shape, computed once ----
const int N = 48;
float ux[N + 1], uy[N + 1];

void buildHeart() {
  for (int i = 0; i <= N; i++) {
    float t = i * 2 * PI / N;
    float s = sinf(t);
    ux[i] = 16 * s * s * s;
    uy[i] = 13 * cosf(t) - 5 * cosf(2 * t) - 2 * cosf(3 * t) - cosf(4 * t);
  }
}

// Heart point i at scale s, centred on (cx,cy)  (y flipped for the screen)
static inline int px(int i, float cx, float s) { return (int)(cx + ux[i] * s); }
static inline int py(int i, float cy, float s) { return (int)(cy - (uy[i] + 2.5f) * s); }

void heartFill(float cx, float cy, float s, uint16_t col) {
  int ox = (int)cx, oy = (int)(cy - 2.5f * s);   // fan centre inside the heart
  for (int i = 0; i < N; i++)
    canvas->fillTriangle(ox, oy, px(i, cx, s), py(i, cy, s),
                         px(i + 1, cx, s), py(i + 1, cy, s), col);
}

void heartLine(float cx, float cy, float s, uint16_t col) {
  for (int i = 0; i < N; i++)
    canvas->drawLine(px(i, cx, s), py(i, cy, s),
                     px(i + 1, cx, s), py(i + 1, cy, s), col);
}

// One ECG beat shape, u runs 0..1
float ecg(float u) {
  if (u < 0.30f) return 0;
  if (u < 0.38f) return 5 * sinf((u - 0.30f) / 0.08f * PI);    // P
  if (u < 0.46f) return 0;
  if (u < 0.49f) return -8 * sinf((u - 0.46f) / 0.03f * PI);   // Q
  if (u < 0.55f) return 36 * sinf((u - 0.49f) / 0.06f * PI);   // R spike
  if (u < 0.59f) return -12 * sinf((u - 0.55f) / 0.04f * PI);  // S
  if (u < 0.72f) return 0;
  if (u < 0.84f) return 9 * sinf((u - 0.72f) / 0.12f * PI);    // T
  return 0;
}

void animTask(void *) {
  TickType_t last = xTaskGetTickCount();
  float phase = 0;       // heartbeat phase 0..1  (~72 bpm)
  float t = 0;           // general time
  const float S0 = 3.4f; // heart size

  for (;;) {
    canvas->fillScreen(BG);

    // --- ECG trace behind everything ---
    int pxl = 0, pyl = CY;
    for (int x = 0; x < W; x += 2) {
      float u = x * 0.009f - t * 0.9f;
      u -= floorf(u);
      int y = CY - (int)ecg(u);
      if (x > 0) canvas->drawLine(pxl, pyl, x, y, rgb(30, 130, 135));
      pxl = x; pyl = y;
    }

    // --- heartbeat: lub-dub ---
    float b1 = expf(-powf((phase - 0.12f) / 0.05f, 2));
    float b2 = expf(-powf((phase - 0.30f) / 0.06f, 2)) * 0.7f;
    float beat = 1.0f + 0.12f * (b1 + b2);
    float s = S0 * beat;

    // --- ripple on each beat ---
    if (phase > 0.12f) {
      float u = (phase - 0.12f) / 0.88f;
      heartLine(CX, CY, S0 * (1.05f + 0.30f * u), fade(255, 100, 170, 0.8f * (1 - u)));
    }

    // --- glow halo ---
    heartFill(CX, CY, s * 1.22f, HALO2);
    heartFill(CX, CY, s * 1.10f, HALO1);

    // --- gradient body: smaller, lighter layers shifted up-left ---
    float sc[4] = { 1.0f, 0.78f, 0.56f, 0.34f };
    for (int k = 0; k < 4; k++)
      heartFill(CX - 1.5f * k, CY - 2.5f * k, s * sc[k], LAYER[k]);

    // --- neon outline (drawn twice for thickness) + highlight ---
    heartLine(CX, CY, s * 1.0f, NEON);
    heartLine(CX + 1, CY, s * 1.0f, NEON);
    canvas->fillCircle(CX - (int)(8.5f * s * 0.80f), CY - (int)(10 * s * 0.80f), 3, WHT);

    canvas->flush();
    phase += 0.04f; if (phase >= 1.0f) phase -= 1.0f;
    t += 0.05f;
    vTaskDelayUntil(&last, pdMS_TO_TICKS(33));   // ~30 fps
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_POWER_ON, OUTPUT);
  digitalWrite(PIN_POWER_ON, HIGH);

  buildHeart();
  canvas = new Arduino_Canvas(W, H, gfx);
  if (!canvas->begin(10000000)) {
    Serial.println("canvas/display begin failed - check PSRAM = OPI PSRAM");
    while (true) delay(1000);
  }
  canvas->fillScreen(BG);
  canvas->flush();

  pinMode(PIN_LCD_BL, OUTPUT);
  digitalWrite(PIN_LCD_BL, HIGH);

  xTaskCreatePinnedToCore(animTask, "anim", 8192, NULL, 2, NULL, 1);
  Serial.println("heart task started");
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}