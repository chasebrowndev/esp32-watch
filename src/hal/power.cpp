#include "power.h"
#include "display.h"
#include "touch.h"
#include "config.h"
#include "board_pins.h"
#include <Arduino.h>
#include <Preferences.h>
#include <esp_pm.h>
#include <esp_sleep.h>
#include <driver/gpio.h>
#include <tusb.h>
#include <WiFi.h>

namespace {
  enum class St { Bright, Dim, Asleep };
  St       s_state   = St::Bright;
  uint32_t s_lastSeen = 0;   // last touch activity we acted on
  uint8_t  s_bright  = BL_BRIGHT;

  // Battery state. s_batMv is the EMA-smoothed millivolts.
  uint16_t s_batMv      = 0;
  uint32_t s_batLastMs  = 0;

  uint8_t mvToPct(uint16_t mv) {
    if (mv >= BAT_MV_FULL)  return 100;
    if (mv <= BAT_MV_EMPTY) return 0;
    // Piecewise: 3.3-3.7 V is the bottom 50% (sags fast under load), 3.7-4.15
    // V is the top 50%. Matches a typical LiPo discharge curve well enough
    // for a status icon.
    if (mv <= 3700) {
      return (uint8_t)((mv - BAT_MV_EMPTY) * 50 / (3700 - BAT_MV_EMPTY));
    }
    return (uint8_t)(50 + (mv - 3700) * 50 / (BAT_MV_FULL - 3700));
  }

  void sampleBattery() {
    // analogReadMilliVolts returns mV at the pin; multiply by divider ratio.
    uint32_t mv = analogReadMilliVolts(PIN_BAT_ADC) * BAT_DIVIDER;
    if (s_batMv == 0) s_batMv = mv;                 // seed on first sample
    else              s_batMv = (uint16_t)((s_batMv * 4 + mv) / 5);   // EMA a=0.2
  }
}

namespace power {

void init() {
  pinMode(PIN_TP_INT, INPUT_PULLUP);   // touch INT — wake source from sleep
  Preferences p;
  if (p.begin("power", true)) { s_bright = p.getUChar("bright", BL_BRIGHT); p.end(); }
  disp::setBacklight(s_bright);
  s_state = St::Bright;
  s_lastSeen = millis();

  // Automatic light-sleep when FreeRTOS goes idle. The WiFi/BLE drivers
  // coordinate modem hold internally; on chips where PM isn't compiled in,
  // esp_pm_configure returns ESP_ERR_NOT_SUPPORTED — we just log and move on.
  //
  // Disable light sleep when USB is attached. Use tud_mounted() (not
  // tud_ready()) because tud_ready() goes false during USB selective
  // suspend — which the host issues routinely on idle CDC devices —
  // even though the cable is still physically plugged in.
  bool usbAttached = tud_mounted();
  // min_freq_mhz = 10 lets DFS drop the CPU to the XTAL during idle windows,
  // which is a much bigger win than the 80 MHz floor — at 80 MHz the chip
  // still pulls tens of mA when "idle." The PM driver scales back up
  // automatically on interrupt / RTOS wake.
  esp_pm_config_esp32s3_t pm = {
    .max_freq_mhz = 240,
    .min_freq_mhz = 10,
    .light_sleep_enable = !usbAttached,
  };
  esp_err_t e = esp_pm_configure(&pm);
  Serial.printf("[pm] esp_pm_configure -> %d (light_sleep=%d)\n",
                (int)e, (int)pm.light_sleep_enable);

  // Battery: take an immediate sample so the UI doesn't show 0% on the first
  // tick. analogReadMilliVolts handles ADC1 calibration internally on S3.
  analogSetPinAttenuation(PIN_BAT_ADC, ADC_11db);
  sampleBattery();
  s_batLastMs = millis();
  Serial.printf("[bat] first sample: %u mV (%u%%)\n", s_batMv, mvToPct(s_batMv));
}

uint16_t batteryMv()  { return s_batMv; }
uint8_t  batteryPct() { return mvToPct(s_batMv); }
bool     batteryLow() { return s_batMv && s_batMv < BAT_MV_LOW; }
bool     charging()   { return tud_mounted(); }

void noteActivity() {
  s_lastSeen = millis();
  if (s_state != St::Bright) wake();
}

void wake() {
  bool wasAsleep = (s_state == St::Asleep);
  if (wasAsleep) disp::wakePanel();
  disp::setBacklight(s_bright);
  s_state = St::Bright;
  s_lastSeen = millis();
  // Some panels report a stale or ghost coordinate on the first poll after
  // panel-on. Suppress the LVGL indev for a short window so the wake tap
  // doesn't also count as a UI click.
  if (wasAsleep) touch::ignoreUntil(millis() + 100);
}

bool asleep() { return s_state == St::Asleep; }

uint8_t brightness() { return s_bright; }

void setBrightness(uint8_t v) {
  if (v < 5) v = 5;   // never let user blank the screen permanently
  s_bright = v;
  if (s_state == St::Bright) disp::setBacklight(s_bright);
  Preferences p;
  if (p.begin("power", false)) { p.putUChar("bright", s_bright); p.end(); }
}

void powerOff() {
  // Drop the backlight and put the panel to sleep so nothing bright lingers
  // during the millisecond between here and esp_deep_sleep_start().
  disp::setBacklight(0);
  disp::sleepPanel();

  // Deep sleep cuts power to the WiFi/BT modems automatically, but a clean
  // disconnect avoids leaving the AP/peer wondering where we went.
  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_OFF);

  // No wake source configured — the chip resumes only on EN/reset.
  esp_deep_sleep_start();
}

void tick() {
  // Battery: re-sample every 30s. The ADC + EMA settles fine at this rate
  // for a status icon, and the saved ticks compound to meaningful idle time.
  uint32_t now = millis();
  if (now - s_batLastMs >= 30000) { s_batLastMs = now; sampleBattery(); }

  // Touch activity (tracked by the indev) resets the idle timer from any state.
  static uint32_t prevActivity = 0;
  uint32_t act = touch::lastActivityMs();
  if (act != prevActivity) { prevActivity = act; noteActivity(); }

  // When USB-C is plugged in, the chip is host-powered and entering deep
  // sleep tears down the USB endpoint — causing a reboot loop. Stay Bright
  // for the duration of the USB session so dim/sleep never trigger.
  //
  // Use tud_mounted() instead of tud_ready(): the host periodically issues
  // USB selective suspend on idle CDC devices, which flips tud_ready() to
  // false (and back true on resume) every few seconds. Using tud_ready()
  // here caused the screen to cycle sleep/wake while plugged in.
  if (tud_mounted()) { noteActivity(); return; }

  uint32_t idle = millis() - s_lastSeen;

  switch (s_state) {
    case St::Bright:
      if (idle >= IDLE_DIM_MS) { disp::setBacklight(BL_DIM); s_state = St::Dim; }
      break;
    case St::Dim:
      if (idle >= IDLE_SLEEP_MS) {
        disp::sleepPanel();
        s_state = St::Asleep;
        // Panel-sleep / backlight-off transitions cause a short EMI burst
        // that the FT6336G frequently reports as a phantom touch. Mask the
        // indev for ~400 ms so that ghost doesn't immediately wake us and
        // trap us in a sleep<->wake cycle with no user input.
        touch::ignoreUntil(millis() + 400);
      }
      break;
    case St::Asleep:
      // Panel off; LVGL/WiFi/BLE still run (automatic light sleep above
      // gives us most of the idle savings). Wake detection is handled by
      // the LVGL touch indev — its read_cb updates touch::lastActivityMs(),
      // which the activity check at the top of this tick converts into a
      // wake(). Do NOT poll disp::readTouch() here: it races the indev's
      // I2C transactions on the FT6336G and the contention itself produces
      // spurious touches (see touch.cpp).
      //
      // Deep sleep is intentionally disabled: this board's FT6336G does
      // not drive PIN_TP_INT low reliably (see HANDOFF.md), so the only
      // wake source we had would never fire and the device would brick
      // until manual reset. We rely on panel-off + automatic light sleep
      // for power saving; the chip stays awake enough to wake on a tap.
      break;
  }
}

} // namespace power
