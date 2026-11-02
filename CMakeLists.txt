cmake_minimum_required(VERSION 3.16.0)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(CendoriRC)

# GCC 15 on Windows ICEs in rgb_panel_draw_bitmap (RTL/IRA, -O2).
# -Os on this one IDF file avoids the crash; we still need esp_lcd.
idf_component_get_property(esp_lcd_dir esp_lcd COMPONENT_DIR)
if(esp_lcd_dir AND EXISTS "${esp_lcd_dir}/rgb/esp_lcd_panel_rgb.c")
  set_source_files_properties(
    "${esp_lcd_dir}/rgb/esp_lcd_panel_rgb.c"
    PROPERTIES COMPILE_OPTIONS "-Os")
endif()
