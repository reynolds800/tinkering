// The display the app draws on: the real ILI9488 panel and FT6236 touch
// controller on the device, or an SDL2 window in the simulator. Both expose
// the same LovyanGFX drawing and touch API.
#pragma once

#include <LovyanGFX.hpp>

static const int SCREEN_W = 320;
static const int SCREEN_H = 480;

#if defined(SIMULATOR)

#include <lgfx/v1/platforms/sdl/Panel_sdl.hpp>

static const int SCREEN_ROTATION = 0;

class Display : public lgfx::LGFX_Device {
  lgfx::Panel_sdl _panel;

  bool init_impl(bool, bool use_clear) override {
    return lgfx::LGFX_Device::init_impl(false, use_clear);
  }

public:
  Display() {
    auto cfg = _panel.config();
    cfg.memory_width = cfg.panel_width = SCREEN_W;
    cfg.memory_height = cfg.panel_height = SCREEN_H;
    _panel.config(cfg);
    _panel.setScaling(1, 1);
    setPanel(&_panel);
  }
};

#else

// Portrait, as in the ThingPulse Color Kit Grande sample project, which is
// also where these pin assignments come from:
// https://github.com/ThingPulse/esp32-weather-station-touch
static const int SCREEN_ROTATION = 2;

class Display : public lgfx::LGFX_Device {
  lgfx::Panel_ILI9488 _panel;
  lgfx::Bus_SPI _bus;
  lgfx::Light_PWM _light;
  lgfx::Touch_FT5x06 _touch;  // the FT6236 speaks the same protocol

public:
  Display() {
    {
      auto cfg = _bus.config();
      cfg.spi_host = VSPI_HOST;
      cfg.freq_write = 27000000;
      cfg.freq_read = 16000000;
      cfg.pin_sclk = 5;
      cfg.pin_mosi = 18;
      cfg.pin_miso = 19;
      cfg.pin_dc = 2;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {
      auto cfg = _panel.config();
      cfg.pin_cs = 15;
      cfg.pin_rst = 4;
      cfg.panel_width = SCREEN_W;
      cfg.panel_height = SCREEN_H;
      cfg.bus_shared = false;
      _panel.config(cfg);
    }
    {
      auto cfg = _light.config();
      cfg.pin_bl = 32;
      cfg.freq = 5000;
      cfg.pwm_channel = 7;
      _light.config(cfg);
      _panel.setLight(&_light);
    }
    {
      auto cfg = _touch.config();
      cfg.i2c_port = 0;
      cfg.i2c_addr = 0x38;
      cfg.pin_sda = 23;
      cfg.pin_scl = 22;
      cfg.pin_int = -1;
      cfg.freq = 400000;
      cfg.x_min = 0;
      cfg.x_max = SCREEN_W - 1;
      cfg.y_min = 0;
      cfg.y_max = SCREEN_H - 1;
      cfg.bus_shared = false;
      _touch.config(cfg);
      _panel.setTouch(&_touch);
    }
    setPanel(&_panel);
  }
};

#endif
