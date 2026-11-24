// SPDX-License-Identifier: GPL-3.0-only
// Majistic Recomp - Separate Text Language and Audio/Voice Language Hooks
//
// Hook points (all below 0x821C0000 -> identical addresses on both PAL 0.0.0.3 and USA 0.0.0.2):
//   - sub_8216F170: Boot-time language initialization -> writes *(state + 0x15C48)
//   - sub_821AB0A0: Centralized voice/audio language updater (SoundManager*)

#include "generated/default/majin_pch.h"
#include "edition_offsets.h"

#include <rex/cvar.h>
#include <rex/logging.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

REXCVAR_DEFINE_STRING(majin_text_language, "auto", "Gameplay",
    "Text/UI language override (auto, en, po, sp, fr, ge, it, du, ru, kr, jp)");

REXCVAR_DEFINE_STRING(majin_audio_language, "auto", "Gameplay",
    "Audio/voice language override (auto, en, po, sp, fr, ge, it, du, ru, jp)");

namespace {

constexpr uint32_t kScenarioRootPtr = majin::offsets::kScenarioRootPtr;
constexpr uint32_t kStatePtrOffset = 0x15128;        // 65536 | 20776
constexpr uint32_t kLanguageIdOffset = 0x15C48;      // 65536 | 23624
constexpr uint32_t kSoundForceEnOffset = 0x1722C;    // 65536 | 29228

struct LangEntry {
  uint32_t id;
  const char* code;
  const char* name;
  const char* pak_name;      // finalizedXBox360/KB/package/<pak_name>.pak
  const char* audio_suffix;  // finalizedXBox360/KB/sound/xbox360/rtd_<audio_suffix> (nullptr if none)
  bool supported_pal;
  bool supported_usa;
};

constexpr std::array<LangEntry, 10> kLanguages = {{
    {0, "jp", "Japanese",   "jp", "jp", false, false},
    {1, "en", "English",    "us", "en", true,  true },
    {2, "du", "Dutch",      "du", "du", true,  false},
    {3, "fr", "French",     "fr", "fr", true,  true },
    {4, "it", "Italian",    "it", "it", true,  false},
    {5, "ge", "German",     "ge", "ge", true,  false},
    {6, "sp", "Spanish",    "sp", "sp", true,  true },
    {7, "po", "Portuguese", "po", "po", true,  false},
    {8, "ru", "Russian",    "ru", "ru", true,  false},
    {9, "kr", "Korean",     "kr", nullptr, false, true},
}};

bool Span(uint32_t addr, uint32_t size) {
  return addr >= 0x10000 && addr <= UINT32_MAX - size;
}

std::string ToLowerTrimmed(std::string_view input) {
  size_t start = 0;
  while (start < input.size() && std::isspace(static_cast<unsigned char>(input[start]))) {
    ++start;
  }
  size_t end = input.size();
  while (end > start && std::isspace(static_cast<unsigned char>(input[end - 1]))) {
    --end;
  }
  std::string out;
  out.reserve(end - start);
  for (size_t i = start; i < end; ++i) {
    out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(input[i]))));
  }
  return out;
}

// Returns -1 for "auto"/"same"/empty, or 0..9 for a recognized language, or -2 for unknown.
int32_t ParseLanguageId(std::string_view raw) {
  const std::string v = ToLowerTrimmed(raw);
  if (v.empty() || v == "auto" || v == "same" || v == "default") {
    return -1;
  }
  if (v == "jp" || v == "ja" || v == "japanese") return 0;
  if (v == "en" || v == "us" || v == "english") return 1;
  if (v == "du" || v == "nl" || v == "dutch") return 2;
  if (v == "fr" || v == "french") return 3;
  if (v == "it" || v == "italian") return 4;
  if (v == "ge" || v == "de" || v == "german") return 5;
  if (v == "sp" || v == "es" || v == "spanish") return 6;
  if (v == "po" || v == "pt" || v == "pt-br" || v == "pt-pt" || v == "portuguese") return 7;
  if (v == "ru" || v == "russian") return 8;
  if (v == "kr" || v == "ko" || v == "korean") return 9;
  return -2;
}

const LangEntry* FindLangEntry(uint32_t id) {
  if (id < kLanguages.size()) {
    return &kLanguages[id];
  }
  return nullptr;
}

bool IsTextSupportedOnActiveEdition(const LangEntry& entry) {
#if defined(MAJIN_EDITION_PAL)
  return entry.supported_pal;
#else
  return entry.supported_usa;
#endif
}

std::filesystem::path GetGameDataRootPath() {
  const std::string root = rex::cvar::GetFlagByName("game_data_root");
  if (!root.empty()) {
    return std::filesystem::path(root);
  }
  return {};
}

bool TextAssetsAvailable(const LangEntry& entry) {
  const auto root = GetGameDataRootPath();
  if (root.empty()) {
    return true;  // Cannot probe disk; trust edition support table.
  }
  std::error_code ec;
  const auto pak = root / "finalizedXBox360" / "KB" / "package" / (std::string(entry.pak_name) + ".pak");
  return std::filesystem::is_regular_file(pak, ec);
}

bool AudioAssetsAvailable(const LangEntry& entry) {
  if (!entry.audio_suffix) {
    return false;
  }
  const auto root = GetGameDataRootPath();
  if (root.empty()) {
    return true;  // Cannot probe disk; assume present.
  }
  std::error_code ec;
  const auto rtd_dir = root / "finalizedXBox360" / "KB" / "sound" / "xbox360" /
                       (std::string("rtd_") + entry.audio_suffix);
  if (!std::filesystem::is_directory(rtd_dir, ec)) {
    return false;
  }
  for (const auto& item : std::filesystem::directory_iterator(rtd_dir, ec)) {
    if (item.is_regular_file(ec)) {
      return true;
    }
  }
  return false;
}

uint32_t ResolveStatePtr(uint8_t* base) {
  const uint32_t root = REX_LOAD_U32(kScenarioRootPtr);
  if (!Span(root, kStatePtrOffset + 4)) return 0;
  const uint32_t state = REX_LOAD_U32(root + kStatePtrOffset);
  if (!Span(state, kLanguageIdOffset + 4)) return 0;
  return state;
}

}  // namespace

// Boot-time language initialization (identical address 0x8216F170 on PAL and USA).
// Applies independent Text/UI language override and unlocks edition-supported languages
// that have no Xbox XC_LANGUAGE mapping (e.g. Dutch on PAL).
REX_EXTERN(__imp__sub_8216F170);
REX_EXTERN(sub_8216F170) {
  __imp__sub_8216F170(ctx, base);

  const std::string raw = REXCVAR_GET(majin_text_language);
  const int32_t parsed = ParseLanguageId(raw);
  if (parsed == -1) {
    return;  // "auto" -> keep game's default XGetLanguage resolution
  }

  const uint32_t state = ResolveStatePtr(base);
  if (!state) {
    return;
  }

  uint32_t target_id = 1;  // English fallback
  if (parsed >= 0) {
    const LangEntry* entry = FindLangEntry(static_cast<uint32_t>(parsed));
    if (entry && IsTextSupportedOnActiveEdition(*entry) && TextAssetsAvailable(*entry)) {
      target_id = entry->id;
    } else if (entry) {
      REXLOG_WARN("[Majin Language] Requested text language '{}' is not available on {}; falling back to English.",
                  entry->name, MAJIN_EDITION_NAME);
    }
  } else {
    REXLOG_WARN("[Majin Language] Unknown text language '{}'; falling back to English.", raw);
  }

  REX_STORE_U32(state + kLanguageIdOffset, target_id);
  const LangEntry* applied = FindLangEntry(target_id);
  REXLOG_INFO("[Majin Language] Text/UI language set to {} (id={}, {})",
              applied ? applied->name : "Unknown", target_id, MAJIN_EDITION_NAME);
}

// Centralized audio/voice language updater (identical address 0x821AB0A0 on PAL and USA).
// Temporarily swaps *(state + 0x15C48) during SoundManager language setup so voice streams
// (rtd_<lang>, dig*_<lang>) and voice banks load the chosen audio language independently
// from the active Text/UI language.
REX_EXTERN(__imp__sub_821AB0A0);
REX_EXTERN(sub_821AB0A0) {
  const std::string raw = REXCVAR_GET(majin_audio_language);
  const int32_t parsed = ParseLanguageId(raw);
  if (parsed == -1) {
    __imp__sub_821AB0A0(ctx, base);
    return;
  }

  const uint32_t sound_mgr = ctx.r3.u32;
  const uint32_t state = ResolveStatePtr(base);
  if (!state || !Span(sound_mgr, kSoundForceEnOffset + 4)) {
    __imp__sub_821AB0A0(ctx, base);
    return;
  }

  uint32_t target_audio_id = 1;  // English fallback
  if (parsed >= 0) {
    const LangEntry* entry = FindLangEntry(static_cast<uint32_t>(parsed));
    if (entry && AudioAssetsAvailable(*entry)) {
      target_audio_id = entry->id;
    } else if (entry) {
      REXLOG_WARN("[Majin Language] Requested audio language '{}' has no voice assets on this disc ({}); falling back to English.",
                  entry->name, MAJIN_EDITION_NAME);
    }
  } else {
    REXLOG_WARN("[Majin Language] Unknown audio language '{}'; falling back to English.", raw);
  }

  const uint32_t orig_text_lang = REX_LOAD_U32(state + kLanguageIdOffset);
  const uint32_t orig_force_en = REX_LOAD_U32(sound_mgr + kSoundForceEnOffset);

  REX_STORE_U32(state + kLanguageIdOffset, target_audio_id);
  REX_STORE_U32(sound_mgr + kSoundForceEnOffset, 0u);

  __imp__sub_821AB0A0(ctx, base);

  REX_STORE_U32(state + kLanguageIdOffset, orig_text_lang);
  REX_STORE_U32(sound_mgr + kSoundForceEnOffset, orig_force_en);

  static uint32_t s_last_logged_audio = 0xFFFFFFFFu;
  if (s_last_logged_audio != target_audio_id) {
    const LangEntry* applied = FindLangEntry(target_audio_id);
    const LangEntry* text_entry = FindLangEntry(orig_text_lang);
    REXLOG_INFO("[Majin Language] Audio/Voice language set to {} (id={}) while Text/UI remains {} (id={}) ({})",
                applied ? applied->name : "Unknown", target_audio_id,
                text_entry ? text_entry->name : "Unknown", orig_text_lang,
                MAJIN_EDITION_NAME);
    s_last_logged_audio = target_audio_id;
  }
}
