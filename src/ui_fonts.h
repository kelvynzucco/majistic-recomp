// SPDX-License-Identifier: GPL-3.0-only
// Majistic Recomp - Embedded Launcher Typography (Rubik) & Solar Icons for Dear ImGui

#pragma once

#include <imgui.h>

// Solar Icons (bold variant, matching launcher/index.html) mapped to BMP PUA U+E000..U+E00B
#define ICON_SOLAR_MIN 0xE000
#define ICON_SOLAR_MAX 0xE00B

#define ICON_SOLAR_MONITOR     "\xEE\x80\x80"  // U+E000 monitor-bold (Graphics)
#define ICON_SOLAR_SHADOW      "\xEE\x80\x81"  // U+E001 contrast-bold (Shadow)
#define ICON_SOLAR_ATMOSPHERE  "\xEE\x80\x82"  // U+E002 sun-fog-bold (Atmosphere)
#define ICON_SOLAR_GAMEPAD     "\xEE\x80\x83"  // U+E003 gamepad-bold (Controls)
#define ICON_SOLAR_SHIELD      "\xEE\x80\x84"  // U+E004 shield-check-bold (Fixes & Patches)
#define ICON_SOLAR_SETTINGS    "\xEE\x80\x85"  // U+E005 settings-bold (Game Preferences)
#define ICON_SOLAR_DISKETTE    "\xEE\x80\x86"  // U+E006 diskette-bold (Save Config)
#define ICON_SOLAR_PLAY        "\xEE\x80\x87"  // U+E007 play-bold (Resume)
#define ICON_SOLAR_LOGOUT      "\xEE\x80\x88"  // U+E008 logout-2-bold (Quit)
#define ICON_SOLAR_CODE        "\xEE\x80\x89"  // U+E009 code-bold (Developer Mode)
#define ICON_SOLAR_INFO        "\xEE\x80\x8A"  // U+E00A info-circle-bold (Session Info)
#define ICON_SOLAR_SLIDERS     "\xEE\x80\x8B"  // U+E00B tuning-2-bold (Tuning / Bias)

namespace majin {

struct UiFonts {
  ImFont* body = nullptr;      // Rubik Regular 15px + Solar Icons 15px
  ImFont* caption = nullptr;   // Rubik Regular 13px (hints, badges)
  ImFont* semibold = nullptr;  // Rubik SemiBold 15px + Solar Icons 17px (sidebar, headers, buttons)
  ImFont* title = nullptr;     // Rubik SemiBold 19px + Solar Icons 19px (window & page titles)
};

// Registers Rubik + Solar Icons into the SDK ImFontAtlas during MajinApp::OnConfigureFonts().
void RegisterLauncherFonts(ImFontAtlas* atlas);

// Accessor for the registered launcher fonts (safe fallback to nullptr if not initialized).
const UiFonts& GetUiFonts();

}  // namespace majin
