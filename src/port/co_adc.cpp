#include "co_adc.h"

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "soc/adc_channel.h"

namespace co_adc {

namespace {
adc_oneshot_unit_handle_t s_unit = nullptr;
adc_cali_handle_t s_cali = nullptr;
adc_channel_t s_channel = ADC_CHANNEL_0;
bool s_ready = false;
} // namespace

void init(int gpioPin) {
  if (gpioPin != 8) return;
  s_channel = (adc_channel_t)ADC1_GPIO8_CHANNEL;

  adc_oneshot_unit_init_cfg_t unit_cfg = {};
  unit_cfg.unit_id = ADC_UNIT_1;
  if (adc_oneshot_new_unit(&unit_cfg, &s_unit) != ESP_OK) return;

  adc_oneshot_chan_cfg_t chan_cfg = {};
  chan_cfg.atten = ADC_ATTEN_DB_12; // full ~0-3.3V range
  chan_cfg.bitwidth = ADC_BITWIDTH_DEFAULT;
  if (adc_oneshot_config_channel(s_unit, s_channel, &chan_cfg) != ESP_OK) return;

  // ESP32-S3 only supports the curve-fitting calibration scheme (the
  // line-fitting scheme is ESP32-original-only).
  adc_cali_curve_fitting_config_t cali_cfg = {};
  cali_cfg.unit_id = ADC_UNIT_1;
  cali_cfg.chan = s_channel;
  cali_cfg.atten = ADC_ATTEN_DB_12;
  cali_cfg.bitwidth = ADC_BITWIDTH_DEFAULT;
  if (adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_cali) != ESP_OK) {
    s_cali = nullptr;
  }

  s_ready = true;
}

int readMilliVolts() {
  if (!s_ready) return -1;
  int raw = 0;
  if (adc_oneshot_read(s_unit, s_channel, &raw) != ESP_OK) return -1;
  if (!s_cali) return raw;
  int mv = 0;
  if (adc_cali_raw_to_voltage(s_cali, raw, &mv) != ESP_OK) return -1;
  return mv;
}

} // namespace co_adc
