// Hairclip - Step 1 (GFX version): animation as a FreeRTOS task
// Board: LilyGO T-Display-S3 (ESP32-S3, 170x320 ST7789, 8-bit parallel)
// Library: "GFX Library for Arduino" (moononournation)

#include <Arduino_GFX_Library.h>
#include <math.h>

#define PIN_POWER_ON 15   // must be HIGH for the display on battery
#define PIN_LCD_BL   38   // backlight

// 8-bit parallel bus: DC, CS, WR, RD, D0..D7
Arduino_DataBus *bus = new Arduino_ESP32LCD8(
    7 /* DC */, 6 /* CS */, 8 /* WR */, 9 /* RD */,
    39 /* D0 */, 40, 41, 42, 45, 46, 47, 48 /* D7 */);

// rotation 1 = landscape (320x170); 35 = column offset of this panel
Arduino_GFX *gfx = new Arduino_ST7789(
    bus, 5 /* RST */, 1 /* rotation */, true /* IPS */,
    170, 320, 35, 0, 35, 0);

Arduino_Canvas *canvas;   // off-screen buffer -> flicker-free animation
int W, H;

const uint16_t PALE_BLUE = 0x9E7F;

void animTask(void *) {
  TickType_t last = xTaskGetTickCount();
  float t = 0;
  for (;;) {
    canvas->fillScreen(BLACK);

    for (int k = 0; k < 4; k++) {
      uint16_t col = (k % 2) ? PALE_BLUE : WHITE;
      float phase = t + k * 0.9f;
      int prevY = 0;
      for (int x = 0; x < W; x += 2) {
        float fx = x / (float)W;
        float y = H / 2
                + sinf(fx * 6.28f * 1.5f + phase) * (30 + 12 * k)
                * sinf(fx * 3.14f);
        if (x > 0) canvas->drawLine(x - 2, prevY, x, (int)y, col);
        prevY = (int)y;
      }
    }
    canvas->drawRoundRect(2, 2, W - 4, H - 4, 8, PALE_BLUE);

    canvas->flush();                       // push buffer to the screen
    t += 0.08f;
    vTaskDelayUntil(&last, pdMS_TO_TICKS(33));   // ~30 fps
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_POWER_ON, OUTPUT);
  digitalWrite(PIN_POWER_ON, HIGH);

  W = gfx->width();
  H = gfx->height();
  canvas = new Arduino_Canvas(W, H, gfx);
  if (!canvas->begin()) {          // this also starts the display, once
    Serial.println("canvas/display begin failed - check PSRAM = OPI PSRAM");
    while (true) delay(1000);
  }
  canvas->fillScreen(BLACK);
  canvas->flush();

  pinMode(PIN_LCD_BL, OUTPUT);
  digitalWrite(PIN_LCD_BL, HIGH);

  xTaskCreatePinnedToCore(animTask, "anim", 4096, NULL, 2, NULL, 1);
  Serial.printf("anim task started (%dx%d)\n", W, H);
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));   // idle; later: battery/button/monitor
}