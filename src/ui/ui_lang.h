/*
 * UI language (한국어 / English), saved in NVS.
 *
 * Korean shows Korean only; English shows English only.
 * Every on-screen string goes through tr().
 * Screens read the language when they are built, so main.cpp rebuilds
 * them after a change.
 */
#pragma once

#include <stdint.h>

namespace ui_lang {

enum class Lang : uint8_t { Ko = 0, En = 1 };

// Loads the saved language (Korean when nothing is saved). Call once
// before the screens are built.
void load();

Lang get();

// Sets and saves the language. Returns true if it changed.
bool set(Lang lang);

inline bool en() { return get() == Lang::En; }

// The string for the current language.
inline const char *tr(const char *ko, const char *en_text) { return en() ? en_text : ko; }

} // namespace ui_lang
