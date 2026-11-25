// SPDX-License-Identifier: GPL-3.0-only
// Majistic Recomp - In-Game Configuration & Settings Overlay Menu
// Clean Dear ImGui presentation layer for live in-game adjustments (Issue #10)

#include "in_game_menu.h"

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/input/mnk/mnk_input_driver.h>

#include <imgui.h>

#include <algorithm>
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

void DrawSectionHeader(const char* title) {
  ImGui::TextColored(ImVec4(0.35f, 0.82f, 1.0f, 1.0f), "%s", title);
  ImGui::Separator();
  ImGui::Spacing();
}

void DrawSubSectionHeader(const char* title) {
  ImGui::TextColored(ImVec4(0.65f, 0.72f, 0.82f, 1.0f), "%s", title);
  ImGui::Spacing();
}

void DrawExperimentalBadge() {
  ImGui::SameLine();
  ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.55f, 1.0f), "[Experimental]");
}

void DrawHintText(const char* text) {
  ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
  ImGui::TextWrapped("%s", text);
  ImGui::PopStyleColor();
}

}  // namespace

InGameMenuDialog::InGameMenuDialog(rex::ui::ImGuiDrawer* imgui_drawer,
                                   std::function<void()> on_resume,
                                   std::function<void()> on_quit,
                                   std::function<void()> on_clear_caches)
    : rex::ui::ImGuiDialog(imgui_drawer),
      on_resume_(std::move(on_resume)),
      on_quit_(std::move(on_quit)),
      on_clear_caches_(std::move(on_clear_caches)) {
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
  double shadow_slope_bias = 0.30;
  if (!rex::cvar::GetFlagByName("majin_shadow_slope_bias").empty()) {
    shadow_slope_bias = rex::cvar::Query<double>("majin_shadow_slope_bias");
  }
  double shadow_const_bias = 0.000;
  if (!rex::cvar::GetFlagByName("majin_shadow_constant_bias").empty()) {
    shadow_const_bias = rex::cvar::Query<double>("majin_shadow_constant_bias");
  }
  double shadow_bias_clamp = 0.002;
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

  ReplaceJsonField(json, "PostEffect", "\"" + post_effect + "\"");
  ReplaceJsonField(json, "DeDitherStrength", strength_buf);
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

void InGameMenuDialog::OnDraw(ImGuiIO& io) {
  if (save_status_timer_ > 0.0f) {
    save_status_timer_ -= io.DeltaTime;
    if (save_status_timer_ <= 0.0f) {
      save_status_msg_.clear();
    }
  }

  ImGui::SetNextWindowSize(ImVec2(660, 580), ImGuiCond_FirstUseEver);
  const ImVec2 center(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
  ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

  const ImGuiWindowFlags window_flags =
      ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_MenuBar;

  if (ImGui::Begin("Majistic Recomp - In-Game Settings###MajinInGameOverlay", nullptr, window_flags)) {
    const bool dev_mode = rex::cvar::Query<bool>("majin_dev_mode");
    if (ImGui::BeginMenuBar()) {
      ImGui::TextColored(ImVec4(0.22f, 0.74f, 0.97f, 1.0f), "MAJISTIC RECOMP");
      if (dev_mode) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.98f, 0.75f, 0.20f, 1.0f), "[DEV]");
      }
      ImGui::SameLine();
      ImGui::TextDisabled("|");
      ImGui::SameLine();
      ImGui::TextDisabled("In-Game Configuration Layer (Issue #10)");
      ImGui::EndMenuBar();
    }

    // Top Header
    ImGui::TextColored(ImVec4(0.35f, 0.82f, 1.0f, 1.0f), "Live Game Settings");
    ImGui::SameLine();
    ImGui::TextDisabled("(Changes apply immediately; press [F1] or [ESC] to resume)");
    ImGui::Separator();
    ImGui::Spacing();

    // Tab Bar for categorized configuration (standardized with Launcher)
    if (ImGui::BeginTabBar("MajinInGameTabs", ImGuiTabBarFlags_None)) {
      if (ImGui::BeginTabItem("Graphics")) {
        if (ImGui::BeginChild("GraphicsScroll", ImVec2(0, -44.0f), false)) {
          DrawGraphicsTab();
        }
        ImGui::EndChild();
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Controls")) {
        if (ImGui::BeginChild("ControlsScroll", ImVec2(0, -44.0f), false)) {
          DrawControlsTab();
        }
        ImGui::EndChild();
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Fixes & Patches")) {
        if (ImGui::BeginChild("FixesScroll", ImVec2(0, -44.0f), false)) {
          DrawFixesTab();
        }
        ImGui::EndChild();
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Game Preferences")) {
        if (ImGui::BeginChild("PreferencesScroll", ImVec2(0, -44.0f), false)) {
          DrawPreferencesTab();
        }
        ImGui::EndChild();
        ImGui::EndTabItem();
      }
      ImGui::EndTabBar();
    }

    ImGui::Separator();
    ImGui::Spacing();

    // Bottom Action Bar
    if (ImGui::Button("Save to Launcher Config", ImVec2(180, 28))) {
      if (SaveSettingsToLauncherConfig()) {
        save_status_msg_ = "Settings saved to launcher!";
        save_status_timer_ = 3.0f;
      } else {
        save_status_msg_ = "Could not save to config file.";
        save_status_timer_ = 3.0f;
      }
    }
    if (!save_status_msg_.empty()) {
      ImGui::SameLine();
      ImGui::TextColored(ImVec4(0.40f, 0.85f, 0.40f, 1.0f), "%s", save_status_msg_.c_str());
    }

    // Right-aligned Resume & Quit buttons
    const float button_width = 110.0f;
    const float right_pos = ImGui::GetWindowWidth() - (button_width * 2 + 25.0f);
    if (right_pos > ImGui::GetCursorPosX()) {
      ImGui::SameLine(right_pos);
    }

    // Resume button
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.45f, 0.65f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.18f, 0.60f, 0.85f, 1.0f));
    if (ImGui::Button("Resume", ImVec2(button_width, 28))) {
      if (on_resume_) on_resume_();
    }
    ImGui::PopStyleColor(2);

    ImGui::SameLine();

    // Quit button
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.60f, 0.15f, 0.15f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.80f, 0.20f, 0.20f, 1.0f));
    if (ImGui::Button("Quit Game", ImVec2(button_width, 28))) {
      show_quit_confirm_ = true;
    }
    ImGui::PopStyleColor(2);

    DrawQuitConfirmModal();
  }
  ImGui::End();
}

void InGameMenuDialog::DrawGraphicsTab() {
  ImGui::Spacing();
  DrawSectionHeader("DISPLAY");

  // Shadow Quality (majin_shadow_scale)
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

  ImGui::Text("Shadow Quality:");
  ImGui::SetNextItemWidth(360.0f);
  if (ImGui::Combo("##MajinShadowScale", &shadow_idx, shadow_scale_labels, IM_ARRAYSIZE(shadow_scale_labels))) {
    const int new_val = shadow_scale_values[shadow_idx];
    rex::cvar::SetFlagByName("majin_shadow_scale", std::to_string(new_val));
    if (on_clear_caches_) {
      on_clear_caches_();
    }
  }

  ImGui::Spacing();

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

  ImGui::Text("Upscaler:");
  ImGui::SetNextItemWidth(360.0f);
  if (ImGui::Combo("##PresentEffect", &upscaler_idx, upscaler_labels, IM_ARRAYSIZE(upscaler_labels))) {
    rex::cvar::SetFlagByName("present_effect", upscaler_values[upscaler_idx]);
  }

  // Sharpening controls based on active upscaler
  if (current_upscaler == "cas") {
    ImGui::Spacing();
    float cas_sharp = static_cast<float>(rex::cvar::Query<double>("present_cas_additional_sharpness"));
    ImGui::Text("CAS Sharp:");
    ImGui::SetNextItemWidth(320.0f);
    if (ImGui::SliderFloat("##CasSharpness", &cas_sharp, 0.0f, 1.0f, "%.2f")) {
      char buf[32];
      snprintf(buf, sizeof(buf), "%.2f", cas_sharp);
      rex::cvar::SetFlagByName("present_cas_additional_sharpness", buf);
    }
    DrawHintText("Contrast Adaptive Sharpening intensity (0.00 = default, 1.00 = max).");
  } else if (current_upscaler == "fsr") {
    ImGui::Spacing();
    float fsr_reduction = static_cast<float>(rex::cvar::Query<double>("present_fsr_sharpness_reduction"));
    ImGui::Text("FSR RCAS Sharp:");
    ImGui::SetNextItemWidth(320.0f);
    if (ImGui::SliderFloat("##FsrSharpnessReduction", &fsr_reduction, 0.0f, 2.0f, "%.2f stops")) {
      char buf[32];
      snprintf(buf, sizeof(buf), "%.2f", fsr_reduction);
      rex::cvar::SetFlagByName("present_fsr_sharpness_reduction", buf);
    }
    DrawHintText("0.00 = max sharpness; higher values reduce RCAS sharpening strength.");
  }

  ImGui::Spacing();

  const bool dev_mode = rex::cvar::Query<bool>("majin_dev_mode");
  const std::string active_backend = rex::cvar::GetFlagByName("gpu_backend");
  const bool is_vulkan = (active_backend == "vulkan");

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

  ImGui::Text("Anti-Aliasing:");
  ImGui::SetNextItemWidth(360.0f);
  if (ImGui::Combo("##SwapPostEffect", &post_idx, post_effect_labels, post_effect_count)) {
    rex::cvar::SetFlagByName("swap_post_effect", post_effect_values[post_idx]);
  }

  const std::string active_post = rex::cvar::GetFlagByName("swap_post_effect");
  const bool uses_dedither = (active_post.rfind("dedither", 0) == 0);
  if (uses_dedither) {
    ImGui::Spacing();
    double current_strength = rex::cvar::Query<double>("swap_dedither_strength");
    float slider_strength = static_cast<float>(current_strength);
    ImGui::Text("De-Dither Intensity:");
    ImGui::SetNextItemWidth(320.0f);
    if (ImGui::SliderFloat("##DeDitherStrength", &slider_strength, 0.0f, 1.0f, "%.2f")) {
      char str_buf[32];
      snprintf(str_buf, sizeof(str_buf), "%.2f", slider_strength);
      rex::cvar::SetFlagByName("swap_dedither_strength", str_buf);
    }
    DrawHintText("Controls foliage cross-blend strength (0.00 = off, 1.00 = max smoothing).");
  }

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  ImGui::TextColored(ImVec4(0.65f, 0.72f, 0.82f, 1.0f), "SHADOW DEPTH BIAS");
  if (!is_vulkan) {
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.95f, 0.65f, 0.25f, 1.0f), "[Off - Vulkan Only]");
  }
  ImGui::Spacing();
  if (!is_vulkan) {
    DrawHintText("Shadow Depth Bias is Off on DirectX 12. Switch Render Path to Vulkan in the launcher to adjust these settings.");
    ImGui::Spacing();
  }

  ImGui::BeginDisabled(!is_vulkan);

  // Shadow Slope-Scaled Depth Bias
  double current_slope_bias = 0.30;
  const std::string slope_bias_str = rex::cvar::GetFlagByName("majin_shadow_slope_bias");
  if (!slope_bias_str.empty()) {
    current_slope_bias = rex::cvar::Query<double>("majin_shadow_slope_bias");
  }
  float slope_val = is_vulkan ? static_cast<float>(current_slope_bias) : 0.0f;
  ImGui::Text("Shadow Slope-Scaled Depth Bias:");
  ImGui::SetNextItemWidth(320.0f);
  if (ImGui::SliderFloat("##ShadowSlopeBias", &slope_val, 0.0f, 3.0f, is_vulkan ? "%.2f" : "Off")) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.2f", slope_val);
    rex::cvar::SetFlagByName("majin_shadow_slope_bias", buf);
  }
  ImGui::SameLine();
  if (ImGui::Button("-0.05##SlopeMinus")) {
    slope_val = std::max(0.0f, slope_val - 0.05f);
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
  DrawHintText("Eliminates shadow acne/flickering on angled walls as sunlight rotates (default: 0.30).");

  ImGui::Spacing();

  // Shadow Constant Depth Bias
  double current_const_bias = 0.000;
  const std::string const_bias_str = rex::cvar::GetFlagByName("majin_shadow_constant_bias");
  if (!const_bias_str.empty()) {
    current_const_bias = rex::cvar::Query<double>("majin_shadow_constant_bias");
  }
  float const_val = is_vulkan ? static_cast<float>(current_const_bias) : 0.0f;
  ImGui::Text("Shadow Constant Depth Bias:");
  ImGui::SetNextItemWidth(320.0f);
  if (ImGui::SliderFloat("##ShadowConstBias", &const_val, 0.000f, 0.050f, is_vulkan ? "%.3f" : "Off")) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.3f", const_val);
    rex::cvar::SetFlagByName("majin_shadow_constant_bias", buf);
  }
  ImGui::SameLine();
  if (ImGui::Button("-0.001##ConstMinus")) {
    const_val = std::max(0.000f, const_val - 0.001f);
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
  DrawHintText("Fine constant offset added to shadow depth passes (default: 0.000).");

  ImGui::Spacing();

  // Shadow Depth Bias Clamp
  double current_bias_clamp = 0.002;
  const std::string bias_clamp_str = rex::cvar::GetFlagByName("majin_shadow_bias_clamp");
  if (!bias_clamp_str.empty()) {
    current_bias_clamp = rex::cvar::Query<double>("majin_shadow_bias_clamp");
  }
  float clamp_val = is_vulkan ? static_cast<float>(current_bias_clamp) : 0.0f;
  ImGui::Text("Shadow Depth Bias Clamp:");
  ImGui::SetNextItemWidth(320.0f);
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
  DrawHintText("Caps maximum slope bias on silhouette edges, eliminating glowing halos (default: 0.002).");

  ImGui::Spacing();

  // Shadow Bias Preset Buttons
  if (ImGui::Button("Reset Default (0.30 / 0.000 / 0.002)", ImVec2(260, 26))) {
    rex::cvar::SetFlagByName("majin_shadow_slope_bias", "0.30");
    rex::cvar::SetFlagByName("majin_shadow_constant_bias", "0.000");
    rex::cvar::SetFlagByName("majin_shadow_bias_clamp", "0.002");
  }
  ImGui::SameLine();
  if (ImGui::Button("Zero Bias (Original)", ImVec2(180, 26))) {
    rex::cvar::SetFlagByName("majin_shadow_slope_bias", "0.00");
    rex::cvar::SetFlagByName("majin_shadow_constant_bias", "0.000");
    rex::cvar::SetFlagByName("majin_shadow_bias_clamp", "0.000");
  }

  ImGui::EndDisabled();

  ImGui::Spacing();
  ImGui::Spacing();
  DrawSectionHeader("VISUALS");

  // FOV
  float fov_scale = static_cast<float>(rex::cvar::Query<double>("majin_fov_scale"));
  ImGui::Text("FOV:");
  DrawExperimentalBadge();
  ImGui::SetNextItemWidth(320.0f);
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
  ImGui::Text("Draw Distance:");
  ImGui::SetNextItemWidth(320.0f);
  if (ImGui::SliderFloat("##DrawDistanceScale", &draw_distance_scale, 0.50f, 3.00f, "%.2fx")) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.2f", draw_distance_scale);
    rex::cvar::SetFlagByName("majin_draw_distance_scale", buf);
  }
  DrawHintText("Scales far clip planes and object draw distance (default: 1.00x).");

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  DrawSubSectionHeader("VISUAL EFFECTS");

  // Depth of Field Intensity & Mode
  float dof_intensity = static_cast<float>(rex::cvar::Query<double>("majin_dof"));
  ImGui::Text("Depth of Field:");
  ImGui::SetNextItemWidth(320.0f);
  const char* dof_fmt = (dof_intensity <= 0.001f) ? "Off" : "%.2f";
  if (ImGui::SliderFloat("##DofIntensity", &dof_intensity, 0.0f, 2.0f, dof_fmt)) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.2f", dof_intensity);
    rex::cvar::SetFlagByName("majin_dof", buf);
  }
  if (!rex::cvar::GetFlagByName("majin_dof_mode").empty()) {
    std::string cur_dof_mode = rex::cvar::Query<std::string>("majin_dof_mode");
    int dof_mode_idx = (cur_dof_mode == "native") ? 1 : 0;
    const char* dof_mode_labels[] = {"Optical Bokeh (Recommended)", "Native (Original 360)"};
    ImGui::Text("Depth of Field Style:");
    ImGui::SetNextItemWidth(320.0f);
    if (ImGui::Combo("##DofMode", &dof_mode_idx, dof_mode_labels, IM_ARRAYSIZE(dof_mode_labels))) {
      rex::cvar::SetFlagByName("majin_dof_mode", dof_mode_idx == 1 ? "native" : "bokeh");
    }
  }
  DrawHintText("Controls optical lens blur intensity (0.00 = Off, 0.40 = default, 2.00 = strong bokeh). Optical Bokeh removes black dither dots and foreground halos.");

  ImGui::Spacing();

  // Pause Background
  bool pause_distortion = rex::cvar::Query<bool>("majin_pause_distortion");
  if (ImGui::Checkbox("Pause Background", &pause_distortion)) {
    rex::cvar::SetFlagByName("majin_pause_distortion", pause_distortion ? "true" : "false");
  }

  ImGui::Spacing();

  // Bloom Intensity
  float bloom_intensity = static_cast<float>(rex::cvar::Query<double>("majin_bloom"));
  ImGui::Text("Bloom Intensity:");
  ImGui::SetNextItemWidth(320.0f);
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
  ImGui::Text("Fog Intensity:");
  ImGui::SetNextItemWidth(320.0f);
  const char* fog_fmt = (fog_intensity <= 0.001f) ? "Off" : "%.2f";
  if (ImGui::SliderFloat("##FogIntensity", &fog_intensity, 0.0f, 1.0f, fog_fmt)) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.2f", fog_intensity);
    rex::cvar::SetFlagByName("majin_fog", buf);
  }
  DrawHintText("Controls scene fog density and distance (0.00 = Off, 0.80 = default, 1.00 = original).");

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  DrawSubSectionHeader("DISPLAY");

  // VSync
  bool vsync = rex::cvar::Query<bool>("vsync");
  if (ImGui::Checkbox("VSync", &vsync)) {
    rex::cvar::SetFlagByName("vsync", vsync ? "true" : "false");
  }

  // Output Dithering
  bool dither = rex::cvar::Query<bool>("present_dither");
  if (ImGui::Checkbox("Output Dithering", &dither)) {
    rex::cvar::SetFlagByName("present_dither", dither ? "true" : "false");
  }
  DrawHintText("Applies swapchain color dithering (present_dither). Disable to eliminate screen grain/noise.");

  // Window Title FPS (Dev Mode only, matching launcher)
  if (dev_mode) {
    ImGui::Spacing();
    bool show_fps = rex::cvar::Query<bool>("majin_window_fps");
    if (ImGui::Checkbox("Window Title FPS", &show_fps)) {
      rex::cvar::SetFlagByName("majin_window_fps", show_fps ? "true" : "false");
    }
    DrawHintText("Show Live Guest FPS in Window Title (majin_window_fps).");
  }
}

void InGameMenuDialog::DrawControlsTab() {
  ImGui::Spacing();
  DrawSectionHeader("INPUT");

  // Mouse Sensitivity
  float mouse_sens = static_cast<float>(rex::cvar::Query<double>("mnk_sensitivity"));
  ImGui::Text("Mouse Sensitivity:");
  DrawExperimentalBadge();
  ImGui::SetNextItemWidth(320.0f);
  if (ImGui::SliderFloat("##MouseSens", &mouse_sens, 0.1f, 10.0f, "%.1fx")) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.2f", mouse_sens);
    rex::cvar::SetFlagByName("mnk_sensitivity", buf);
  }
  DrawHintText("Camera look sensitivity when playing with Keyboard & Mouse.");
}

void InGameMenuDialog::DrawFixesTab() {
  ImGui::Spacing();
  DrawSectionHeader("PHYSICS");

  // Step Fix
  bool step_fix = rex::cvar::Query<bool>("majin_step_fix");
  if (ImGui::Checkbox("High FPS Edge Climbing (Step Fix)", &step_fix)) {
    rex::cvar::SetFlagByName("majin_step_fix", step_fix ? "true" : "false");
  }
  DrawExperimentalBadge();
  DrawHintText(
      "Scales movement step when blocked on small edges for >80 FPS. Keep disabled if you feel your "
      "character being pushed away or repelled when close to walls, enemies, or Majin at 30-60 FPS.");

  ImGui::Spacing();

  // Engine Delta-Time Smoothing
  bool dt_smooth = rex::cvar::Query<bool>("majin_dt_smooth");
  if (ImGui::Checkbox("Engine Delta-Time Smoothing (dt_smooth)", &dt_smooth)) {
    rex::cvar::SetFlagByName("majin_dt_smooth", dt_smooth ? "true" : "false");
  }
  DrawHintText(
      "Replaces guest frame delta-time with a smoothed moving average. Leave off for purest "
      "vanilla console physics pacing.");

  ImGui::Spacing();
  ImGui::Spacing();
  DrawSectionHeader("ANIMATION");

  // Sub-Motion Fix
  bool submotion_fix = rex::cvar::Query<bool>("majin_submotion_fix");
  if (ImGui::Checkbox("Sub-Motion Animation Normalization", &submotion_fix)) {
    rex::cvar::SetFlagByName("majin_submotion_fix", submotion_fix ? "true" : "false");
  }
  DrawExperimentalBadge();
  DrawHintText(
      "Normalizes facial expressions and world object animations above 30 FPS. Keep disabled if "
      "you experience interaction timing or dialogue delay issues with Majin.");
}

void InGameMenuDialog::DrawPreferencesTab() {
  ImGui::Spacing();
  DrawSectionHeader("GAME PREFERENCES");

  // Cutscene Speed
  int32_t cutscene_speed = rex::cvar::Query<int32_t>("majin_cutscene_speed");
  const char* speed_items[] = {"1x (Normal Speed)", "2x Speed", "4x Fast-Forward", "8x Hyper Speed"};
  int current_speed_idx = (cutscene_speed == 1) ? 0 : (cutscene_speed == 2) ? 1 : (cutscene_speed == 8) ? 3 : 2;

  ImGui::Text("Cutscene Speed:");
  ImGui::SetNextItemWidth(260.0f);
  if (ImGui::Combo("##CutsceneSpeed", &current_speed_idx, speed_items, IM_ARRAYSIZE(speed_items))) {
    const int new_speed = (current_speed_idx == 0) ? 1 : (current_speed_idx == 1) ? 2 : (current_speed_idx == 3) ? 8 : 4;
    rex::cvar::SetFlagByName("majin_cutscene_speed", std::to_string(new_speed));
  }

  ImGui::Spacing();

  // Allow Fast-Forwarding In-Game Cutscenes
  bool skip_cutscenes = rex::cvar::Query<bool>("majin_skip_cutscenes");
  if (ImGui::Checkbox("Allow Fast-Forwarding In-Game Cutscenes", &skip_cutscenes)) {
    rex::cvar::SetFlagByName("majin_skip_cutscenes", skip_cutscenes ? "true" : "false");
  }
  DrawExperimentalBadge();
  DrawHintText("Hold [Select/Back] or [Tab] to speed through cutscenes.");

  ImGui::Spacing();
  ImGui::Spacing();

  // Developer Mode Toggle (matching launcher Setup Game -> below Game Preferences)
  const bool dev_mode = rex::cvar::Query<bool>("majin_dev_mode");
  ImGui::TextColored(ImVec4(0.35f, 0.82f, 1.0f, 1.0f), "DEVELOPER MODE");
  ImGui::SameLine();
  if (dev_mode) {
    ImGui::TextColored(ImVec4(0.98f, 0.75f, 0.20f, 1.0f), "[ENABLED]");
  } else {
    ImGui::TextDisabled("[DISABLED]");
  }
  ImGui::Separator();
  ImGui::Spacing();

  if (dev_mode) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.36f, 0.08f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.70f, 0.46f, 0.12f, 1.0f));
    if (ImGui::Button("Disable Developer Mode", ImVec2(240, 28))) {
      rex::cvar::SetFlagByName("majin_dev_mode", "false");
      const std::string cur_post = rex::cvar::GetFlagByName("swap_post_effect");
      if (cur_post == "dedither" || cur_post == "dedither_debug") {
        rex::cvar::SetFlagByName("swap_post_effect", "dedither_fxaa");
      }
    }
    ImGui::PopStyleColor(2);
  } else {
    if (ImGui::Button("Enable Developer Mode", ImVec2(240, 28))) {
      rex::cvar::SetFlagByName("majin_dev_mode", "true");
    }
  }
  DrawHintText(
      "Reveal hidden experimental and diagnostic options in the in-game menu "
      "(De-Dither debug modes and Window Title FPS). Disabled by default.");

  ImGui::Spacing();
  ImGui::Spacing();
  DrawSectionHeader("SESSION INFO");

  const std::string backend = rex::cvar::GetFlagByName("gpu_backend");
  const std::string res_scale = rex::cvar::GetFlagByName("resolution_scale");
  const std::string upscaler = rex::cvar::GetFlagByName("present_effect");
  const std::string aa = rex::cvar::GetFlagByName("swap_post_effect");
  const int32_t active_shadow_scale = rex::cvar::Query<int32_t>("majin_shadow_scale");

  ImGui::BulletText("Render Path: %s", backend.empty() ? "d3d12" : backend.c_str());
  ImGui::BulletText("Render Scale: %sx (Requires launcher restart to modify)", res_scale.empty() ? "1" : res_scale.c_str());
  ImGui::BulletText("Shadow Quality: %dx (Live dynamic scaling)", active_shadow_scale <= 1 ? 1 : (active_shadow_scale >= 4 ? 4 : 2));
  ImGui::BulletText("Upscaler: %s", upscaler.empty() ? "bilinear" : upscaler.c_str());
  ImGui::BulletText("Anti-Aliasing: %s", aa.empty() ? "none" : aa.c_str());
}

void InGameMenuDialog::DrawQuitConfirmModal() {
  if (show_quit_confirm_) {
    ImGui::OpenPopup("Quit to Desktop?###QuitConfirmModal");
  }

  if (ImGui::BeginPopupModal("Quit to Desktop?###QuitConfirmModal", &show_quit_confirm_, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::Text("Are you sure you want to quit the game?");
    ImGui::TextDisabled("Any unsaved progress since your last checkpoint will be lost.");
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.70f, 0.15f, 0.15f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.85f, 0.20f, 0.20f, 1.0f));
    if (ImGui::Button("Yes, Quit Game", ImVec2(130, 28))) {
      show_quit_confirm_ = false;
      ImGui::CloseCurrentPopup();
      if (on_quit_) on_quit_();
    }
    ImGui::PopStyleColor(2);

    ImGui::SameLine();

    if (ImGui::Button("Cancel", ImVec2(100, 28))) {
      show_quit_confirm_ = false;
      ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
  }
}

}  // namespace majin
