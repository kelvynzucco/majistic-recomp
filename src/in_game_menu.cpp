// SPDX-License-Identifier: GPL-3.0-only
// Majistic Recomp - In-Game Configuration & Settings Overlay Menu
// Standardized with the Majistic Launcher design system (Rubik + Solar Icons)

#include "in_game_menu.h"

#include "ui_fonts.h"

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/input/mnk/mnk_input_driver.h>

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

REXCVAR_DEFINE_BOOL(majin_dev_mode, false, "Majin/UI",
                    "Enable Developer Mode options in the in-game settings overlay")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

namespace majin {

namespace {

// Launcher color palette (matching launcher/tailwind.config.js)
constexpr ImVec4 kColDarkBg       = ImVec4(0.059f, 0.067f, 0.090f, 1.00f);  // #0f1117
constexpr ImVec4 kColDarkCard     = ImVec4(0.094f, 0.106f, 0.141f, 1.00f);  // #181b24
constexpr ImVec4 kColDarkControl  = ImVec4(0.137f, 0.153f, 0.204f, 1.00f);  // #232734
constexpr ImVec4 kColDarkBorder   = ImVec4(0.196f, 0.220f, 0.290f, 0.85f);  // #32384a
constexpr ImVec4 kColTextPrimary  = ImVec4(0.945f, 0.961f, 0.976f, 1.00f);  // slate-100
constexpr ImVec4 kColTextMuted    = ImVec4(0.580f, 0.639f, 0.722f, 1.00f);  // slate-400
constexpr ImVec4 kColSkyAccent    = ImVec4(0.220f, 0.741f, 0.973f, 1.00f);  // #38bdf8
constexpr ImVec4 kColSkyBadgeBg   = ImVec4(0.031f, 0.184f, 0.286f, 0.78f);  // sky-950/70
constexpr ImVec4 kColSkyBadgeBdr  = ImVec4(0.027f, 0.349f, 0.522f, 0.80f);  // sky-800/80
constexpr ImVec4 kColMajinGreen   = ImVec4(0.427f, 0.655f, 0.071f, 1.00f);  // #6da712
constexpr ImVec4 kColMajinHover   = ImVec4(0.486f, 0.722f, 0.094f, 1.00f);  // #7cb818
constexpr ImVec4 kColMajinActive  = ImVec4(0.333f, 0.510f, 0.055f, 1.00f);  // #55820e
constexpr ImVec4 kColMajinLight   = ImVec4(0.710f, 0.871f, 0.388f, 1.00f);  // #b5de63
constexpr ImVec4 kColMajinTintBg  = ImVec4(0.427f, 0.655f, 0.071f, 0.20f);  // #6da712/20
constexpr ImVec4 kColMajinTintBdr = ImVec4(0.427f, 0.655f, 0.071f, 0.45f);
constexpr ImVec4 kColAmberBg      = ImVec4(0.271f, 0.102f, 0.012f, 0.80f);  // amber-950/80
constexpr ImVec4 kColAmberBdr     = ImVec4(0.573f, 0.251f, 0.055f, 0.75f);  // amber-800/75
constexpr ImVec4 kColAmberText    = ImVec4(0.988f, 0.827f, 0.302f, 1.00f);  // amber-300
constexpr ImVec4 kColRoseBg       = ImVec4(0.298f, 0.020f, 0.098f, 0.75f);  // rose-950/75
constexpr ImVec4 kColRoseBdr      = ImVec4(0.624f, 0.071f, 0.224f, 0.75f);  // rose-800/75
constexpr ImVec4 kColRoseText     = ImVec4(0.992f, 0.643f, 0.686f, 1.00f);  // rose-300
constexpr ImVec4 kColRoseBtn      = ImVec4(0.624f, 0.071f, 0.224f, 0.90f);  // rose-800
constexpr ImVec4 kColRoseBtnHov   = ImVec4(0.745f, 0.071f, 0.235f, 1.00f);  // rose-700

void PushFontSafe(ImFont* font) {
  if (font) {
    ImGui::PushFont(font);
  }
}

void PopFontSafe(ImFont* font) {
  if (font) {
    ImGui::PopFont();
  }
}

std::filesystem::path FindLauncherConfigPath() {
  const std::vector<std::filesystem::path> candidates = {
      "private/launcher-usa.json",
      "../private/launcher-usa.json",
      "../../private/launcher-usa.json",
  };
  for (const auto& path : candidates) {
    if (std::filesystem::exists(path)) {
      return std::filesystem::absolute(path);
    }
  }
  return {};
}

void ReplaceJsonField(std::string& json, const std::string& key, const std::string& new_val) {
  const std::string pattern = "\"" + key + "\":";
  size_t pos = json.find(pattern);
  if (pos == std::string::npos) {
    // Insert missing keys before the closing brace so older launcher configs upgrade cleanly.
    size_t close_brace = json.rfind('}');
    if (close_brace == std::string::npos) return;
    size_t last_non_ws = json.find_last_not_of(" \t\r\n", close_brace - 1);
    const bool needs_comma = (last_non_ws != std::string::npos && json[last_non_ws] != '{' && json[last_non_ws] != ',');
    const std::string entry = (needs_comma ? ",\n  " : "\n  ") + pattern + " " + new_val + "\n";
    json.insert(last_non_ws != std::string::npos ? last_non_ws + 1 : close_brace, entry);
    return;
  }
  size_t val_start = json.find_first_not_of(" \t", pos + pattern.size());
  if (val_start == std::string::npos) return;
  size_t val_end = json.find_first_of(",\r\n}", val_start);
  if (val_end == std::string::npos) val_end = json.size();
  json.replace(val_start, val_end - val_start, new_val);
}

void DrawPillBadge(const char* label, const ImVec4& bg, const ImVec4& border, const ImVec4& text_col) {
  const auto& fonts = GetUiFonts();
  PushFontSafe(fonts.caption);
  const ImVec2 text_sz = ImGui::CalcTextSize(label);
  const ImVec2 pad(8.0f, 2.5f);
  const ImVec2 box_sz(text_sz.x + pad.x * 2.0f, text_sz.y + pad.y * 2.0f);
  const ImVec2 pos = ImGui::GetCursorScreenPos();
  ImGui::Dummy(box_sz);

  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 p_max(pos.x + box_sz.x, pos.y + box_sz.y);
  dl->AddRectFilled(pos, p_max, ImGui::GetColorU32(bg), 6.0f);
  dl->AddRect(pos, p_max, ImGui::GetColorU32(border), 6.0f, 0, 1.0f);
  dl->AddText(ImVec2(pos.x + pad.x, pos.y + pad.y), ImGui::GetColorU32(text_col), label);
  PopFontSafe(fonts.caption);
}

void DrawExperimentalBadge() {
  ImGui::SameLine(0.0f, 8.0f);
  DrawPillBadge("Experimental", kColRoseBg, kColRoseBdr, kColRoseText);
}

void DrawVulkanOnlyBadge() {
  ImGui::SameLine(0.0f, 8.0f);
  DrawPillBadge("Vulkan Only", kColAmberBg, kColAmberBdr, kColAmberText);
}

void DrawHintText(const char* text) {
  const auto& fonts = GetUiFonts();
  PushFontSafe(fonts.caption);
  ImGui::PushStyleColor(ImGuiCol_Text, kColTextMuted);
  ImGui::TextWrapped("%s", text);
  ImGui::PopStyleColor();
  PopFontSafe(fonts.caption);
}

void DrawControlLabel(const char* label) {
  const auto& fonts = GetUiFonts();
  PushFontSafe(fonts.semibold);
  ImGui::PushStyleColor(ImGuiCol_Text, kColTextPrimary);
  ImGui::TextUnformatted(label);
  ImGui::PopStyleColor();
  PopFontSafe(fonts.semibold);
}

bool DrawDefaultResetButton(const char* id, bool is_default, const char* tooltip = nullptr) {
  const auto& fonts = GetUiFonts();
  PushFontSafe(fonts.caption);
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 3.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.0f);
  if (is_default) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.118f, 0.133f, 0.180f, 0.55f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.150f, 0.170f, 0.225f, 0.70f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.180f, 0.205f, 0.270f, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.45f, 0.50f, 0.60f, 0.85f));
  } else {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.149f, 0.173f, 0.235f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.220f, 0.300f, 0.420f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.260f, 0.360f, 0.500f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, kColSkyAccent);
  }
  ImGui::PushID(id);
  const bool clicked = ImGui::Button("Default");
  ImGui::PopID();
  if (tooltip && tooltip[0] != '\0' && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
    ImGui::SetTooltip("%s", tooltip);
  }
  ImGui::PopStyleColor(4);
  ImGui::PopStyleVar(2);
  PopFontSafe(fonts.caption);
  return clicked;
}

void DrawTabPageHeader(const char* icon, const char* title, const char* subtitle) {
  const auto& fonts = GetUiFonts();
  PushFontSafe(fonts.title);
  ImGui::PushStyleColor(ImGuiCol_Text, kColMajinLight);
  ImGui::TextUnformatted(icon);
  ImGui::PopStyleColor();
  ImGui::SameLine(0.0f, 10.0f);
  ImGui::PushStyleColor(ImGuiCol_Text, kColTextPrimary);
  ImGui::TextUnformatted(title);
  ImGui::PopStyleColor();
  PopFontSafe(fonts.title);

  if (subtitle && subtitle[0] != '\0') {
    DrawHintText(subtitle);
  }
  ImGui::Dummy(ImVec2(0.0f, 6.0f));
}

bool BeginCard(const char* id, const char* title, const char* icon = nullptr) {
  ImGui::PushStyleColor(ImGuiCol_ChildBg, kColDarkCard);
  ImGui::PushStyleColor(ImGuiCol_Border, kColDarkBorder);
  ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 14.0f));

  const ImGuiChildFlags flags =
      ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding;
  const bool open = ImGui::BeginChild(id, ImVec2(0.0f, 0.0f), flags, ImGuiWindowFlags_NoScrollbar);
  if (open && title && title[0] != '\0') {
    const auto& fonts = GetUiFonts();
    PushFontSafe(fonts.semibold);
    ImGui::PushStyleColor(ImGuiCol_Text, kColSkyAccent);
    if (icon && icon[0] != '\0') {
      ImGui::Text("%s  %s", icon, title);
    } else {
      ImGui::TextUnformatted(title);
    }
    ImGui::PopStyleColor();
    PopFontSafe(fonts.semibold);

    ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.196f, 0.220f, 0.290f, 0.65f));
    ImGui::Separator();
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0.0f, 4.0f));
  }
  return open;
}

void EndCard() {
  ImGui::EndChild();
  ImGui::PopStyleVar(2);
  ImGui::PopStyleColor(2);
  ImGui::Dummy(ImVec2(0.0f, 6.0f));
}

float ControlWidth(float max_width = 400.0f) {
  return std::min(ImGui::GetContentRegionAvail().x, max_width);
}

bool DrawSidebarItem(const char* id, const char* icon, const char* label, bool active) {
  const auto& fonts = GetUiFonts();
  PushFontSafe(fonts.semibold);

  const float width = ImGui::GetContentRegionAvail().x;
  const float height = 42.0f;
  const ImVec2 pos = ImGui::GetCursorScreenPos();
  const bool pressed = ImGui::InvisibleButton(id, ImVec2(width, height));
  const bool hovered = ImGui::IsItemHovered();

  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 p_max(pos.x + width, pos.y + height);

  if (active) {
    dl->AddRectFilled(pos, p_max, ImGui::GetColorU32(kColMajinTintBg), 10.0f);
    dl->AddRect(pos, p_max, ImGui::GetColorU32(kColMajinTintBdr), 10.0f, 0, 1.0f);
    // Crisp left accent bar matching launcher active state
    dl->AddRectFilled(
        ImVec2(pos.x + 2.0f, pos.y + 9.0f),
        ImVec2(pos.x + 5.0f, p_max.y - 9.0f),
        ImGui::GetColorU32(kColMajinGreen),
        2.0f);
  } else if (hovered) {
    dl->AddRectFilled(pos, p_max, ImGui::GetColorU32(ImVec4(0.137f, 0.153f, 0.204f, 0.78f)), 10.0f);
  }

  const ImVec4 content_col =
      active ? kColMajinLight : (hovered ? kColTextPrimary : kColTextMuted);
  const ImU32 col_u32 = ImGui::GetColorU32(content_col);

  const ImVec2 icon_sz = ImGui::CalcTextSize(icon);
  const ImVec2 label_sz = ImGui::CalcTextSize(label);
  const float icon_y = pos.y + (height - icon_sz.y) * 0.5f;
  const float label_y = pos.y + (height - label_sz.y) * 0.5f;

  dl->AddText(ImVec2(pos.x + 14.0f, icon_y), col_u32, icon);
  dl->AddText(ImVec2(pos.x + 42.0f, label_y), col_u32, label);

  PopFontSafe(fonts.semibold);
  return pressed;
}

std::string UpperAscii(std::string s) {
  for (char& ch : s) {
    ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
  }
  return s;
}

int32_t GetEffectiveFpsLimit() {
  if (!rex::cvar::GetFlagByName("majin_fps_limit").empty()) {
    const int32_t limit = rex::cvar::Query<int32_t>("majin_fps_limit");
    if (limit >= 0) return limit;
  }
  return rex::cvar::Query<bool>("majin_60fps") ? 60 : 30;
}

void ApplyLiveFpsLimit(int32_t fps_limit) {
  rex::cvar::SetFlagByName("majin_fps_limit", std::to_string(fps_limit));
  rex::cvar::SetFlagByName("majin_60fps", (fps_limit == 0 || fps_limit > 30) ? "true" : "false");
}

// Persisted across menu open/close during a play session.
// Opens fullscreen by default; switching to windowed mode remembers position & size.
struct OverlayWindowState {
  bool is_fullscreen = true;
  bool has_windowed_rect = false;
  ImVec2 windowed_pos = ImVec2(0.0f, 0.0f);
  ImVec2 windowed_size = ImVec2(880.0f, 640.0f);
};

OverlayWindowState g_overlay_state;

void ClampWindowedRect(const ImVec2& vp_pos, const ImVec2& vp_size) {
  const float min_w = std::min(720.0f, std::max(320.0f, vp_size.x - 24.0f));
  const float min_h = std::min(480.0f, std::max(280.0f, vp_size.y - 24.0f));
  const float max_w = std::max(min_w, vp_size.x - 16.0f);
  const float max_h = std::max(min_h, vp_size.y - 16.0f);

  g_overlay_state.windowed_size.x = std::clamp(g_overlay_state.windowed_size.x, min_w, max_w);
  g_overlay_state.windowed_size.y = std::clamp(g_overlay_state.windowed_size.y, min_h, max_h);

  const float min_x = vp_pos.x - g_overlay_state.windowed_size.x + 120.0f;
  const float max_x = vp_pos.x + vp_size.x - 120.0f;
  const float min_y = vp_pos.y;
  const float max_y = vp_pos.y + vp_size.y - 56.0f;

  g_overlay_state.windowed_pos.x = std::clamp(g_overlay_state.windowed_pos.x, min_x, std::max(min_x, max_x));
  g_overlay_state.windowed_pos.y = std::clamp(g_overlay_state.windowed_pos.y, min_y, std::max(min_y, max_y));
}

void InitDefaultWindowedRect(const ImVec2& vp_pos, const ImVec2& vp_size) {
  const float def_w = std::clamp(vp_size.x * 0.72f, 720.0f, 960.0f);
  const float def_h = std::clamp(vp_size.y * 0.78f, 480.0f, 680.0f);
  g_overlay_state.windowed_size = ImVec2(def_w, def_h);
  g_overlay_state.windowed_pos = ImVec2(
      vp_pos.x + (vp_size.x - def_w) * 0.5f,
      vp_pos.y + (vp_size.y - def_h) * 0.5f);
  ClampWindowedRect(vp_pos, vp_size);
  g_overlay_state.has_windowed_rect = true;
}

bool DrawWindowModeButton(const char* id, float size, bool is_fullscreen, bool* out_hovered) {
  const ImVec2 pos = ImGui::GetCursorScreenPos();
  const bool pressed = ImGui::InvisibleButton(id, ImVec2(size, size));
  const bool hovered = ImGui::IsItemHovered();
  const bool active = ImGui::IsItemActive();
  if (out_hovered) *out_hovered = hovered;

  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 p_max(pos.x + size, pos.y + size);

  if (active) {
    dl->AddRectFilled(pos, p_max, ImGui::GetColorU32(ImVec4(0.240f, 0.270f, 0.350f, 1.00f)), 7.0f);
  } else if (hovered) {
    dl->AddRectFilled(pos, p_max, ImGui::GetColorU32(ImVec4(0.196f, 0.220f, 0.290f, 0.90f)), 7.0f);
  }

  const ImU32 icon_col = ImGui::GetColorU32(
      hovered ? kColTextPrimary : ImVec4(0.760f, 0.800f, 0.860f, 1.00f));
  const float cx = pos.x + size * 0.5f;
  const float cy = pos.y + size * 0.5f;

  if (is_fullscreen) {
    // Restore Down (Windowed Mode) icon: overlapping front + back squares
    const ImVec2 front_min(cx - 5.0f, cy - 2.0f);
    const ImVec2 front_max(cx + 3.0f, cy + 6.0f);
    dl->AddRect(front_min, front_max, icon_col, 1.5f, 0, 1.5f);

    // Back square top & right edges peeking behind the front square
    dl->PathClear();
    dl->PathLineTo(ImVec2(cx - 2.0f, cy - 2.5f));
    dl->PathLineTo(ImVec2(cx - 2.0f, cy - 5.0f));
    dl->PathLineTo(ImVec2(cx + 5.5f, cy - 5.0f));
    dl->PathLineTo(ImVec2(cx + 5.5f, cy + 2.5f));
    dl->PathLineTo(ImVec2(cx + 3.0f, cy + 2.5f));
    dl->PathStroke(icon_col, 0, 1.5f);
  } else {
    // Maximize (Fullscreen Mode) icon: crisp rounded square
    dl->AddRect(ImVec2(cx - 5.0f, cy - 5.0f), ImVec2(cx + 5.0f, cy + 5.0f),
                icon_col, 1.5f, 0, 1.5f);
  }

  if (hovered) {
    ImGui::SetTooltip("%s", is_fullscreen
                                ? "Windowed Mode (drag to inspect live changes)"
                                : "Fullscreen Mode");
  }
  return pressed;
}

bool DrawCloseButton(const char* id, float size, bool* out_hovered) {
  const ImVec2 pos = ImGui::GetCursorScreenPos();
  const bool pressed = ImGui::InvisibleButton(id, ImVec2(size, size));
  const bool hovered = ImGui::IsItemHovered();
  const bool active = ImGui::IsItemActive();
  if (out_hovered) *out_hovered = hovered;

  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 p_max(pos.x + size, pos.y + size);

  if (active) {
    dl->AddRectFilled(pos, p_max, ImGui::GetColorU32(kColRoseBtn), 7.0f);
  } else if (hovered) {
    dl->AddRectFilled(pos, p_max, ImGui::GetColorU32(kColRoseBtnHov), 7.0f);
  }

  const ImU32 icon_col = ImGui::GetColorU32(
      hovered ? kColTextPrimary : ImVec4(0.760f, 0.800f, 0.860f, 1.00f));
  const float cx = pos.x + size * 0.5f;
  const float cy = pos.y + size * 0.5f;
  constexpr float kHalf = 4.5f;
  dl->AddLine(ImVec2(cx - kHalf, cy - kHalf), ImVec2(cx + kHalf, cy + kHalf), icon_col, 1.75f);
  dl->AddLine(ImVec2(cx + kHalf, cy - kHalf), ImVec2(cx - kHalf, cy + kHalf), icon_col, 1.75f);

  if (hovered) {
    ImGui::SetTooltip("Close Menu (F1 / ESC)");
  }
  return pressed;
}

}  // namespace

InGameMenuDialog::InGameMenuDialog(rex::ui::ImGuiDrawer* imgui_drawer,
                                   std::function<void()> on_resume,
                                   std::function<void()> on_quit,
                                   std::function<void()> on_clear_caches)
    : rex::ui::ImGuiDialog(imgui_drawer),
      on_resume_(std::move(on_resume)),
      on_quit_(std::move(on_quit)),
      on_clear_caches_(std::move(on_clear_caches)),
      apply_windowed_rect_(!g_overlay_state.is_fullscreen) {
  rex::input::mnk::SetMouseLookActive(false);
  REXLOG_INFO("[InGameMenu] Opened in-game settings overlay.");
}

InGameMenuDialog::~InGameMenuDialog() {
  rex::input::mnk::SetMouseLookActive(true);
  REXLOG_INFO("[InGameMenu] Closed in-game settings overlay.");
}

bool InGameMenuDialog::SaveSettingsToLauncherConfig() {
  const auto config_path = FindLauncherConfigPath();
  if (config_path.empty()) {
    REXLOG_WARN("[InGameMenu] launcher-usa.json not found to save settings.");
    return false;
  }

  std::ifstream infile(config_path);
  if (!infile.is_open()) return false;
  std::stringstream buffer;
  buffer << infile.rdbuf();
  infile.close();

  std::string json = buffer.str();

  // Read current live cvar states
  const std::string post_effect = rex::cvar::Query<std::string>("swap_post_effect");
  const double dedither_strength = rex::cvar::Query<double>("swap_dedither_strength");
  std::string foliage_alpha_mode = "solid";
  if (!rex::cvar::GetFlagByName("majin_foliage_alpha_mode").empty()) {
    foliage_alpha_mode = rex::cvar::Query<std::string>("majin_foliage_alpha_mode");
  }
  const std::string upscaler = rex::cvar::Query<std::string>("present_effect");
  const double mouse_sens = rex::cvar::Query<double>("mnk_sensitivity");
  const double fov_scale = rex::cvar::Query<double>("majin_fov_scale");
  const double draw_distance_scale = rex::cvar::Query<double>("majin_draw_distance_scale");
  const double dof = rex::cvar::Query<double>("majin_dof");
  std::string dof_mode = "bokeh";
  if (!rex::cvar::GetFlagByName("majin_dof_mode").empty()) {
    dof_mode = rex::cvar::Query<std::string>("majin_dof_mode");
  }
  const double bloom = rex::cvar::Query<double>("majin_bloom");
  const double fog = rex::cvar::Query<double>("majin_fog");
  bool tepeu_ambient_vfx = true;
  if (!rex::cvar::GetFlagByName("majin_tepeu_ambient_vfx").empty()) {
    tepeu_ambient_vfx = rex::cvar::Query<bool>("majin_tepeu_ambient_vfx");
  }
  bool tyndall_enabled = true;
  if (!rex::cvar::GetFlagByName("majin_tyndall").empty()) {
    tyndall_enabled = rex::cvar::Query<bool>("majin_tyndall");
  }
  const bool pause_distortion = rex::cvar::Query<bool>("majin_pause_distortion");
  const bool dither = rex::cvar::Query<bool>("present_dither");
  const bool vsync = rex::cvar::Query<bool>("vsync");
  const bool step_fix = rex::cvar::Query<bool>("majin_step_fix");
  const bool anim_fix = rex::cvar::Query<bool>("majin_submotion_fix");
  const bool dt_smoothing = rex::cvar::Query<bool>("majin_dt_smooth");
  const bool skip_cutscenes = rex::cvar::Query<bool>("majin_skip_cutscenes");
  const int32_t cutscene_speed = rex::cvar::Query<int32_t>("majin_cutscene_speed");
  const double cas_sharp = rex::cvar::Query<double>("present_cas_additional_sharpness");
  const double fsr_reduction = rex::cvar::Query<double>("present_fsr_sharpness_reduction");
  const bool window_fps = rex::cvar::Query<bool>("majin_window_fps");
  const bool dev_mode = rex::cvar::Query<bool>("majin_dev_mode");
  const int32_t shadow_scale = rex::cvar::Query<int32_t>("majin_shadow_scale");
  std::string shadow_pcf_mode = "soft";
  if (!rex::cvar::GetFlagByName("majin_shadow_pcf_mode").empty()) {
    shadow_pcf_mode = rex::cvar::Query<std::string>("majin_shadow_pcf_mode");
  }
  bool shadow_receiver_bias = true;
  if (!rex::cvar::GetFlagByName("majin_shadow_receiver_bias").empty()) {
    shadow_receiver_bias = rex::cvar::Query<bool>("majin_shadow_receiver_bias");
  }
  bool shadow_two_sided = false;
  if (!rex::cvar::GetFlagByName("majin_shadow_two_sided").empty()) {
    shadow_two_sided = rex::cvar::Query<bool>("majin_shadow_two_sided");
  }
  bool shadow_texel_snap = true;
  if (!rex::cvar::GetFlagByName("majin_shadow_texel_snap").empty()) {
    shadow_texel_snap = rex::cvar::Query<bool>("majin_shadow_texel_snap");
  }
  std::string shadow_sssm_mode = "auto";
  if (!rex::cvar::GetFlagByName("majin_shadow_sssm_mode").empty()) {
    shadow_sssm_mode = rex::cvar::Query<std::string>("majin_shadow_sssm_mode");
  }
  double shadow_slope_bias = 0.00;
  if (!rex::cvar::GetFlagByName("majin_shadow_slope_bias").empty()) {
    shadow_slope_bias = rex::cvar::Query<double>("majin_shadow_slope_bias");
  }
  double shadow_const_bias = 0.000;
  if (!rex::cvar::GetFlagByName("majin_shadow_constant_bias").empty()) {
    shadow_const_bias = rex::cvar::Query<double>("majin_shadow_constant_bias");
  }
  double shadow_bias_clamp = 0.000;
  if (!rex::cvar::GetFlagByName("majin_shadow_bias_clamp").empty()) {
    shadow_bias_clamp = rex::cvar::Query<double>("majin_shadow_bias_clamp");
  }

  char sens_buf[32];
  snprintf(sens_buf, sizeof(sens_buf), "%.1f", mouse_sens);
  char fov_buf[32];
  snprintf(fov_buf, sizeof(fov_buf), "%.2f", fov_scale);
  char draw_dist_buf[32];
  snprintf(draw_dist_buf, sizeof(draw_dist_buf), "%.2f", draw_distance_scale);
  char strength_buf[32];
  snprintf(strength_buf, sizeof(strength_buf), "%.2f", dedither_strength);
  char dof_buf[32];
  snprintf(dof_buf, sizeof(dof_buf), "%.2f", dof);
  char bloom_buf[32];
  snprintf(bloom_buf, sizeof(bloom_buf), "%.2f", bloom);
  char fog_buf[32];
  snprintf(fog_buf, sizeof(fog_buf), "%.2f", fog);
  char cas_buf[32];
  snprintf(cas_buf, sizeof(cas_buf), "%.2f", cas_sharp);
  char fsr_buf[32];
  snprintf(fsr_buf, sizeof(fsr_buf), "%.2f", fsr_reduction);
  char slope_bias_buf[32];
  snprintf(slope_bias_buf, sizeof(slope_bias_buf), "%.2f", shadow_slope_bias);
  char const_bias_buf[32];
  snprintf(const_bias_buf, sizeof(const_bias_buf), "%.3f", shadow_const_bias);
  char bias_clamp_buf[32];
  snprintf(bias_clamp_buf, sizeof(bias_clamp_buf), "%.3f", shadow_bias_clamp);

  const int32_t active_fps_limit = GetEffectiveFpsLimit();
  const std::string frame_rate_mode =
      (active_fps_limit == 0) ? "Uncapped" : (active_fps_limit > 30 ? "60" : "30");
  int32_t active_render_scale = 1;
  if (!rex::cvar::GetFlagByName("resolution_scale").empty()) {
    active_render_scale = std::clamp(rex::cvar::Query<int32_t>("resolution_scale"), 1, 3);
  }

  ReplaceJsonField(json, "RenderScale", std::to_string(active_render_scale));
  ReplaceJsonField(json, "FrameRateMode", "\"" + frame_rate_mode + "\"");
  ReplaceJsonField(json, "PostEffect", "\"" + post_effect + "\"");
  ReplaceJsonField(json, "DeDitherStrength", strength_buf);
  ReplaceJsonField(json, "FoliageAlphaMode", "\"" + foliage_alpha_mode + "\"");
  ReplaceJsonField(json, "Upscaler", "\"" + upscaler + "\"");
  ReplaceJsonField(json, "CasSharpness", cas_buf);
  ReplaceJsonField(json, "FsrReduction", fsr_buf);
  ReplaceJsonField(json, "MouseSensitivity", sens_buf);
  ReplaceJsonField(json, "FovScale", fov_buf);
  ReplaceJsonField(json, "DrawDistanceScale", draw_dist_buf);
  ReplaceJsonField(json, "DepthOfField", dof > 0.001 ? "true" : "false");
  ReplaceJsonField(json, "DepthOfFieldIntensity", dof_buf);
  ReplaceJsonField(json, "DepthOfFieldMode", "\"" + dof_mode + "\"");
  ReplaceJsonField(json, "Bloom", bloom > 0.0 ? "true" : "false");
  ReplaceJsonField(json, "BloomIntensity", bloom_buf);
  ReplaceJsonField(json, "FogIntensity", fog_buf);
  ReplaceJsonField(json, "TepeuAmbientVfx", tepeu_ambient_vfx ? "true" : "false");
  ReplaceJsonField(json, "Tyndall", tyndall_enabled ? "true" : "false");
  ReplaceJsonField(json, "PauseDistortion", pause_distortion ? "true" : "false");
  ReplaceJsonField(json, "PresentDither", dither ? "true" : "false");
  ReplaceJsonField(json, "WindowFps", window_fps ? "true" : "false");
  ReplaceJsonField(json, "VSync", vsync ? "true" : "false");
  ReplaceJsonField(json, "StepFix", step_fix ? "true" : "false");
  ReplaceJsonField(json, "AnimSpeedFix", anim_fix ? "true" : "false");
  ReplaceJsonField(json, "DtSmoothing", dt_smoothing ? "true" : "false");
  ReplaceJsonField(json, "SkipCutscenes", skip_cutscenes ? "true" : "false");
  ReplaceJsonField(json, "CutsceneSpeed", std::to_string(cutscene_speed));
  ReplaceJsonField(json, "ShadowScale", std::to_string(shadow_scale <= 1 ? 1 : (shadow_scale >= 4 ? 4 : 2)));
  ReplaceJsonField(json, "ShadowPcfMode", "\"" + shadow_pcf_mode + "\"");
  ReplaceJsonField(json, "ShadowReceiverBias", shadow_receiver_bias ? "true" : "false");
  ReplaceJsonField(json, "ShadowTwoSided", shadow_two_sided ? "true" : "false");
  ReplaceJsonField(json, "ShadowTexelSnap", shadow_texel_snap ? "true" : "false");
  ReplaceJsonField(json, "ShadowSssmMode", "\"" + shadow_sssm_mode + "\"");
  ReplaceJsonField(json, "ShadowSlopeBias", slope_bias_buf);
  ReplaceJsonField(json, "ShadowConstantBias", const_bias_buf);
  ReplaceJsonField(json, "ShadowBiasClamp", bias_clamp_buf);
  ReplaceJsonField(json, "DevMode", dev_mode ? "true" : "false");

  std::ofstream outfile(config_path, std::ios::trunc);
  if (!outfile.is_open()) return false;
  outfile << json;
  outfile.close();

  REXLOG_INFO("[InGameMenu] Successfully updated launcher configuration: {}", config_path.string());
  return true;
}

void InGameMenuDialog::DrawSidebar() {
  const auto& fonts = GetUiFonts();

  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.094f, 0.106f, 0.141f, 0.55f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 14.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);

  if (ImGui::BeginChild("MajinSidebar", ImVec2(216.0f, 0.0f),
                        ImGuiChildFlags_AlwaysUseWindowPadding,
                        ImGuiWindowFlags_NoScrollbar)) {
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 6.0f));

    if (DrawSidebarItem("##NavGraphics", ICON_SOLAR_MONITOR, "Graphics",
                        active_tab_ == Tab::kGraphics)) {
      active_tab_ = Tab::kGraphics;
    }
    if (DrawSidebarItem("##NavShadow", ICON_SOLAR_SHADOW, "Shadow",
                        active_tab_ == Tab::kShadow)) {
      active_tab_ = Tab::kShadow;
    }
    if (DrawSidebarItem("##NavAtmosphere", ICON_SOLAR_ATMOSPHERE, "Atmosphere",
                        active_tab_ == Tab::kAtmosphere)) {
      active_tab_ = Tab::kAtmosphere;
    }
    if (DrawSidebarItem("##NavControls", ICON_SOLAR_GAMEPAD, "Controls",
                        active_tab_ == Tab::kControls)) {
      active_tab_ = Tab::kControls;
    }
    if (DrawSidebarItem("##NavFixes", ICON_SOLAR_SHIELD, "Fixes & Patches",
                        active_tab_ == Tab::kFixes)) {
      active_tab_ = Tab::kFixes;
    }
    if (DrawSidebarItem("##NavPreferences", ICON_SOLAR_SETTINGS, "Preferences",
                        active_tab_ == Tab::kPreferences)) {
      active_tab_ = Tab::kPreferences;
    }

    ImGui::PopStyleVar();

    // Bottom session badge inside sidebar
    const float footer_card_h = 56.0f;
    const float remaining_h = ImGui::GetContentRegionAvail().y;
    if (remaining_h > footer_card_h + 12.0f) {
      ImGui::Dummy(ImVec2(0.0f, remaining_h - footer_card_h - 4.0f));
      ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.059f, 0.067f, 0.090f, 0.85f));
      ImGui::PushStyleColor(ImGuiCol_Border, kColDarkBorder);
      ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
      ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 8.0f));
      if (ImGui::BeginChild("SidebarStatus", ImVec2(0.0f, footer_card_h),
                            ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding,
                            ImGuiWindowFlags_NoScrollbar)) {
        const std::string backend = rex::cvar::GetFlagByName("gpu_backend");
        const std::string res_scale = rex::cvar::GetFlagByName("resolution_scale");
        const bool is_vulkan = (backend == "vulkan");
        const std::string backend_upper = UpperAscii(backend.empty() ? "d3d12" : backend);

        PushFontSafe(fonts.caption);
        ImGui::PushStyleColor(ImGuiCol_Text, kColTextMuted);
        ImGui::TextUnformatted("ACTIVE SESSION");
        ImGui::PopStyleColor();

        ImGui::PushStyleColor(ImGuiCol_Text, is_vulkan ? kColSkyAccent : kColAmberText);
        ImGui::Text("%s", backend_upper.c_str());
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, kColTextMuted);
        ImGui::Text("|  %sx Scale", res_scale.empty() ? "1" : res_scale.c_str());
        ImGui::PopStyleColor();
        PopFontSafe(fonts.caption);
      }
      ImGui::EndChild();
      ImGui::PopStyleVar(2);
      ImGui::PopStyleColor(2);
    }

    // Crisp right divider matching launcher `border-r border-darkBorder`
    const ImVec2 wpos = ImGui::GetWindowPos();
    const ImVec2 wsz = ImGui::GetWindowSize();
    ImGui::GetWindowDrawList()->AddLine(
        ImVec2(wpos.x + wsz.x - 1.0f, wpos.y),
        ImVec2(wpos.x + wsz.x - 1.0f, wpos.y + wsz.y),
        ImGui::GetColorU32(kColDarkBorder),
        1.0f);
  }
  ImGui::EndChild();

  ImGui::PopStyleVar(2);
  ImGui::PopStyleColor();
}

void InGameMenuDialog::OnDraw(ImGuiIO& io) {
  if (save_status_timer_ > 0.0f) {
    save_status_timer_ -= io.DeltaTime;
    if (save_status_timer_ <= 0.0f) {
      save_status_msg_.clear();
    }
  }

  const auto& fonts = GetUiFonts();
  PushFontSafe(fonts.body);

  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  const ImVec2 vp_pos = viewport ? viewport->Pos : ImVec2(0.0f, 0.0f);
  const ImVec2 vp_size =
      (viewport && viewport->Size.x > 0.0f && viewport->Size.y > 0.0f)
          ? viewport->Size
          : io.DisplaySize;

  const bool is_fullscreen = g_overlay_state.is_fullscreen;

  // Zero-latency custom title bar drag when in windowed mode
  if (dragging_window_) {
    if (!is_fullscreen && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
      g_overlay_state.windowed_pos = ImVec2(
          io.MousePos.x - drag_offset_.x,
          io.MousePos.y - drag_offset_.y);
      ClampWindowedRect(vp_pos, vp_size);
      ImGui::SetNextWindowPos(g_overlay_state.windowed_pos, ImGuiCond_Always);
    } else {
      dragging_window_ = false;
    }
  }

  ImGuiWindowFlags window_flags =
      ImGuiWindowFlags_NoCollapse |
      ImGuiWindowFlags_NoTitleBar |
      ImGuiWindowFlags_NoScrollbar |
      ImGuiWindowFlags_NoScrollWithMouse;

  if (is_fullscreen) {
    ImGui::SetNextWindowPos(vp_pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(vp_size, ImGuiCond_Always);
    window_flags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize;
  } else {
    if (!g_overlay_state.has_windowed_rect) {
      InitDefaultWindowedRect(vp_pos, vp_size);
      apply_windowed_rect_ = true;
    }

    const float min_w = std::min(720.0f, std::max(320.0f, vp_size.x - 24.0f));
    const float min_h = std::min(480.0f, std::max(280.0f, vp_size.y - 24.0f));
    const float max_w = std::max(min_w, vp_size.x - 16.0f);
    const float max_h = std::max(min_h, vp_size.y - 16.0f);
    ImGui::SetNextWindowSizeConstraints(ImVec2(min_w, min_h), ImVec2(max_w, max_h));

    if (apply_windowed_rect_) {
      ClampWindowedRect(vp_pos, vp_size);
      ImGui::SetNextWindowPos(g_overlay_state.windowed_pos, ImGuiCond_Always);
      ImGui::SetNextWindowSize(g_overlay_state.windowed_size, ImGuiCond_Always);
      apply_windowed_rect_ = false;
    }
  }

  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, is_fullscreen ? 0.0f : 12.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, is_fullscreen ? 0.0f : 1.0f);
  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.059f, 0.067f, 0.090f, 0.97f));

  const bool window_open =
      ImGui::Begin("Majistic Recomp - In-Game Settings###MajinInGameOverlay", nullptr, window_flags);

  ImGui::PopStyleColor();
  ImGui::PopStyleVar(3);

  if (window_open) {
    const ImVec2 parent_win_pos = ImGui::GetWindowPos();
    const ImVec2 parent_win_size = ImGui::GetWindowSize();
    if (!is_fullscreen) {
      g_overlay_state.windowed_pos = parent_win_pos;
      g_overlay_state.windowed_size = parent_win_size;
      g_overlay_state.has_windowed_rect = true;
    }

    const bool dev_mode = rex::cvar::Query<bool>("majin_dev_mode");
    constexpr float kHeaderHeight = 56.0f;
    constexpr float kFooterHeight = 58.0f;
    bool toggle_mode_requested = false;

    // =========================================================================
    // TOP HEADER BAR (matching launcher appHeader)
    // =========================================================================
    ImGui::PushStyleColor(ImGuiCol_ChildBg, kColDarkCard);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
    if (ImGui::BeginChild("MajinTopHeader", ImVec2(0.0f, kHeaderHeight),
                          ImGuiChildFlags_AlwaysUseWindowPadding,
                          ImGuiWindowFlags_NoScrollbar)) {
      const float title_h = fonts.title ? fonts.title->LegacySize : ImGui::GetTextLineHeight();
      ImGui::SetCursorPosY((kHeaderHeight - title_h) * 0.5f);

      PushFontSafe(fonts.title);
      ImGui::PushStyleColor(ImGuiCol_Text, kColTextPrimary);
      ImGui::TextUnformatted("MAJISTIC RECOMP");
      ImGui::PopStyleColor();
      PopFontSafe(fonts.title);

      ImGui::SameLine(0.0f, 12.0f);
      ImGui::SetCursorPosY((kHeaderHeight - 20.0f) * 0.5f);
      DrawPillBadge("IN-GAME", kColSkyBadgeBg, kColSkyBadgeBdr, kColSkyAccent);

      if (dev_mode) {
        ImGui::SameLine(0.0f, 8.0f);
        ImGui::SetCursorPosY((kHeaderHeight - 20.0f) * 0.5f);
        DrawPillBadge("DEV", kColAmberBg, kColAmberBdr, kColAmberText);
      }

      // Right side: shortcut hint + windowed/fullscreen toggle + close button
      const char* shortcut_hint = is_fullscreen
                                      ? "Live Settings  |  Press [F1] or [ESC] to resume"
                                      : "Drag bar to move  |  [F1] or [ESC] to resume";
      PushFontSafe(fonts.caption);
      const float hint_w = ImGui::CalcTextSize(shortcut_hint).x;
      const float hint_h = ImGui::GetTextLineHeight();
      PopFontSafe(fonts.caption);

      constexpr float kHeaderBtnSz = 28.0f;
      constexpr float kHeaderBtnGap = 6.0f;
      const float right_edge = ImGui::GetWindowWidth() - 16.0f;
      const float close_btn_x = right_edge - kHeaderBtnSz;
      const float mode_btn_x = close_btn_x - kHeaderBtnGap - kHeaderBtnSz;
      const float hint_x = mode_btn_x - 14.0f - hint_w;

      if (hint_x > ImGui::GetCursorPosX() + 16.0f) {
        ImGui::SameLine();
        ImGui::SetCursorPos(ImVec2(hint_x, (kHeaderHeight - hint_h) * 0.5f));
        PushFontSafe(fonts.caption);
        ImGui::PushStyleColor(ImGuiCol_Text, kColTextMuted);
        ImGui::TextUnformatted(shortcut_hint);
        ImGui::PopStyleColor();
        PopFontSafe(fonts.caption);
      }

      const float btn_y = (kHeaderHeight - kHeaderBtnSz) * 0.5f;
      bool mode_btn_hovered = false;
      bool close_btn_hovered = false;

      ImGui::SetCursorPos(ImVec2(mode_btn_x, btn_y));
      if (DrawWindowModeButton("##HeaderWindowMode", kHeaderBtnSz, is_fullscreen, &mode_btn_hovered)) {
        toggle_mode_requested = true;
      }

      ImGui::SetCursorPos(ImVec2(close_btn_x, btn_y));
      if (DrawCloseButton("##HeaderClose", kHeaderBtnSz, &close_btn_hovered)) {
        if (on_resume_) on_resume_();
      }

      // Drag header to reposition window in windowed mode; double-click to toggle fullscreen
      const bool header_btn_hovered = mode_btn_hovered || close_btn_hovered;
      if (ImGui::IsWindowHovered() && !header_btn_hovered && !ImGui::IsAnyItemActive()) {
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
          toggle_mode_requested = true;
        } else if (!is_fullscreen && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
          dragging_window_ = true;
          drag_offset_ = ImVec2(io.MousePos.x - parent_win_pos.x,
                                io.MousePos.y - parent_win_pos.y);
        }
      }

      // Bottom divider
      const ImVec2 hpos = ImGui::GetWindowPos();
      const ImVec2 hsz = ImGui::GetWindowSize();
      ImGui::GetWindowDrawList()->AddLine(
          ImVec2(hpos.x, hpos.y + hsz.y - 1.0f),
          ImVec2(hpos.x + hsz.x, hpos.y + hsz.y - 1.0f),
          ImGui::GetColorU32(kColDarkBorder),
          1.0f);
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();

    if (toggle_mode_requested) {
      dragging_window_ = false;
      if (g_overlay_state.is_fullscreen) {
        g_overlay_state.is_fullscreen = false;
        if (!g_overlay_state.has_windowed_rect) {
          InitDefaultWindowedRect(vp_pos, vp_size);
        } else {
          ClampWindowedRect(vp_pos, vp_size);
        }
        apply_windowed_rect_ = true;
      } else {
        g_overlay_state.windowed_pos = parent_win_pos;
        g_overlay_state.windowed_size = parent_win_size;
        g_overlay_state.has_windowed_rect = true;
        g_overlay_state.is_fullscreen = true;
      }
    }

    // =========================================================================
    // MIDDLE WORKSPACE: Vertical Sidebar + Scrollable Content Area
    // =========================================================================
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
    const float middle_h = std::max(120.0f, ImGui::GetContentRegionAvail().y - kFooterHeight);
    if (ImGui::BeginChild("MajinWorkspace", ImVec2(0.0f, middle_h), ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
      DrawSidebar();

      ImGui::SameLine(0.0f, 0.0f);

      // Right scrollable content area
      ImGui::PushStyleColor(ImGuiCol_ChildBg, kColDarkBg);
      ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(22.0f, 18.0f));
      ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
      ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10.0f, 8.0f));
      if (ImGui::BeginChild("MajinContentScroll", ImVec2(0.0f, 0.0f),
                            ImGuiChildFlags_AlwaysUseWindowPadding)) {
        switch (active_tab_) {
          case Tab::kGraphics:
            DrawGraphicsTab();
            break;
          case Tab::kShadow:
            DrawShadowTab();
            break;
          case Tab::kAtmosphere:
            DrawAtmosphereTab();
            break;
          case Tab::kControls:
            DrawControlsTab();
            break;
          case Tab::kFixes:
            DrawFixesTab();
            break;
          case Tab::kPreferences:
            DrawPreferencesTab();
            break;
        }
      }
      ImGui::EndChild();
      ImGui::PopStyleVar(3);
      ImGui::PopStyleColor();
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();

    // =========================================================================
    // BOTTOM ACTION BAR (matching launcher footer)
    // =========================================================================
    ImGui::PushStyleColor(ImGuiCol_ChildBg, kColDarkCard);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
    if (ImGui::BeginChild("MajinFooterBar", ImVec2(0.0f, kFooterHeight),
                          ImGuiChildFlags_AlwaysUseWindowPadding,
                          ImGuiWindowFlags_NoScrollbar)) {
      // Top divider
      const ImVec2 fpos = ImGui::GetWindowPos();
      const ImVec2 fsz = ImGui::GetWindowSize();
      ImGui::GetWindowDrawList()->AddLine(
          ImVec2(fpos.x, fpos.y),
          ImVec2(fpos.x + fsz.x, fpos.y),
          ImGui::GetColorU32(kColDarkBorder),
          1.0f);

      constexpr float kBtnH = 34.0f;
      const float btn_y = (kFooterHeight - kBtnH) * 0.5f;
      ImGui::SetCursorPosY(btn_y);

      PushFontSafe(fonts.semibold);
      ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);

      // Save to Launcher Config button
      ImGui::PushStyleColor(ImGuiCol_Button, kColDarkControl);
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.196f, 0.220f, 0.290f, 1.00f));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.240f, 0.270f, 0.350f, 1.00f));
      if (ImGui::Button(ICON_SOLAR_DISKETTE "  Save to Launcher Config", ImVec2(226.0f, kBtnH))) {
        if (SaveSettingsToLauncherConfig()) {
          save_status_msg_ = "Settings saved to launcher!";
          save_status_timer_ = 3.0f;
        } else {
          save_status_msg_ = "Could not save to config file.";
          save_status_timer_ = 3.0f;
        }
      }
      ImGui::PopStyleColor(3);

      if (!save_status_msg_.empty()) {
        ImGui::SameLine(0.0f, 12.0f);
        ImGui::SetCursorPosY((kFooterHeight - ImGui::GetTextLineHeight()) * 0.5f);
        ImGui::PushStyleColor(ImGuiCol_Text, kColMajinLight);
        ImGui::TextUnformatted(save_status_msg_.c_str());
        ImGui::PopStyleColor();
      }

      // Right-aligned Resume (Majin Green) & Quit Game (Rose) buttons
      constexpr float kResumeW = 128.0f;
      constexpr float kQuitW = 136.0f;
      constexpr float kGap = 10.0f;
      const float right_start = ImGui::GetWindowWidth() - 18.0f - (kResumeW + kQuitW + kGap);
      if (right_start > ImGui::GetCursorPosX() + 8.0f) {
        ImGui::SetCursorPos(ImVec2(right_start, btn_y));
      } else {
        ImGui::SameLine();
        ImGui::SetCursorPosY(btn_y);
      }

      // Resume button — Majin green matching launcher LAUNCH GAME CTA
      ImGui::PushStyleColor(ImGuiCol_Button, kColMajinGreen);
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kColMajinHover);
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, kColMajinActive);
      ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.50f, 0.76f, 0.10f, 0.80f));
      if (ImGui::Button(ICON_SOLAR_PLAY "  Resume", ImVec2(kResumeW, kBtnH))) {
        if (on_resume_) on_resume_();
      }
      ImGui::PopStyleColor(4);

      ImGui::SameLine(0.0f, kGap);
      ImGui::SetCursorPosY(btn_y);

      // Quit button
      ImGui::PushStyleColor(ImGuiCol_Button, kColRoseBtn);
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kColRoseBtnHov);
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.50f, 0.05f, 0.18f, 1.00f));
      ImGui::PushStyleColor(ImGuiCol_Border, kColRoseBdr);
      if (ImGui::Button(ICON_SOLAR_LOGOUT "  Quit Game", ImVec2(kQuitW, kBtnH))) {
        show_quit_confirm_ = true;
      }
      ImGui::PopStyleColor(4);

      ImGui::PopStyleVar();
      PopFontSafe(fonts.semibold);
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();

    DrawQuitConfirmModal();
  }
  ImGui::End();

  PopFontSafe(fonts.body);
}

void InGameMenuDialog::DrawGraphicsTab() {
  DrawTabPageHeader(ICON_SOLAR_MONITOR, "Graphics",
                    "Display scaling, frame rate, sync, upscaling, anti-aliasing, camera, and visual effects.");

  const bool dev_mode = rex::cvar::Query<bool>("majin_dev_mode");
  const bool is_vulkan = (rex::cvar::GetFlagByName("gpu_backend") == "vulkan");

  // Card 1: Display & Output (Render Scale, Frame Rate Limit, VSync, Dither, Window FPS)
  if (BeginCard("CardGraphicsDisplay", "DISPLAY & OUTPUT", ICON_SOLAR_MONITOR)) {
    // Render Scale (Vulkan live hot-reload; D3D12 deprecated / restart required)
    {
      int32_t current_scale = 1;
      if (!rex::cvar::GetFlagByName("resolution_scale").empty()) {
        current_scale = std::clamp(rex::cvar::Query<int32_t>("resolution_scale"), 1, 3);
      }
      const char* scale_labels[] = {
          "1x - 720p (Native)",
          "2x - 1440p",
          "3x - 4K",
      };
      int scale_idx = current_scale - 1;

      DrawControlLabel("Render Scale");
      if (!is_vulkan) {
        DrawVulkanOnlyBadge();
      }
      ImGui::BeginDisabled(!is_vulkan);
      ImGui::SetNextItemWidth(ControlWidth());
      if (ImGui::Combo("##RenderScale", &scale_idx, scale_labels, IM_ARRAYSIZE(scale_labels))) {
        const int32_t new_scale = scale_idx + 1;
        rex::cvar::SetFlagByName("resolution_scale", std::to_string(new_scale));
      }
      ImGui::EndDisabled();
      if (is_vulkan) {
        DrawHintText("Changes internal 3D rendering resolution live on Vulkan (brief pause while pipelines rebuild).");
      } else {
        DrawHintText("Live Render Scale requires Vulkan. DirectX 12 is deprecated (Dev Mode only) and requires restarting via the launcher.");
      }
    }

    ImGui::Spacing();

    // Frame Rate Limit (live hot-reload via guest vblank pacing + Vulkan hook)
    {
      const int32_t current_fps = GetEffectiveFpsLimit();
      const char* fps_labels_std[] = {
          "30 FPS (Native)",
          "60 FPS (Patched)",
      };
      const int32_t fps_values_std[] = {30, 60};
      const char* fps_labels_dev[] = {
          "30 FPS (Native)",
          "60 FPS (Patched)",
          "Uncapped (Experimental)",
      };
      const int32_t fps_values_dev[] = {30, 60, 0};

      const char* const* fps_labels = dev_mode ? fps_labels_dev : fps_labels_std;
      const int32_t* fps_values = dev_mode ? fps_values_dev : fps_values_std;
      const int fps_count = dev_mode ? IM_ARRAYSIZE(fps_values_dev) : IM_ARRAYSIZE(fps_values_std);

      int fps_idx = (current_fps == 0 && dev_mode) ? 2 : (current_fps > 30 ? 1 : 0);
      DrawControlLabel("Frame Rate Limit");
      if (current_fps == 0) {
        DrawExperimentalBadge();
      }
      ImGui::SetNextItemWidth(ControlWidth());
      if (ImGui::Combo("##FrameRateLimit", &fps_idx, fps_labels, fps_count)) {
        ApplyLiveFpsLimit(fps_values[fps_idx]);
      }
      if (current_fps == 0 && rex::cvar::Query<bool>("vsync")) {
        DrawHintText("Uncapped guest pacing active. Disable VSync (or launch with VSync off on Vulkan) to exceed your monitor's refresh rate.");
      } else {
        DrawHintText("Switches guest frame pacing live without restarting (30 FPS native or 60 FPS patched).");
      }
    }

    ImGui::Spacing();

    bool vsync = rex::cvar::Query<bool>("vsync");
    if (ImGui::Checkbox("VSync", &vsync)) {
      rex::cvar::SetFlagByName("vsync", vsync ? "true" : "false");
    }

    ImGui::Spacing();

    bool dither = rex::cvar::Query<bool>("present_dither");
    if (ImGui::Checkbox("Output Dithering", &dither)) {
      rex::cvar::SetFlagByName("present_dither", dither ? "true" : "false");
    }
    DrawHintText("Applies swapchain color dithering (present_dither). Disable to eliminate screen grain/noise.");

    if (dev_mode) {
      ImGui::Spacing();
      bool show_fps = rex::cvar::Query<bool>("majin_window_fps");
      if (ImGui::Checkbox("Window Title FPS", &show_fps)) {
        rex::cvar::SetFlagByName("majin_window_fps", show_fps ? "true" : "false");
      }
      DrawHintText("Show Live Guest FPS in Window Title (majin_window_fps).");
    }
  }
  EndCard();

  // Card 2: Upscaling & Anti-Aliasing
  if (BeginCard("CardGraphicsAA", "UPSCALING & ANTI-ALIASING", ICON_SOLAR_SLIDERS)) {
    // Upscaler (present_effect)
    const std::string current_upscaler = rex::cvar::GetFlagByName("present_effect");
    const char* upscaler_labels[] = {
        "Bilinear (Native)",
        "FSR 1.0",
        "Contrast Adaptive Sharpening (CAS)",
    };
    const char* upscaler_values[] = {
        "bilinear",
        "fsr",
        "cas",
    };

    int upscaler_idx = 0;
    for (int i = 0; i < IM_ARRAYSIZE(upscaler_values); ++i) {
      if (current_upscaler == upscaler_values[i]) {
        upscaler_idx = i;
        break;
      }
    }

    DrawControlLabel("Upscaler");
    ImGui::SetNextItemWidth(ControlWidth());
    if (ImGui::Combo("##PresentEffect", &upscaler_idx, upscaler_labels, IM_ARRAYSIZE(upscaler_labels))) {
      rex::cvar::SetFlagByName("present_effect", upscaler_values[upscaler_idx]);
    }

    // Sharpening controls based on active upscaler
    if (current_upscaler == "cas") {
      ImGui::Spacing();
      float cas_sharp = static_cast<float>(rex::cvar::Query<double>("present_cas_additional_sharpness"));
      DrawControlLabel("CAS Sharpness");
      ImGui::SetNextItemWidth(ControlWidth(360.0f));
      if (ImGui::SliderFloat("##CasSharpness", &cas_sharp, 0.0f, 1.0f, "%.2f")) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.2f", cas_sharp);
        rex::cvar::SetFlagByName("present_cas_additional_sharpness", buf);
      }
      DrawHintText("Contrast Adaptive Sharpening intensity (0.00 = default, 1.00 = max).");
    } else if (current_upscaler == "fsr") {
      ImGui::Spacing();
      float fsr_reduction = static_cast<float>(rex::cvar::Query<double>("present_fsr_sharpness_reduction"));
      DrawControlLabel("FSR RCAS Sharpness Reduction");
      ImGui::SetNextItemWidth(ControlWidth(360.0f));
      if (ImGui::SliderFloat("##FsrSharpnessReduction", &fsr_reduction, 0.0f, 2.0f, "%.2f stops")) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.2f", fsr_reduction);
        rex::cvar::SetFlagByName("present_fsr_sharpness_reduction", buf);
      }
      DrawHintText("0.00 = max sharpness; higher values reduce RCAS sharpening strength.");
    }

    ImGui::Spacing();

    // Anti-Aliasing (swap_post_effect)
    const std::string current_post = rex::cvar::GetFlagByName("swap_post_effect");
    const char* post_effect_labels_dev[] = {
        "Off (Native)",
        "FXAA 3.11 Default",
        "FXAA 3.11 Quality",
        "De-Dither",
        "De-Dither + FXAA Fast (Recommended)",
        "De-Dither + FXAA High",
        "De-Dither Debug Heatmap",
    };
    const char* post_effect_values_dev[] = {
        "none",
        "fxaa",
        "fxaa_extreme",
        "dedither",
        "dedither_fxaa",
        "dedither_fxaa_extreme",
        "dedither_debug",
    };
    const char* post_effect_labels_std[] = {
        "Off (Native)",
        "FXAA 3.11 Default",
        "FXAA 3.11 Quality",
        "De-Dither + FXAA Fast (Recommended)",
        "De-Dither + FXAA High",
    };
    const char* post_effect_values_std[] = {
        "none",
        "fxaa",
        "fxaa_extreme",
        "dedither_fxaa",
        "dedither_fxaa_extreme",
    };

    const char* const* post_effect_labels = dev_mode ? post_effect_labels_dev : post_effect_labels_std;
    const char* const* post_effect_values = dev_mode ? post_effect_values_dev : post_effect_values_std;
    const int post_effect_count = dev_mode ? IM_ARRAYSIZE(post_effect_values_dev) : IM_ARRAYSIZE(post_effect_values_std);

    int post_idx = dev_mode ? 4 : 3;  // Default fallback to dedither_fxaa
    for (int i = 0; i < post_effect_count; ++i) {
      if (current_post == post_effect_values[i]) {
        post_idx = i;
        break;
      }
    }

    DrawControlLabel("Anti-Aliasing");
    ImGui::SetNextItemWidth(ControlWidth());
    if (ImGui::Combo("##SwapPostEffect", &post_idx, post_effect_labels, post_effect_count)) {
      rex::cvar::SetFlagByName("swap_post_effect", post_effect_values[post_idx]);
    }

    const std::string active_post = rex::cvar::GetFlagByName("swap_post_effect");
    const bool uses_dedither = (active_post.rfind("dedither", 0) == 0);
    if (uses_dedither) {
      ImGui::Spacing();
      double current_strength = rex::cvar::Query<double>("swap_dedither_strength");
      float slider_strength = static_cast<float>(current_strength);
      DrawControlLabel("De-Dither Intensity");
      ImGui::SetNextItemWidth(ControlWidth(360.0f));
      if (ImGui::SliderFloat("##DeDitherStrength", &slider_strength, 0.0f, 1.0f, "%.2f")) {
        char str_buf[32];
        snprintf(str_buf, sizeof(str_buf), "%.2f", slider_strength);
        rex::cvar::SetFlagByName("swap_dedither_strength", str_buf);
      }
      DrawHintText("Controls foliage cross-blend strength (0.00 = off, 1.00 = max smoothing).");
    }

    ImGui::Spacing();
    {
      std::string cur_a2m = rex::cvar::GetFlagByName("majin_foliage_alpha_mode");
      if (cur_a2m.empty()) cur_a2m = "solid";
      static const char* kA2mLabels[] = {
          "Solid Cutout (0.50 - Zero Dither)",
          "Smooth Fade (Anti-Hole + Camera Fade)",
          "Lush Cutout (0.25 - Fuller)",
          "Original 360 (Bayer 4x4 Dithered)",
      };
      static const char* kA2mValues[] = {"solid", "smooth", "lush", "original"};
      int a2m_idx = 0;
      for (int i = 0; i < 4; ++i) {
        if (cur_a2m == kA2mValues[i]) {
          a2m_idx = i;
          break;
        }
      }
      DrawControlLabel("Foliage Alpha Cutout (Anti-Holes)");
      ImGui::SetNextItemWidth(ControlWidth());
      if (ImGui::Combo("##FoliageAlphaMode", &a2m_idx, kA2mLabels, 4)) {
        rex::cvar::SetFlagByName("majin_foliage_alpha_mode", kA2mValues[a2m_idx]);
      }
      DrawHintText("Overrides 360 Bayer 4x4 foliage/shadow dither & A2M holes. Smooth Fade keeps leaves solid (>=0.75) while preserving camera fade (hot-reload).");
    }
  }
  EndCard();

  // Card 2: Camera & World Visibility
  if (BeginCard("CardGraphicsVisuals", "CAMERA & WORLD", ICON_SOLAR_SLIDERS)) {
    // FOV
    float fov_scale = static_cast<float>(rex::cvar::Query<double>("majin_fov_scale"));
    DrawControlLabel("Field of View (FOV)");
    DrawExperimentalBadge();
    ImGui::SetNextItemWidth(ControlWidth(360.0f));
    if (ImGui::SliderFloat("##FovScale", &fov_scale, 0.80f, 1.50f, "%.2fx")) {
      char buf[32];
      snprintf(buf, sizeof(buf), "%.2f", fov_scale);
      rex::cvar::SetFlagByName("majin_fov_scale", buf);
    }
    DrawHintText("A wider FOV may cause parts of the environment to remain unrendered or appear missing (default: 1.00x).");

    ImGui::Spacing();

    // Draw Distance
    float draw_distance_scale =
        static_cast<float>(rex::cvar::Query<double>("majin_draw_distance_scale"));
    DrawControlLabel("Draw Distance");
    ImGui::SetNextItemWidth(ControlWidth(360.0f));
    if (ImGui::SliderFloat("##DrawDistanceScale", &draw_distance_scale, 0.50f, 3.00f, "%.2fx")) {
      char buf[32];
      snprintf(buf, sizeof(buf), "%.2f", draw_distance_scale);
      rex::cvar::SetFlagByName("majin_draw_distance_scale", buf);
    }
    DrawHintText("Scales far clip planes and object draw distance (default: 1.00x).");
  }
  EndCard();

  // Card 3: Visual Effects
  if (BeginCard("CardGraphicsPost", "VISUAL EFFECTS", ICON_SOLAR_ATMOSPHERE)) {
    // Depth of Field Intensity & Mode
    float dof_intensity = static_cast<float>(rex::cvar::Query<double>("majin_dof"));
    DrawControlLabel("Depth of Field");
    ImGui::SetNextItemWidth(ControlWidth(360.0f));
    const char* dof_fmt = (dof_intensity <= 0.001f) ? "Off" : "%.2f";
    if (ImGui::SliderFloat("##DofIntensity", &dof_intensity, 0.0f, 2.0f, dof_fmt)) {
      char buf[32];
      snprintf(buf, sizeof(buf), "%.2f", dof_intensity);
      rex::cvar::SetFlagByName("majin_dof", buf);
    }
    if (!rex::cvar::GetFlagByName("majin_dof_mode").empty()) {
      ImGui::Spacing();
      std::string cur_dof_mode = rex::cvar::Query<std::string>("majin_dof_mode");
      int dof_mode_idx = (cur_dof_mode == "native") ? 1 : 0;
      const char* dof_mode_labels[] = {"Optical Bokeh (Recommended)", "Native (Original 360)"};
      DrawControlLabel("Depth of Field Style");
      ImGui::SetNextItemWidth(ControlWidth(360.0f));
      if (ImGui::Combo("##DofMode", &dof_mode_idx, dof_mode_labels, IM_ARRAYSIZE(dof_mode_labels))) {
        rex::cvar::SetFlagByName("majin_dof_mode", dof_mode_idx == 1 ? "native" : "bokeh");
      }
    }
    DrawHintText("Controls optical lens blur intensity (0.00 = Off, 0.40 = default, 2.00 = strong bokeh). Optical Bokeh removes black dither dots and foreground halos.");

    ImGui::Spacing();

    // Bloom Intensity
    float bloom_intensity = static_cast<float>(rex::cvar::Query<double>("majin_bloom"));
    DrawControlLabel("Bloom Intensity");
    ImGui::SetNextItemWidth(ControlWidth(360.0f));
    const char* bloom_fmt = (bloom_intensity <= 0.001f) ? "Off" : "%.2f";
    if (ImGui::SliderFloat("##BloomIntensity", &bloom_intensity, 0.0f, 2.0f, bloom_fmt)) {
      char buf[32];
      snprintf(buf, sizeof(buf), "%.2f", bloom_intensity);
      rex::cvar::SetFlagByName("majin_bloom", buf);
    }
    DrawHintText("Controls glare and specular light intensity (0.00 = disabled, 0.80 = default, 2.00 = 2x glow).");

    ImGui::Spacing();

    // Fog Intensity
    float fog_intensity = static_cast<float>(rex::cvar::Query<double>("majin_fog"));
    DrawControlLabel("Fog Intensity");
    ImGui::SetNextItemWidth(ControlWidth(360.0f));
    const char* fog_fmt = (fog_intensity <= 0.001f) ? "Off" : "%.2f";
    if (ImGui::SliderFloat("##FogIntensity", &fog_intensity, 0.0f, 1.0f, fog_fmt)) {
      char buf[32];
      snprintf(buf, sizeof(buf), "%.2f", fog_intensity);
      rex::cvar::SetFlagByName("majin_fog", buf);
    }
    DrawHintText("Controls scene fog density and distance (0.00 = Off, 0.80 = default, 1.00 = original).");

    ImGui::Spacing();

    // Pause Background
    bool pause_distortion = rex::cvar::Query<bool>("majin_pause_distortion");
    if (ImGui::Checkbox("Pause Background Effect", &pause_distortion)) {
      rex::cvar::SetFlagByName("majin_pause_distortion", pause_distortion ? "true" : "false");
    }
  }
  EndCard();
}

void InGameMenuDialog::DrawShadowTab() {
  const std::string active_backend = rex::cvar::GetFlagByName("gpu_backend");
  const bool is_vulkan = (active_backend == "vulkan");

  DrawTabPageHeader(ICON_SOLAR_SHADOW, "Shadow",
                    "Dynamic shadow map resolution, sub-texel PCF filtering, stability, and depth bias.");

  // Card 1: Shadow Quality
  if (BeginCard("CardShadowQuality", "SHADOW QUALITY", ICON_SOLAR_SHADOW)) {
    const int32_t current_shadow_scale = rex::cvar::Query<int32_t>("majin_shadow_scale");
    const char* shadow_scale_labels[] = {
        "1x - Native",
        "2x - Enhanced",
        "4x - Ultra Clean",
    };
    const int shadow_scale_values[] = {1, 2, 4};

    int shadow_idx = 0;
    if (current_shadow_scale >= 4) {
      shadow_idx = 2;
    } else if (current_shadow_scale >= 2) {
      shadow_idx = 1;
    } else {
      shadow_idx = 0;
    }

    DrawControlLabel("Shadow Resolution Scale");
    ImGui::SetNextItemWidth(ControlWidth(330.0f));
    if (ImGui::Combo("##MajinShadowScale", &shadow_idx, shadow_scale_labels, IM_ARRAYSIZE(shadow_scale_labels))) {
      const int new_val = shadow_scale_values[shadow_idx];
      rex::cvar::SetFlagByName("majin_shadow_scale", std::to_string(new_val));
      if (on_clear_caches_) {
        on_clear_caches_();
      }
    }
    ImGui::SameLine();
    if (DrawDefaultResetButton("DefShadowScale", shadow_idx == 1, "Reset to default (2x - Enhanced)")) {
      rex::cvar::SetFlagByName("majin_shadow_scale", "2");
      if (on_clear_caches_) {
        on_clear_caches_();
      }
    }
    DrawHintText("Scales shadow map resolution dynamically (1x = native 360, 2x = enhanced, 4x = ultra clean).");
  }
  EndCard();

  // Card 2: Shadow Filtering & Stability
  if (BeginCard("CardShadowStability", "SHADOW FILTERING & STABILITY", ICON_SOLAR_SLIDERS)) {
    {
      ImGui::BeginDisabled(!is_vulkan);
      std::string cur_pcf = rex::cvar::GetFlagByName("majin_shadow_pcf_mode");
      if (cur_pcf.empty()) cur_pcf = "soft";
      static const char* kPcfLabels[] = {
          "4x4 Cubic PCF (Recommended - Smooth, Zero Striping)",
          "2x2 Bilinear PCF (Sharp, Smooth Sun Motion)",
          "6x6 Tent PCF (Ultra Soft Penumbra)",
          "Original 360 (1-Tap Binary)",
      };
      static const char* kPcfValues[] = {"soft", "bilinear", "ultra", "original"};
      int pcf_idx = 0;
      for (int i = 0; i < 4; ++i) {
        if (cur_pcf == kPcfValues[i]) {
          pcf_idx = i;
          break;
        }
      }
      DrawControlLabel("Shadow Filtering (PCF)");
      if (!is_vulkan) {
        DrawVulkanOnlyBadge();
      }
      ImGui::SetNextItemWidth(ControlWidth(330.0f));
      if (ImGui::Combo("##ShadowPcfMode", &pcf_idx, kPcfLabels, 4)) {
        rex::cvar::SetFlagByName("majin_shadow_pcf_mode", kPcfValues[pcf_idx]);
      }
      ImGui::SameLine();
      if (DrawDefaultResetButton("DefShadowPcf", pcf_idx == 0, "Reset to default (4x4 Cubic PCF)")) {
        rex::cvar::SetFlagByName("majin_shadow_pcf_mode", "soft");
      }
      DrawHintText("Sub-texel shadow filtering in KbPreZSSSM* shaders. Eliminates stone-wall striping and makes sunlight move smoothly at 60 FPS (hot-reload).");

      ImGui::Spacing();

      bool rpb_enabled = true;
      if (!rex::cvar::GetFlagByName("majin_shadow_receiver_bias").empty()) {
        rpb_enabled = rex::cvar::Query<bool>("majin_shadow_receiver_bias");
      }
      if (ImGui::Checkbox("Receiver Plane Depth Bias (Anti-Striping)", &rpb_enabled)) {
        rex::cvar::SetFlagByName("majin_shadow_receiver_bias", rpb_enabled ? "true" : "false");
      }
      ImGui::SameLine(0.0f, 10.0f);
      if (DrawDefaultResetButton("DefShadowRpb", rpb_enabled, "Reset to default (On)")) {
        rex::cvar::SetFlagByName("majin_shadow_receiver_bias", "true");
      }
      DrawHintText("Computes per-texel tangent-plane depth slope in PCF to remove grazing stone-wall stripes without Peter-Panning (hot-reload).");

      ImGui::Spacing();

      bool two_sided_shadows = false;
      if (!rex::cvar::GetFlagByName("majin_shadow_two_sided").empty()) {
        two_sided_shadows = rex::cvar::Query<bool>("majin_shadow_two_sided");
      }
      if (ImGui::Checkbox("Two-Sided Shadow Casters (Experimental)", &two_sided_shadows)) {
        rex::cvar::SetFlagByName("majin_shadow_two_sided", two_sided_shadows ? "true" : "false");
      }
      ImGui::SameLine(0.0f, 10.0f);
      if (DrawDefaultResetButton("DefShadowTwoSided", !two_sided_shadows, "Reset to default (Off)")) {
        rex::cvar::SetFlagByName("majin_shadow_two_sided", "false");
      }
      DrawHintText("Default Off (native 360 front-face culling): prevents extra polygonal shadows on stone walls and ground. Enabling seals hollow rock bases at the cost of single-sided shell artifacts (hot-reload).");
      ImGui::EndDisabled();
    }

    ImGui::Spacing();

    // Cascade Texel Snapping (CPU/PPC - works on Vulkan & D3D12)
    {
      bool texel_snap = true;
      if (!rex::cvar::GetFlagByName("majin_shadow_texel_snap").empty()) {
        texel_snap = rex::cvar::Query<bool>("majin_shadow_texel_snap");
      }
      if (ImGui::Checkbox("Cascade Texel Snapping (Anti-Shimmer)", &texel_snap)) {
        rex::cvar::SetFlagByName("majin_shadow_texel_snap", texel_snap ? "true" : "false");
      }
      ImGui::SameLine(0.0f, 10.0f);
      if (DrawDefaultResetButton("DefShadowTexelSnap", texel_snap, "Reset to default (On)")) {
        rex::cvar::SetFlagByName("majin_shadow_texel_snap", "true");
      }
      DrawHintText("Snaps orthographic shadow cascade bounds to shadow-map texel increments so edges stay stable during camera movement (hot-reload).");
    }

    ImGui::Spacing();

    // Screen-Space Shadow Blur (SSSM) Integration
    {
      std::string cur_sssm = rex::cvar::GetFlagByName("majin_shadow_sssm_mode");
      if (cur_sssm.empty()) cur_sssm = "auto";
      static const char* kSssmLabels[] = {
          "Auto (Off for 4x4/6x6 PCF, 0.5x for 2x2, Full for Original)",
          "Original (Full SSSM Blur)",
          "Subtle (0.5x SSSM Blur)",
          "Off (Pure 3D Shadow Map / PCF)",
      };
      static const char* kSssmValues[] = {"auto", "original", "subtle", "off"};
      int sssm_idx = 0;
      for (int i = 0; i < 4; ++i) {
        if (cur_sssm == kSssmValues[i]) {
          sssm_idx = i;
          break;
        }
      }
      DrawControlLabel("Screen-Space Shadow Blur (SSSM)");
      ImGui::SetNextItemWidth(ControlWidth(330.0f));
      if (ImGui::Combo("##ShadowSssmMode", &sssm_idx, kSssmLabels, 4)) {
        rex::cvar::SetFlagByName("majin_shadow_sssm_mode", kSssmValues[sssm_idx]);
      }
      ImGui::SameLine();
      if (DrawDefaultResetButton("DefShadowSssm", sssm_idx == 0, "Reset to default (Auto)")) {
        rex::cvar::SetFlagByName("majin_shadow_sssm_mode", "auto");
      }
      DrawHintText("Coordinates screen-space shadow blur with 3D PCF so flat floors and steep walls share uniform shadow softness (hot-reload).");
    }
  }
  EndCard();

  // Card 3: Shadow Depth Bias
  if (BeginCard("CardShadowBias", "SHADOW DEPTH BIAS", ICON_SOLAR_SLIDERS)) {
    if (!is_vulkan) {
      DrawPillBadge("Off - Vulkan Only", kColAmberBg, kColAmberBdr, kColAmberText);
      DrawHintText("Shadow Depth Bias is Off on DirectX 12. Switch Render Path to Vulkan in the launcher to adjust these settings.");
      ImGui::Spacing();
    }

    ImGui::BeginDisabled(!is_vulkan);

    // Shadow Slope-Scaled Depth Bias
    double current_slope_bias = 0.00;
    const std::string slope_bias_str = rex::cvar::GetFlagByName("majin_shadow_slope_bias");
    if (!slope_bias_str.empty()) {
      current_slope_bias = rex::cvar::Query<double>("majin_shadow_slope_bias");
    }
    float slope_val = is_vulkan ? static_cast<float>(current_slope_bias) : 0.0f;
    DrawControlLabel("Shadow Slope-Scaled Depth Bias");
    ImGui::SetNextItemWidth(ControlWidth(260.0f));
    if (ImGui::SliderFloat("##ShadowSlopeBias", &slope_val, -2.0f, 3.0f, is_vulkan ? "%.2f" : "Off")) {
      char buf[32];
      snprintf(buf, sizeof(buf), "%.2f", slope_val);
      rex::cvar::SetFlagByName("majin_shadow_slope_bias", buf);
    }
    ImGui::SameLine();
    if (ImGui::Button("-0.05##SlopeMinus")) {
      slope_val = std::max(-2.0f, slope_val - 0.05f);
      char buf[32];
      snprintf(buf, sizeof(buf), "%.2f", slope_val);
      rex::cvar::SetFlagByName("majin_shadow_slope_bias", buf);
    }
    ImGui::SameLine();
    if (ImGui::Button("+0.05##SlopePlus")) {
      slope_val = std::min(3.0f, slope_val + 0.05f);
      char buf[32];
      snprintf(buf, sizeof(buf), "%.2f", slope_val);
      rex::cvar::SetFlagByName("majin_shadow_slope_bias", buf);
    }
    ImGui::SameLine();
    if (DrawDefaultResetButton("DefShadowSlopeBias", std::abs(slope_val) < 0.005f, "Reset to default (0.00)")) {
      rex::cvar::SetFlagByName("majin_shadow_slope_bias", "0.00");
    }
    DrawHintText("Signed slope bias for shadow passes (default: 0.00). With Two-Sided Off, positive bias is isolated to static world geometry so characters and foliage stay clean.");

    ImGui::Spacing();

    // Shadow Constant Depth Bias
    double current_const_bias = 0.000;
    const std::string const_bias_str = rex::cvar::GetFlagByName("majin_shadow_constant_bias");
    if (!const_bias_str.empty()) {
      current_const_bias = rex::cvar::Query<double>("majin_shadow_constant_bias");
    }
    float const_val = is_vulkan ? static_cast<float>(current_const_bias) : 0.0f;
    DrawControlLabel("Shadow Constant Depth Bias");
    ImGui::SetNextItemWidth(ControlWidth(260.0f));
    if (ImGui::SliderFloat("##ShadowConstBias", &const_val, -0.050f, 0.050f, is_vulkan ? "%.3f" : "Off")) {
      char buf[32];
      snprintf(buf, sizeof(buf), "%.3f", const_val);
      rex::cvar::SetFlagByName("majin_shadow_constant_bias", buf);
    }
    ImGui::SameLine();
    if (ImGui::Button("-0.001##ConstMinus")) {
      const_val = std::max(-0.050f, const_val - 0.001f);
      char buf[32];
      snprintf(buf, sizeof(buf), "%.3f", const_val);
      rex::cvar::SetFlagByName("majin_shadow_constant_bias", buf);
    }
    ImGui::SameLine();
    if (ImGui::Button("+0.001##ConstPlus")) {
      const_val = std::min(0.050f, const_val + 0.001f);
      char buf[32];
      snprintf(buf, sizeof(buf), "%.3f", const_val);
      rex::cvar::SetFlagByName("majin_shadow_constant_bias", buf);
    }
    ImGui::SameLine();
    if (DrawDefaultResetButton("DefShadowConstBias", std::abs(const_val) < 0.0005f, "Reset to default (0.000)")) {
      rex::cvar::SetFlagByName("majin_shadow_constant_bias", "0.000");
    }
    DrawHintText("Fine signed constant offset added to shadow depth passes (default: 0.000; isolated to static world geometry when positive with Two-Sided Off).");

    ImGui::Spacing();

    // Shadow Depth Bias Clamp
    double current_bias_clamp = 0.000;
    const std::string bias_clamp_str = rex::cvar::GetFlagByName("majin_shadow_bias_clamp");
    if (!bias_clamp_str.empty()) {
      current_bias_clamp = rex::cvar::Query<double>("majin_shadow_bias_clamp");
    }
    float clamp_val = is_vulkan ? static_cast<float>(current_bias_clamp) : 0.0f;
    DrawControlLabel("Shadow Depth Bias Clamp");
    ImGui::SetNextItemWidth(ControlWidth(260.0f));
    if (ImGui::SliderFloat("##ShadowBiasClamp", &clamp_val, 0.000f, 0.020f, is_vulkan ? "%.3f" : "Off")) {
      char buf[32];
      snprintf(buf, sizeof(buf), "%.3f", clamp_val);
      rex::cvar::SetFlagByName("majin_shadow_bias_clamp", buf);
    }
    ImGui::SameLine();
    if (ImGui::Button("-0.001##ClampMinus")) {
      clamp_val = std::max(0.000f, clamp_val - 0.001f);
      char buf[32];
      snprintf(buf, sizeof(buf), "%.3f", clamp_val);
      rex::cvar::SetFlagByName("majin_shadow_bias_clamp", buf);
    }
    ImGui::SameLine();
    if (ImGui::Button("+0.001##ClampPlus")) {
      clamp_val = std::min(0.020f, clamp_val + 0.001f);
      char buf[32];
      snprintf(buf, sizeof(buf), "%.3f", clamp_val);
      rex::cvar::SetFlagByName("majin_shadow_bias_clamp", buf);
    }
    ImGui::SameLine();
    if (DrawDefaultResetButton("DefShadowBiasClamp", std::abs(clamp_val) < 0.0005f, "Reset to default (0.000)")) {
      rex::cvar::SetFlagByName("majin_shadow_bias_clamp", "0.000");
    }
    DrawHintText("Caps maximum slope bias magnitude on silhouette edges; sign automatically matches bias sign (default: 0.000).");

    ImGui::Spacing();

    // Shadow Bias Preset Buttons
    if (ImGui::Button("Zero Bias (Default)", ImVec2(158.0f, 30.0f))) {
      rex::cvar::SetFlagByName("majin_shadow_slope_bias", "0.00");
      rex::cvar::SetFlagByName("majin_shadow_constant_bias", "0.000");
      rex::cvar::SetFlagByName("majin_shadow_bias_clamp", "0.000");
    }
    ImGui::SameLine();
    if (ImGui::Button("Positive (+0.30)", ImVec2(138.0f, 30.0f))) {
      rex::cvar::SetFlagByName("majin_shadow_slope_bias", "0.30");
      rex::cvar::SetFlagByName("majin_shadow_constant_bias", "0.000");
      rex::cvar::SetFlagByName("majin_shadow_bias_clamp", "0.002");
    }
    ImGui::SameLine();
    if (ImGui::Button("Reversed-Z (-0.30)", ImVec2(154.0f, 30.0f))) {
      rex::cvar::SetFlagByName("majin_shadow_slope_bias", "-0.30");
      rex::cvar::SetFlagByName("majin_shadow_constant_bias", "0.000");
      rex::cvar::SetFlagByName("majin_shadow_bias_clamp", "0.002");
    }

    ImGui::EndDisabled();
  }
  EndCard();
}

void InGameMenuDialog::DrawAtmosphereTab() {
  DrawTabPageHeader(ICON_SOLAR_ATMOSPHERE, "Atmosphere",
                    "Player-attached atmospheric particle emitters and volumetric sun-ray screen filters.");

  // Card 1: Player-Attached Ambient VFX
  if (BeginCard("CardAtmoTepeuVfx", "PLAYER-ATTACHED AMBIENT VFX", ICON_SOLAR_ATMOSPHERE)) {
    bool tepeu_ambient_vfx = true;
    if (!rex::cvar::GetFlagByName("majin_tepeu_ambient_vfx").empty()) {
      tepeu_ambient_vfx = rex::cvar::Query<bool>("majin_tepeu_ambient_vfx");
    }
    if (ImGui::Checkbox("Ambient Dust & Smoke Around Tepeu", &tepeu_ambient_vfx)) {
      rex::cvar::SetFlagByName("majin_tepeu_ambient_vfx", tepeu_ambient_vfx ? "true" : "false");
    }
    DrawHintText(
        "Toggles area-specific dust, smoke, ash, embers, and heat-haze particle emitters attached "
        "to Tepeu (Areas 1, 3, 4 & 5). Area 2 rain and wet-surface effects remain unaffected (hot-reload).");
  }
  EndCard();

  // Card 2: Volumetric Screen-Space Light
  if (BeginCard("CardAtmoTyndall", "VOLUMETRIC SCREEN-SPACE LIGHT", ICON_SOLAR_SHADOW)) {
    bool tyndall_enabled = true;
    if (!rex::cvar::GetFlagByName("majin_tyndall").empty()) {
      tyndall_enabled = rex::cvar::Query<bool>("majin_tyndall");
    }
    if (ImGui::Checkbox("Sun God-Rays (Tyndall360)", &tyndall_enabled)) {
      rex::cvar::SetFlagByName("majin_tyndall", tyndall_enabled ? "true" : "false");
    }
    DrawHintText(
        "Toggles the full-screen additive volumetric sun-ray haze filter (filterTyndall360) "
        "that sweeps across the screen as the camera turns toward the sun (hot-reload).");
  }
  EndCard();
}

void InGameMenuDialog::DrawControlsTab() {
  DrawTabPageHeader(ICON_SOLAR_GAMEPAD, "Controls",
                    "Mouse and keyboard camera input adjustments.");

  if (BeginCard("CardControlsInput", "INPUT", ICON_SOLAR_GAMEPAD)) {
    float mouse_sens = static_cast<float>(rex::cvar::Query<double>("mnk_sensitivity"));
    DrawControlLabel("Mouse Sensitivity");
    DrawExperimentalBadge();
    ImGui::SetNextItemWidth(ControlWidth(360.0f));
    if (ImGui::SliderFloat("##MouseSens", &mouse_sens, 0.1f, 10.0f, "%.1fx")) {
      char buf[32];
      snprintf(buf, sizeof(buf), "%.2f", mouse_sens);
      rex::cvar::SetFlagByName("mnk_sensitivity", buf);
    }
    DrawHintText("Camera look sensitivity when playing with Keyboard & Mouse.");
  }
  EndCard();
}

void InGameMenuDialog::DrawFixesTab() {
  DrawTabPageHeader(ICON_SOLAR_SHIELD, "Fixes & Patches",
                    "High-FPS gameplay physics and animation timing normalizations.");

  if (BeginCard("CardFixesPhysics", "PHYSICS", ICON_SOLAR_SHIELD)) {
    bool step_fix = rex::cvar::Query<bool>("majin_step_fix");
    if (ImGui::Checkbox("High FPS Edge Climbing (Step Fix)", &step_fix)) {
      rex::cvar::SetFlagByName("majin_step_fix", step_fix ? "true" : "false");
    }
    DrawExperimentalBadge();
    DrawHintText(
        "Scales movement step when blocked on small edges for >80 FPS. Keep disabled if you feel your "
        "character being pushed away or repelled when close to walls, enemies, or Majin at 30-60 FPS.");

    ImGui::Spacing();

    bool dt_smooth = rex::cvar::Query<bool>("majin_dt_smooth");
    if (ImGui::Checkbox("Engine Delta-Time Smoothing (dt_smooth)", &dt_smooth)) {
      rex::cvar::SetFlagByName("majin_dt_smooth", dt_smooth ? "true" : "false");
    }
    DrawHintText(
        "Replaces guest frame delta-time with a smoothed moving average. Leave off for purest "
        "vanilla console physics pacing.");
  }
  EndCard();

  if (BeginCard("CardFixesAnimation", "ANIMATION", ICON_SOLAR_SLIDERS)) {
    bool submotion_fix = rex::cvar::Query<bool>("majin_submotion_fix");
    if (ImGui::Checkbox("Sub-Motion Animation Normalization", &submotion_fix)) {
      rex::cvar::SetFlagByName("majin_submotion_fix", submotion_fix ? "true" : "false");
    }
    DrawExperimentalBadge();
    DrawHintText(
        "Normalizes facial expressions and world object animations above 30 FPS. Keep disabled if "
        "you experience interaction timing or dialogue delay issues with Majin.");
  }
  EndCard();
}

void InGameMenuDialog::DrawPreferencesTab() {
  DrawTabPageHeader(ICON_SOLAR_SETTINGS, "Preferences",
                    "Cutscene playback speed, developer diagnostics, and active session details.");

  // Card 1: Cutscene Preferences
  if (BeginCard("CardPrefsGame", "CUTSCENE PLAYBACK", ICON_SOLAR_SETTINGS)) {
    int32_t cutscene_speed = rex::cvar::Query<int32_t>("majin_cutscene_speed");
    const char* speed_items[] = {"1x (Normal Speed)", "2x Speed", "4x Fast-Forward", "8x Hyper Speed"};
    int current_speed_idx = (cutscene_speed == 1) ? 0 : (cutscene_speed == 2) ? 1 : (cutscene_speed == 8) ? 3 : 2;

    DrawControlLabel("Cutscene Speed");
    ImGui::SetNextItemWidth(ControlWidth(300.0f));
    if (ImGui::Combo("##CutsceneSpeed", &current_speed_idx, speed_items, IM_ARRAYSIZE(speed_items))) {
      const int new_speed = (current_speed_idx == 0) ? 1 : (current_speed_idx == 1) ? 2 : (current_speed_idx == 3) ? 8 : 4;
      rex::cvar::SetFlagByName("majin_cutscene_speed", std::to_string(new_speed));
    }

    ImGui::Spacing();

    bool skip_cutscenes = rex::cvar::Query<bool>("majin_skip_cutscenes");
    if (ImGui::Checkbox("Allow Fast-Forwarding In-Game Cutscenes", &skip_cutscenes)) {
      rex::cvar::SetFlagByName("majin_skip_cutscenes", skip_cutscenes ? "true" : "false");
    }
    DrawExperimentalBadge();
    DrawHintText("Hold [Select/Back] or [Tab] to speed through cutscenes.");
  }
  EndCard();

  // Card 2: Developer Mode
  if (BeginCard("CardPrefsDevMode", "DEVELOPER MODE", ICON_SOLAR_CODE)) {
    const bool dev_mode = rex::cvar::Query<bool>("majin_dev_mode");
    const auto& fonts = GetUiFonts();

    if (dev_mode) {
      DrawPillBadge("ENABLED", kColAmberBg, kColAmberBdr, kColAmberText);
    } else {
      DrawPillBadge("DISABLED", kColDarkControl, kColDarkBorder, kColTextMuted);
    }

    ImGui::Spacing();
    PushFontSafe(fonts.semibold);
    if (dev_mode) {
      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.36f, 0.08f, 1.0f));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.70f, 0.46f, 0.12f, 1.0f));
      if (ImGui::Button(ICON_SOLAR_CODE "  Disable Developer Mode", ImVec2(246.0f, 32.0f))) {
        rex::cvar::SetFlagByName("majin_dev_mode", "false");
        const std::string cur_post = rex::cvar::GetFlagByName("swap_post_effect");
        if (cur_post == "dedither" || cur_post == "dedither_debug") {
          rex::cvar::SetFlagByName("swap_post_effect", "dedither_fxaa");
        }
        if (GetEffectiveFpsLimit() == 0) {
          ApplyLiveFpsLimit(60);
        }
      }
      ImGui::PopStyleColor(2);
    } else {
      if (ImGui::Button(ICON_SOLAR_CODE "  Enable Developer Mode", ImVec2(246.0f, 32.0f))) {
        rex::cvar::SetFlagByName("majin_dev_mode", "true");
      }
    }
    PopFontSafe(fonts.semibold);

    DrawHintText(
        "Reveal hidden experimental and diagnostic options in the in-game menu "
        "(Uncapped FPS, De-Dither debug modes, and Window Title FPS). Disabled by default.");
  }
  EndCard();

  // Card 3: Session Info
  if (BeginCard("CardPrefsSession", "SESSION INFO", ICON_SOLAR_INFO)) {
    const std::string backend = rex::cvar::GetFlagByName("gpu_backend");
    const std::string res_scale = rex::cvar::GetFlagByName("resolution_scale");
    const std::string upscaler = rex::cvar::GetFlagByName("present_effect");
    const std::string aa = rex::cvar::GetFlagByName("swap_post_effect");
    const int32_t active_shadow_scale = rex::cvar::Query<int32_t>("majin_shadow_scale");
    const int32_t active_fps = GetEffectiveFpsLimit();
    const bool is_vulkan = (backend == "vulkan");

    ImGui::BulletText("Render Path: %s%s", backend.empty() ? "d3d12" : backend.c_str(),
                      is_vulkan ? "" : " (Deprecated - Dev Mode)");
    ImGui::BulletText("Render Scale: %sx (%s)", res_scale.empty() ? "1" : res_scale.c_str(),
                      is_vulkan ? "Live hot-reload" : "Requires Vulkan for live switching");
    ImGui::BulletText("Frame Rate Limit: %s (Live hot-reload)",
                      active_fps == 0 ? "Uncapped" : (active_fps > 30 ? "60 FPS" : "30 FPS"));
    ImGui::BulletText("Shadow Quality: %dx (Live dynamic scaling)", active_shadow_scale <= 1 ? 1 : (active_shadow_scale >= 4 ? 4 : 2));
    ImGui::BulletText("Upscaler: %s", upscaler.empty() ? "bilinear" : upscaler.c_str());
    ImGui::BulletText("Anti-Aliasing: %s", aa.empty() ? "none" : aa.c_str());
  }
  EndCard();
}

void InGameMenuDialog::DrawQuitConfirmModal() {
  if (show_quit_confirm_) {
    ImGui::OpenPopup("Quit to Desktop?###QuitConfirmModal");
  }

  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20.0f, 18.0f));
  ImGui::PushStyleColor(ImGuiCol_PopupBg, kColDarkCard);

  if (ImGui::BeginPopupModal("Quit to Desktop?###QuitConfirmModal", &show_quit_confirm_,
                             ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
    const auto& fonts = GetUiFonts();
    PushFontSafe(fonts.semibold);
    ImGui::TextUnformatted("Are you sure you want to quit the game?");
    PopFontSafe(fonts.semibold);
    DrawHintText("Any unsaved progress since your last checkpoint will be lost.");
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    PushFontSafe(fonts.semibold);
    ImGui::PushStyleColor(ImGuiCol_Button, kColRoseBtn);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kColRoseBtnHov);
    ImGui::PushStyleColor(ImGuiCol_Border, kColRoseBdr);
    if (ImGui::Button(ICON_SOLAR_LOGOUT "  Yes, Quit Game", ImVec2(160.0f, 32.0f))) {
      show_quit_confirm_ = false;
      ImGui::CloseCurrentPopup();
      if (on_quit_) on_quit_();
    }
    ImGui::PopStyleColor(3);

    ImGui::SameLine(0.0f, 10.0f);

    if (ImGui::Button("Cancel", ImVec2(106.0f, 32.0f))) {
      show_quit_confirm_ = false;
      ImGui::CloseCurrentPopup();
    }
    PopFontSafe(fonts.semibold);

    ImGui::EndPopup();
  }

  ImGui::PopStyleColor();
  ImGui::PopStyleVar();
}

}  // namespace majin
