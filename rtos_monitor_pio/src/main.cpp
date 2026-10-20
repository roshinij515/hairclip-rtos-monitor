// Hairclip + RTOS monitor v3: battery, PWM brightness, dim/sleep, monitor-on-demand,
// plus power savings (80 MHz CPU, 20 fps patterns, lower default brightness)
#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include "esp_timer.h"
#include "esp_freertos_hooks.h"
#include <math.h>
#undef RED

struct Snapshot;   // forward declaration (Arduino adds function prototypes above the struct)

#define PIN_POWER_ON 15
#define PIN_LCD_BL   38
#define PIN_BTN_NEXT 0     // BOOT button
#define PIN_BTN_PREV 14    // second button
#define PIN_BAT_VOLT 4     // battery voltage sense (via 1:2 divider)

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

// ---- colours: monitor ----
const uint16_t BG   = rgb(6, 8, 20);
const uint16_t ICE  = rgb(190, 215, 255);
const uint16_t TEAL = rgb(80, 220, 210);
const uint16_t PINK = rgb(255, 105, 180);
const uint16_t DIM  = rgb(60, 70, 130);
const uint16_t GRID = rgb(24, 30, 60);
const uint16_t GRN  = rgb(70, 220, 120);
const uint16_t YEL  = rgb(250, 200, 70);
const uint16_t RED  = rgb(250, 80, 90);
const uint16_t WHT  = 0xFFFF;
// ---- colours: patterns ----
const uint16_t BG_HEART = rgb(6, 4, 14);
const uint16_t HALO2    = rgb(30, 6, 24);
const uint16_t HALO1    = rgb(62, 10, 44);
const uint16_t LAYER[4] = { rgb(200, 20, 90), rgb(235, 45, 115),
                            rgb(255, 95, 155), rgb(255, 150, 190) };
const uint16_t NEON     = rgb(255, 130, 190);
const uint16_t BG_MAN   = rgb(4, 2, 14);
const uint16_t GOLD     = rgb(255, 190, 90);
const uint16_t BG_RIB   = rgb(2, 4, 18);
const uint16_t BLU      = rgb(70, 130, 230);

// =====================================================================
//  POWER MANAGEMENT STATE
// =====================================================================
enum PowerState { P_ACTIVE, P_DIM, P_OFF };
volatile PowerState powerState = P_ACTIVE;
volatile uint32_t lastActivityMs = 0;
volatile uint32_t toastUntil = 0;
volatile bool monitorActive = true;       // true only while a monitor screen is showing
const uint8_t BRIGHT_LEVELS[4] = { 255, 130, 60, 20 };
volatile int brightIdx = 1;               // start at level 2 of 4 (saves power)
const uint32_t DIM_AFTER_MS = 30000;      // dim after 30 s without a press
const uint32_t OFF_AFTER_MS = 120000;     // screen off after 2 min

void applyBacklight() {
  uint8_t d = BRIGHT_LEVELS[brightIdx];
  if (powerState == P_DIM && d > 12) d = 12;
  if (powerState == P_OFF) d = 0;
  ledcWrite(0, d);
}

// =====================================================================
//  BATTERY
// =====================================================================
volatile float battV = 0;
volatile int battPct = 0;

int pctFromVolts(float v) {                // rough LiPo curve
  static const float tv[9] = { 3.30f, 3.50f, 3.60f, 3.70f, 3.80f, 3.90f, 4.00f, 4.10f, 4.20f };
  static const int   tp[9] = { 0, 5, 10, 20, 40, 60, 80, 90, 100 };
  if (v <= tv[0]) return 0;
  if (v >= tv[8]) return 100;
  for (int i = 1; i < 9; i++) {
    if (v <= tv[i]) {
      float f = (v - tv[i - 1]) / (tv[i] - tv[i - 1]);
      return (int)(tp[i - 1] + f * (tp[i] - tp[i - 1]));
    }
  }
  return 100;
}

void batteryTask(void *) {
  analogSetPinAttenuation(PIN_BAT_VOLT, ADC_11db);
  for (;;) {
    uint32_t sum = 0;
    for (int i = 0; i < 8; i++) sum += analogReadMilliVolts(PIN_BAT_VOLT);
    float v = (sum / 8.0f) * 2.0f / 1000.0f;       // x2 for the 1:2 divider
    battV = v;
    battPct = pctFromVolts(v);
    vTaskDelay(pdMS_TO_TICKS(2000));
  }
}

// =====================================================================
//  TASK REGISTRY + DEMO / FAULT TASKS
// =====================================================================
struct TaskInfo {
  const char *name; TaskHandle_t handle; uint32_t stackBytes;
  bool tracked; volatile int64_t busyUs; int64_t prevBusyUs;
};
TaskInfo registry[16];
int registryCount = 0;

int registerExisting(const char *name, TaskHandle_t h, uint32_t stackBytes) {
  int i = registryCount++;
  registry[i].name = name; registry[i].handle = h; registry[i].stackBytes = stackBytes;
  registry[i].tracked = false; registry[i].busyUs = 0; registry[i].prevBusyUs = 0;
  return i;
}
int addTask(TaskFunction_t fn, const char *name, uint32_t stack,
            void *arg, UBaseType_t prio, bool tracked) {
  int i = registerExisting(name, NULL, stack);
  registry[i].tracked = tracked;
  xTaskCreate(fn, name, stack, arg, prio, &registry[i].handle);
  return i;
}
int addTaskPinned(TaskFunction_t fn, const char *name, uint32_t stack, void *arg,
                  UBaseType_t prio, bool tracked, int core) {
  int i = registerExisting(name, NULL, stack);
  registry[i].tracked = tracked;
  xTaskCreatePinnedToCore(fn, name, stack, arg, prio, &registry[i].handle, core);
  return i;
}

struct WorkCfg { int idx; uint32_t loops; uint32_t sleepMs; };
WorkCfg cfgHeavy = { 0, 2000000, 50 };
void workTask(void *p) {
  WorkCfg *c = (WorkCfg *)p;
  for (;;) {
    if (!monitorActive) { vTaskDelay(pdMS_TO_TICKS(200)); continue; }   // idle unless watched
    int64_t t0 = esp_timer_get_time();
    volatile uint32_t x = 0;
    for (uint32_t i = 0; i < c->loops; i++) x += i;
    registry[c->idx].busyUs += esp_timer_get_time() - t0;
    vTaskDelay(pdMS_TO_TICKS(c->sleepMs));
  }
}

volatile bool leakOn = false, hogOn = false, stackGo = false, clearLeaks = false;
void *leaks[500];
int leakCount = 0;
int hogIdx = 0;

void leakTask(void *) {
  for (;;) {
    if (clearLeaks) {
      for (int i = 0; i < leakCount; i++) free(leaks[i]);
      leakCount = 0;
      clearLeaks = false;
    }
    if (leakOn && leakCount < 500 && ESP.getFreeHeap() > 80 * 1024) {
      void *p = malloc(1024);
      if (p) { memset(p, 0xAA, 1024); leaks[leakCount++] = p; }
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}
void hogTask(void *) {
  for (;;) {
    if (hogOn) {
      int64_t t0 = esp_timer_get_time();
      volatile uint32_t x = 0;
      while (esp_timer_get_time() - t0 < 40000) x++;
      registry[hogIdx].busyUs += esp_timer_get_time() - t0;
      vTaskDelay(pdMS_TO_TICKS(10));
    } else {
      vTaskDelay(pdMS_TO_TICKS(100));
    }
  }
}
void stackTask(void *) {
  for (;;) {
    if (stackGo) {
      volatile char big[1800];
      for (int i = 0; i < 1800; i++) big[i] = (char)i;
      vTaskDelay(pdMS_TO_TICKS(1000));
      stackGo = false;
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

// =====================================================================
//  CPU IDLE COUNTERS + SNAPSHOT + MONITOR TASK
// =====================================================================
volatile uint32_t idleCnt[2] = { 0, 0 };
uint32_t idleBase[2] = { 1, 1 };
// returns false (= keep spinning, accurate counts) only while a monitor screen is
// showing; otherwise returns true so the core may sleep between interrupts.
bool idleHook0() { idleCnt[0]++; return !monitorActive; }
bool idleHook1() { idleCnt[1]++; return !monitorActive; }

struct Row { char name[16]; char st[4]; uint8_t pri; uint8_t stackPct;
             float cpu; bool tracked; char core; };
struct Snapshot { uint32_t seq; int n; Row rows[12]; float load[2];
                  uint32_t heapFree, heapMin, heapTotal; float battV; int battPct; };
Snapshot snap;
SemaphoreHandle_t snapMutex;

const char *stateName(eTaskState s) {
  switch (s) {
    case eRunning:   return "RUN";
    case eReady:     return "RDY";
    case eBlocked:   return "BLK";
    case eSuspended: return "SUS";
    default:         return "?";
  }
}

void monitorTask(void *) {
  int64_t lastUs = esp_timer_get_time();
  uint32_t prevIdle[2] = { idleCnt[0], idleCnt[1] };
  uint32_t seq = 0;
  bool wasActive = false;
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(500));
    if (!monitorActive) { wasActive = false; continue; }     // nobody is looking
    int64_t now = esp_timer_get_time();
    if (!wasActive) {                                         // just started: restart window
      wasActive = true;
      lastUs = now;
      prevIdle[0] = idleCnt[0]; prevIdle[1] = idleCnt[1];
      for (int i = 0; i < registryCount; i++) registry[i].prevBusyUs = registry[i].busyUs;
      continue;
    }
    float dt = (now - lastUs) / 1e6f;
    lastUs = now;

    Snapshot s;
    s.seq = ++seq;
    s.n = registryCount;
    for (int i = 0; i < registryCount; i++) {
      TaskHandle_t h = registry[i].handle;
      Row &r = s.rows[i];
      strncpy(r.name, registry[i].name, 15); r.name[15] = 0;
      strncpy(r.st, stateName(eTaskGetState(h)), 3); r.st[3] = 0;
      r.pri = uxTaskPriorityGet(h);
      r.stackPct = (uint8_t)(100.0f * (registry[i].stackBytes -
                   uxTaskGetStackHighWaterMark(h)) / registry[i].stackBytes);
      r.tracked = registry[i].tracked;
      if (r.tracked) {
        int64_t b = registry[i].busyUs;
        r.cpu = 100.0f * (b - registry[i].prevBusyUs) / 1e6f / dt;
        registry[i].prevBusyUs = b;
      } else r.cpu = 0;
      BaseType_t core = xTaskGetAffinity(h);
      r.core = (core == tskNO_AFFINITY) ? '*' : (core == 0 ? '0' : '1');
    }
    for (int c = 0; c < 2; c++) {
      uint32_t d = idleCnt[c] - prevIdle[c];
      prevIdle[c] = idleCnt[c];
      float l = 100.0f * (1.0f - d / dt / (float)idleBase[c]);
      s.load[c] = l < 0 ? 0 : (l > 100 ? 100 : l);
    }
    s.heapFree  = ESP.getFreeHeap();
    s.heapMin   = ESP.getMinFreeHeap();
    s.heapTotal = ESP.getHeapSize();
    s.battV     = battV;
    s.battPct   = battPct;

    xSemaphoreTake(snapMutex, portMAX_DELAY);
    snap = s;
    xSemaphoreGive(snapMutex);
  }
}

// =====================================================================
//  BUTTONS  (BOOT = next;  second button: tap = previous, hold 0.8 s = brightness)
// =====================================================================
enum BtnEvent : uint8_t { BTN_NEXT, BTN_PREV, BTN_BRIGHT };
QueueHandle_t btnQueue;

void sendBtn(uint8_t e) { xQueueSend(btnQueue, &e, 0); }

void buttonTask(void *) {
  pinMode(PIN_BTN_NEXT, INPUT_PULLUP);
  pinMode(PIN_BTN_PREV, INPUT_PULLUP);
  bool nextDown = false, prevDown = false, longFired = false;
  uint32_t prevT = 0;
  for (;;) {
    bool n = (digitalRead(PIN_BTN_NEXT) == LOW);
    bool p = (digitalRead(PIN_BTN_PREV) == LOW);
    uint32_t t = millis();

    if (n && !nextDown) sendBtn(BTN_NEXT);

    if (p && !prevDown) { prevT = t; longFired = false; }
    if (p && !longFired && (t - prevT) >= 800) { sendBtn(BTN_BRIGHT); longFired = true; }
    if (!p && prevDown && !longFired) sendBtn(BTN_PREV);

    nextDown = n; prevDown = p;
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

// =====================================================================
//  DRAWING HELPERS
// =====================================================================
void text(int x, int y, uint16_t col, const char *s) {
  canvas->setTextColor(col);
  canvas->setCursor(x, y);
  canvas->print(s);
}
uint16_t loadColor(float p) { return p < 50 ? GRN : (p < 80 ? YEL : RED); }
void bar(int x, int y, int w, int h, float pct, uint16_t col) {
  canvas->drawRect(x, y, w, h, DIM);
  int fw = (int)((w - 2) * pct / 100.0f);
  if (fw > 0) canvas->fillRect(x + 1, y + 1, fw, h - 2, col);
}

const int NSCR = 6;      // screens 0-2 = hairclip patterns, 3-5 = monitor
void header(const char *title, int screen) {
  char b[40];
  snprintf(b, sizeof(b), "%s  %d/%d", title, screen + 1, NSCR);
  text(4, 2, TEAL, b);
  canvas->drawFastHLine(0, 12, W, DIM);
}

// =====================================================================
//  PATTERN 1: NEON HEART + ECG
// =====================================================================
const int HN_PTS = 48;
float ux[HN_PTS + 1], uy[HN_PTS + 1];

void buildHeart() {
  for (int i = 0; i <= HN_PTS; i++) {
    float t = i * 2 * PI / HN_PTS;
    float s = sinf(t);
    ux[i] = 16 * s * s * s;
    uy[i] = 13 * cosf(t) - 5 * cosf(2 * t) - 2 * cosf(3 * t) - cosf(4 * t);
  }
}
static inline int hpx(int i, float cx, float s) { return (int)(cx + ux[i] * s); }
static inline int hpy(int i, float cy, float s) { return (int)(cy - (uy[i] + 2.5f) * s); }

void heartFill(float cx, float cy, float s, uint16_t col) {
  int ox = (int)cx, oy = (int)(cy - 2.5f * s);
  for (int i = 0; i < HN_PTS; i++)
    canvas->fillTriangle(ox, oy, hpx(i, cx, s), hpy(i, cy, s),
                         hpx(i + 1, cx, s), hpy(i + 1, cy, s), col);
}
void heartLine(float cx, float cy, float s, uint16_t col) {
  for (int i = 0; i < HN_PTS; i++)
    canvas->drawLine(hpx(i, cx, s), hpy(i, cy, s),
                     hpx(i + 1, cx, s), hpy(i + 1, cy, s), col);
}
float ecg(float u) {
  if (u < 0.30f) return 0;
  if (u < 0.38f) return 5 * sinf((u - 0.30f) / 0.08f * PI);
  if (u < 0.46f) return 0;
  if (u < 0.49f) return -8 * sinf((u - 0.46f) / 0.03f * PI);
  if (u < 0.55f) return 36 * sinf((u - 0.49f) / 0.06f * PI);
  if (u < 0.59f) return -12 * sinf((u - 0.55f) / 0.04f * PI);
  if (u < 0.72f) return 0;
  if (u < 0.84f) return 9 * sinf((u - 0.72f) / 0.12f * PI);
  return 0;
}

void drawHeart(float now) {
  canvas->fillScreen(BG_HEART);
  float phase = fmodf(now * 1.2f, 1.0f);
  float tt = now * 1.5f;
  const float S0 = 3.4f;

  int pxl = 0, pyl = CY;
  for (int x = 0; x < W; x += 2) {
    float u = x * 0.009f - tt * 0.9f;
    u -= floorf(u);
    int y = CY - (int)ecg(u);
    if (x > 0) canvas->drawLine(pxl, pyl, x, y, rgb(30, 130, 135));
    pxl = x; pyl = y;
  }

  float b1 = expf(-powf((phase - 0.12f) / 0.05f, 2));
  float b2 = expf(-powf((phase - 0.30f) / 0.06f, 2)) * 0.7f;
  float s = S0 * (1.0f + 0.12f * (b1 + b2));

  if (phase > 0.12f) {
    float u = (phase - 0.12f) / 0.88f;
    heartLine(CX, CY, S0 * (1.05f + 0.30f * u), fade(255, 100, 170, 0.8f * (1 - u)));
  }
  heartFill(CX, CY, s * 1.22f, HALO2);
  heartFill(CX, CY, s * 1.10f, HALO1);
  float sc[4] = { 1.0f, 0.78f, 0.56f, 0.34f };
  for (int k = 0; k < 4; k++)
    heartFill(CX - 1.5f * k, CY - 2.5f * k, s * sc[k], LAYER[k]);
  heartLine(CX, CY, s, NEON);
  heartLine(CX + 1, CY, s, NEON);
  canvas->fillCircle(CX - (int)(8.5f * s * 0.80f), CY - (int)(10 * s * 0.80f), 3, WHT);
}

// =====================================================================
//  PATTERN 2: MANDALA
// =====================================================================
void petal(float cx, float cy, float ang, float r0, float L, float w, uint16_t c) {
  float ca = cosf(ang), sa = sinf(ang);
  const int STEPS = 6;
  for (int side = -1; side <= 1; side += 2) {
    float px = 0, py = 0;
    for (int i = 0; i <= STEPS; i++) {
      float s   = i / (float)STEPS;
      float rad = r0 + L * s;
      float tan = side * w * sinf(PI * s);
      float x = cx + rad * ca - tan * sa;
      float y = cy + rad * sa + tan * ca;
      if (i > 0) canvas->drawLine((int)px, (int)py, (int)x, (int)y, c);
      px = x; py = y;
    }
  }
}
void mandala(float cx, float cy, float s, float t, bool dots) {
  float breathe = 1.0f + 0.10f * sinf(t * 1.2f);
  for (int n = 0; n < 16; n++)
    petal(cx, cy, n * 2 * PI / 16 + t * 0.25f, 52 * s, 28 * s * breathe, 7 * s, GOLD);
  for (int n = 0; n < 12; n++)
    petal(cx, cy, n * 2 * PI / 12 - t * 0.40f, 30 * s, 24 * s * breathe, 9 * s, PINK);
  for (int n = 0; n < 8; n++)
    petal(cx, cy, n * 2 * PI / 8 + t * 0.60f, 8 * s, 20 * s, 6 * s, TEAL);
  canvas->drawCircle((int)cx, (int)cy, (int)(50 * s), DIM);
  canvas->drawCircle((int)cx, (int)cy, (int)(81 * s), DIM);
  canvas->fillCircle((int)cx, (int)cy, (int)(4 * s + 1), WHT);
  if (dots) {
    for (int n = 0; n < 32; n++) {
      float a = n * 2 * PI / 32 + t * 0.2f;
      uint16_t col = (((n + (int)(t * 4)) % 4) == 0) ? WHT : DIM;
      canvas->fillCircle((int)(cx + 83 * cosf(a)), (int)(cy + 83 * sinf(a)), 1, col);
    }
  }
}
void drawMandala(float now) {
  float t = now * 1.5f;
  canvas->fillScreen(BG_MAN);
  mandala(40,  H / 2, 0.42f, -t, false);
  mandala(280, H / 2, 0.42f, -t, false);
  mandala(W / 2, H / 2, 1.0f,  t, true);
}

// =====================================================================
//  PATTERN 3: MIRRORED ORNATE (ribbons + spirals)
// =====================================================================
void line4(int x0, int y0, int x1, int y1, uint16_t c) {
  canvas->drawLine(CX + x0, CY + y0, CX + x1, CY + y1, c);
  canvas->drawLine(CX - x0, CY + y0, CX - x1, CY + y1, c);
  canvas->drawLine(CX + x0, CY - y0, CX + x1, CY - y1, c);
  canvas->drawLine(CX - x0, CY - y0, CX - x1, CY - y1, c);
}
void dot4(int x, int y, int r, uint16_t c) {
  canvas->fillCircle(CX + x, CY + y, r, c);
  canvas->fillCircle(CX - x, CY + y, r, c);
  canvas->fillCircle(CX + x, CY - y, r, c);
  canvas->fillCircle(CX - x, CY - y, r, c);
}
void spiral(float sx, float sy, float k, float aMax, float dir, uint16_t c) {
  float px = sx, py = sy;
  bool first = true;
  for (float a = 0; a <= aMax; a += 0.2f) {
    float r = k * a;
    float x = sx + r * cosf(dir * a);
    float y = sy + r * sinf(dir * a);
    if (!first) line4((int)px, (int)py, (int)x, (int)y, c);
    px = x; py = y; first = false;
  }
}
void drawOrnate(float now) {
  float t = now * 1.8f;
  canvas->fillScreen(BG_RIB);
  canvas->drawRoundRect(2, 2, W - 4, H - 4, 10, BLU);
  canvas->drawRoundRect(6, 6, W - 12, H - 12, 8, DIM);
  for (int k = 0; k < 3; k++) {
    float amp = 10 + 6 * k;
    int px = 14, py = 0;
    for (int x = 14; x <= 150; x += 3) {
      float fadeF = 1.0f - x / 155.0f;
      int y = (int)(amp * sinf(x * 0.06f - t * 1.6f + k * 1.1f) * fadeF) + k * 9;
      if (x > 14) line4(px, py, x, y, (k == 1) ? WHT : ICE);
      px = x; py = y;
    }
  }
  float grow = 0.5f + 0.5f * sinf(t * 0.6f);
  spiral(100, 35, 2.2f, 2.0f * PI + 3.0f * PI * grow,        1.0f, ICE);
  spiral(40,  50, 1.5f, 1.5f * PI + 2.5f * PI * (1 - grow), -1.0f, WHT);
  line4(0, 10, 14, 0, WHT);
  int pr = 2 + (int)(1.5f * (0.5f + 0.5f * sinf(t * 2.0f)));
  dot4(148, 73, pr, ICE);
}

// =====================================================================
//  MONITOR SCREENS
// =====================================================================
void drawTable(const Snapshot &s) {
  char buf[48];
  header("RTOS TASK MONITOR", 3);
  snprintf(buf, sizeof(buf), "heap %uK  bat %.1fV %d%%", s.heapFree / 1024, s.battV, s.battPct);
  text(160, 2, ICE, buf);

  text(4, 16, DIM, "TASK");   text(84, 16, DIM, "ST");  text(108, 16, DIM, "PR");
  text(126, 16, DIM, "STK");  text(160, 16, DIM, "CPU"); text(302, 16, DIM, "C");

  for (int i = 0; i < s.n && i < 8; i++) {
    int y = 28 + i * 12;
    const Row &r = s.rows[i];
    text(4, y, ICE, r.name);
    text(84, y, (r.st[0] == 'R' && r.st[1] == 'U') ? GRN : ICE, r.st);
    snprintf(buf, sizeof(buf), "%u", r.pri);          text(108, y, ICE, buf);
    snprintf(buf, sizeof(buf), "%u%%", r.stackPct);
    text(126, y, r.stackPct > 80 ? RED : ICE, buf);
    if (r.tracked) {
      bar(160, y, 100, 8, r.cpu, loadColor(r.cpu));
      snprintf(buf, sizeof(buf), "%.0f%%", r.cpu);    text(266, y, ICE, buf);
    } else {
      text(160, y, DIM, "n/a");
    }
    buf[0] = r.core; buf[1] = 0;                      text(302, y, TEAL, buf);
  }

  canvas->drawFastHLine(0, 130, W, DIM);
  for (int c = 0; c < 2; c++) {
    int y = 136 + c * 14;
    snprintf(buf, sizeof(buf), "CORE%d", c);          text(4, y, TEAL, buf);
    bar(50, y, 200, 9, s.load[c], loadColor(s.load[c]));
    snprintf(buf, sizeof(buf), "%.0f%%", s.load[c]);  text(256, y, ICE, buf);
  }
}

const int HN = 80;                 // 80 samples x 0.5 s = 40 s of history
float cpuHist[2][HN];
float heapHist[HN];                // KB

void pushHistory(const Snapshot &s) {
  for (int c = 0; c < 2; c++) {
    memmove(&cpuHist[c][0], &cpuHist[c][1], (HN - 1) * sizeof(float));
    cpuHist[c][HN - 1] = s.load[c];
  }
  memmove(&heapHist[0], &heapHist[1], (HN - 1) * sizeof(float));
  heapHist[HN - 1] = s.heapFree / 1024.0f;
}

void drawCpuGraph(const Snapshot &s) {
  char buf[40];
  header("CPU LOAD", 4);
  const int GX = 24, GY = 28, GW = 288, GH = 108;
  canvas->drawRect(GX, GY, GW, GH, DIM);
  for (int pct = 25; pct <= 75; pct += 25) {
    int y = GY + GH - 1 - (GH - 2) * pct / 100;
    canvas->drawFastHLine(GX + 1, y, GW - 2, GRID);
  }
  text(2, GY - 2, DIM, "100"); text(8, GY + GH / 2 - 4, DIM, "50"); text(14, GY + GH - 8, DIM, "0");
  for (int c = 0; c < 2; c++) {
    uint16_t col = c == 0 ? TEAL : PINK;
    for (int i = 1; i < HN; i++) {
      int x0 = GX + 1 + (i - 1) * (GW - 3) / (HN - 1);
      int x1 = GX + 1 + i * (GW - 3) / (HN - 1);
      int y0 = GY + GH - 2 - (int)((GH - 3) * cpuHist[c][i - 1] / 100.0f);
      int y1 = GY + GH - 2 - (int)((GH - 3) * cpuHist[c][i] / 100.0f);
      canvas->drawLine(x0, y0, x1, y1, col);
    }
  }
  snprintf(buf, sizeof(buf), "CORE0 %.0f%%", s.load[0]); text(GX, GY + GH + 8, TEAL, buf);
  snprintf(buf, sizeof(buf), "CORE1 %.0f%%", s.load[1]); text(GX + 130, GY + GH + 8, PINK, buf);
  text(GX + 232, GY + GH + 8, DIM, "40 s");
}

void drawHeap(const Snapshot &s) {
  char buf[48];
  header("HEAP", 5);
  canvas->setTextSize(2);
  snprintf(buf, sizeof(buf), "%uK free", s.heapFree / 1024);
  text(4, 18, ICE, buf);
  canvas->setTextSize(1);

  float total = s.heapTotal ? (float)s.heapTotal : 1.0f;
  float usedPct = 100.0f * (total - s.heapFree) / total;
  bar(4, 44, 312, 12, usedPct, loadColor(usedPct));
  int mx = 4 + (int)(311 * (total - s.heapMin) / total);
  canvas->drawFastVLine(mx, 42, 16, RED);
  snprintf(buf, sizeof(buf), "used %.0f%%   lowest-ever free %uK   total %uK",
           usedPct, s.heapMin / 1024, s.heapTotal / 1024);
  text(4, 60, DIM, buf);

  const int GX = 4, GY = 76, GW = 312, GH = 76;
  canvas->drawRect(GX, GY, GW, GH, DIM);
  float lo = heapHist[0], hi = heapHist[0];
  for (int i = 1; i < HN; i++) { if (heapHist[i] < lo) lo = heapHist[i]; if (heapHist[i] > hi) hi = heapHist[i]; }
  if (hi - lo < 4.0f) { float mid = (hi + lo) / 2; lo = mid - 2; hi = mid + 2; }
  for (int i = 1; i < HN; i++) {
    int x0 = GX + 1 + (i - 1) * (GW - 3) / (HN - 1);
    int x1 = GX + 1 + i * (GW - 3) / (HN - 1);
    int y0 = GY + GH - 2 - (int)((GH - 3) * (heapHist[i - 1] - lo) / (hi - lo));
    int y1 = GY + GH - 2 - (int)((GH - 3) * (heapHist[i] - lo) / (hi - lo));
    canvas->drawLine(x0, y0, x1, y1, TEAL);
  }
  snprintf(buf, sizeof(buf), "free heap, last 40 s  (%.0fK - %.0fK)", lo, hi);
  text(GX + 4, GY + GH + 4, DIM, buf);
}

// =====================================================================
//  UI TASK
// =====================================================================
void uiTask(void *) {
  int screen = 0;                 // start on the hairclip heart
  uint32_t lastSeq = 0;
  lastActivityMs = millis();
  for (;;) {
    // ---- button events ----
    BtnEvent e;
    while (xQueueReceive(btnQueue, &e, 0) == pdTRUE) {
      lastActivityMs = millis();
      if (powerState == P_OFF) {              // first press only wakes the screen
        powerState = P_ACTIVE;
        applyBacklight();
        continue;
      }
      powerState = P_ACTIVE;
      if (e == BTN_NEXT)        screen = (screen + 1) % NSCR;
      else if (e == BTN_PREV)   screen = (screen + NSCR - 1) % NSCR;
      else                      brightIdx = (brightIdx + 1) % 4;
      toastUntil = millis() + 2000;
      applyBacklight();
    }

    // ---- dim / off timeouts ----
    uint32_t idle = millis() - lastActivityMs;
    PowerState ns = (idle >= OFF_AFTER_MS) ? P_OFF : (idle >= DIM_AFTER_MS ? P_DIM : P_ACTIVE);
    if (ns != powerState) { powerState = ns; applyBacklight(); }

    // ---- the monitor only works while a monitor screen is visible ----
    monitorActive = (screen >= 3) && (powerState != P_OFF);

    if (powerState == P_OFF) {                // screen off: draw nothing, let the CPU sleep
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    // ---- latest monitor data ----
    Snapshot s;
    xSemaphoreTake(snapMutex, portMAX_DELAY);
    s = snap;
    xSemaphoreGive(snapMutex);
    if (s.seq != lastSeq) { lastSeq = s.seq; pushHistory(s); }

    // ---- draw ----
    float now = millis() / 1000.0f;
    switch (screen) {
      case 0: drawHeart(now);   break;
      case 1: drawMandala(now); break;
      case 2: drawOrnate(now);  break;
      default:
        canvas->fillScreen(BG);
        if (screen == 3)      drawTable(s);
        else if (screen == 4) drawCpuGraph(s);
        else                  drawHeap(s);
        break;
    }

    // ---- short info banner after a button press (pattern screens only) ----
    if (screen < 3 && millis() < toastUntil) {
      char b[56];
      snprintf(b, sizeof(b), "BAT %.1fV %d%%   BRIGHT %d/4",
               (float)battV, (int)battPct, (int)brightIdx + 1);
      canvas->fillRect(0, H - 13, W, 13, rgb(10, 12, 30));
      text(4, H - 11, ICE, b);
    }

    canvas->flush();
    vTaskDelay(pdMS_TO_TICKS(screen < 3 ? 50 : 100));   // patterns 20 fps, monitor 10 fps
  }
}

// =====================================================================
//  SETUP / LOOP
// =====================================================================
void setup() {
  Serial.begin(115200);
  setCpuFrequencyMhz(80);                        // power saving: 80 MHz is enough here
  pinMode(PIN_POWER_ON, OUTPUT);
  digitalWrite(PIN_POWER_ON, HIGH);

  buildHeart();

  canvas = new Arduino_Canvas(W, H, gfx);
  if (!canvas->begin(10000000)) {
    Serial.println("canvas/display begin failed");
    while (true) delay(1000);
  }
  canvas->setTextSize(1);
  canvas->fillScreen(BG);
  canvas->flush();

  ledcSetup(0, 5000, 8);                         // PWM backlight: channel 0, 5 kHz, 8 bit
  ledcAttachPin(PIN_LCD_BL, 0);
  applyBacklight();

  snapMutex = xSemaphoreCreateMutex();
  btnQueue  = xQueueCreate(8, sizeof(BtnEvent));
  snap.n = 0; snap.seq = 0; snap.battV = 0; snap.battPct = 0;

  esp_register_freertos_idle_hook_for_cpu(idleHook0, 0);
  esp_register_freertos_idle_hook_for_cpu(idleHook1, 1);
  monitorActive = true;                          // hooks spin while we calibrate
  uint32_t a0 = idleCnt[0], a1 = idleCnt[1];
  delay(1000);                                   // calibrate with nothing running
  idleBase[0] = idleCnt[0] - a0; if (!idleBase[0]) idleBase[0] = 1;
  idleBase[1] = idleCnt[1] - a1; if (!idleBase[1]) idleBase[1] = 1;

  registerExisting("loopTask", xTaskGetCurrentTaskHandle(), 8192);
  cfgHeavy.idx = registryCount;  addTask(workTask, "work_heavy", 2048, &cfgHeavy, 2, true);
  addTask(monitorTask, "monitor", 4096, NULL, 1, false);
  addTask(uiTask,      "ui",      8192, NULL, 2, false);
  addTask(buttonTask,  "button",  2048, NULL, 2, false);
  addTask(leakTask,    "leaker",  2048, NULL, 1, false);
  hogIdx = registryCount;  addTaskPinned(hogTask, "hog", 2048, NULL, 2, true, 0);
  addTask(stackTask,   "stacker", 2560, NULL, 1, false);
  addTask(batteryTask, "battery", 3072, NULL, 1, false);   // 9th task (table shows first 8)
  Serial.println("hairclip v3 started. keys: l=leak h=hog s=stack r=reset b=battery");
}

void loop() {
  while (Serial.available()) {
    char c = Serial.read();
    if      (c == 'l') { leakOn = true;  Serial.println("heap leak ON"); }
    else if (c == 'h') { hogOn  = true;  Serial.println("CPU hog ON (core 0)"); }
    else if (c == 's') { stackGo = true; Serial.println("stack burst"); }
    else if (c == 'r') { leakOn = false; hogOn = false; clearLeaks = true;
                         Serial.println("faults cleared"); }
    else if (c == 'b') { Serial.printf("battery: %.2f V  (%d%%)\n", (float)battV, (int)battPct); }
  }
  vTaskDelay(pdMS_TO_TICKS(50));
}