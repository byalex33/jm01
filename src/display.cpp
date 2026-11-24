#include "display.h"
#include <Arduino.h>
#include <LovyanGFX.hpp>
#include <esp_heap_caps.h>
#include "config.h"

class Panel : public lgfx::LGFX_Device {
  lgfx::Panel_GC9A01 panel;
  lgfx::Bus_SPI bus;
  lgfx::Light_PWM light;
 public:
  Panel() {
    auto b = bus.config();
    b.spi_host = SPI2_HOST;
    b.spi_mode = 0;
    b.freq_write = LCD_SPI_HZ;
    b.freq_read = 16000000;
    b.spi_3wire = true;
    b.use_lock = true;
    b.dma_channel = SPI_DMA_CH_AUTO;
    b.pin_sclk = PIN_LCD_SCK;
    b.pin_mosi = PIN_LCD_MOSI;
    b.pin_miso = -1;
    b.pin_dc = PIN_LCD_DC;
    bus.config(b);
    panel.setBus(&bus);

    auto p = panel.config();
    p.pin_cs = PIN_LCD_CS;
    p.pin_rst = PIN_LCD_RST;
    p.pin_busy = -1;
    p.panel_width = 240;
    p.panel_height = 240;
    p.offset_rotation = 0;
    p.readable = false;
    p.invert = true;          // GC9A01 modules need inversion for correct colours
    p.rgb_order = false;
    p.dlen_16bit = false;
    p.bus_shared = false;
    panel.config(p);

    auto l = light.config();
    l.pin_bl = PIN_LCD_BL;
    l.invert = false;
    l.freq = 44100;
    l.pwm_channel = 7;
    light.config(l);
    panel.setLight(&light);

    setPanel(&panel);
  }
};

static Panel lcd;
static lv_display_t* disp;
static lv_obj_t* canvas;

static void flush(lv_display_t* d, const lv_area_t* a, uint8_t* px) {
  const int w = a->x2 - a->x1 + 1, h = a->y2 - a->y1 + 1;
  lcd.pushImageDMA(a->x1, a->y1, w, h, (lgfx::rgb565_t*)px);
  lcd.waitDMA();
  lv_display_flush_ready(d);
}

static uint32_t tick() { return millis(); }

void displayBegin() {
  lcd.init();
  lcd.setRotation(LCD_ROTATION);
  lcd.setBrightness(0);
  lcd.fillScreen(TFT_BLACK);
  lcd.startWrite();           // keep the SPI bus claimed for DMA pushes

  lv_init();
  lv_tick_set_cb(tick);
  disp = lv_display_create(240, 240);
  lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
  lv_display_set_flush_cb(disp, flush);
  // two 40-line DMA buffers in internal RAM
  const size_t bytes = 240 * 40 * 2;
  void* b1 = heap_caps_malloc(bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  void* b2 = heap_caps_malloc(bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  lv_display_set_buffers(disp, b1, b2, bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);

  lv_obj_t* scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

  // the whole picture is one canvas; its pixels live in PSRAM
  void* cbuf = heap_caps_malloc(240 * 240 * 2, MALLOC_CAP_SPIRAM);
  if (!cbuf) cbuf = heap_caps_malloc(240 * 240 * 2, MALLOC_CAP_8BIT);
  canvas = lv_canvas_create(scr);
  lv_canvas_set_buffer(canvas, cbuf, 240, 240, LV_COLOR_FORMAT_RGB565);
  lv_canvas_fill_bg(canvas, lv_color_black(), LV_OPA_COVER);
  lv_obj_center(canvas);
  lv_refr_now(disp);
}

void displayBrightness(uint8_t v) { lcd.setBrightness(v); }
lv_obj_t* displayCanvas() { return canvas; }
void displayLoop() { lv_timer_handler(); }
