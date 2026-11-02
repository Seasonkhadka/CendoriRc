#include "ui_lang.h"

#include <stdio.h>

#include "nvs.h"
#include "nvs_flash.h"

namespace ui_lang {

namespace {

Lang s_lang = Lang::Ko;

bool openNvs(nvs_open_mode_t mode, nvs_handle_t *h) {
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    nvs_flash_erase();
    err = nvs_flash_init();
  }
  return err == ESP_OK && nvs_open("ui", mode, h) == ESP_OK;
}

} // namespace

void load() {
  nvs_handle_t h;
  if (!openNvs(NVS_READONLY, &h)) return;
  uint8_t v = 0;
  if (nvs_get_u8(h, "lang", &v) == ESP_OK && v <= (uint8_t)Lang::En) s_lang = (Lang)v;
  nvs_close(h);
  printf("[UI] language %s\n", s_lang == Lang::En ? "English" : "Korean");
}

Lang get() { return s_lang; }

bool set(Lang lang) {
  if (lang == s_lang) return false;
  s_lang = lang;
  nvs_handle_t h;
  if (openNvs(NVS_READWRITE, &h)) {
    nvs_set_u8(h, "lang", (uint8_t)lang);
    nvs_commit(h);
    nvs_close(h);
  }
  printf("[UI] language -> %s\n", lang == Lang::En ? "English" : "Korean");
  return true;
}

} // namespace ui_lang
