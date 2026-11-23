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
  const bool dof = rex::cvar::Query<bool>("majin_dof");
  const double bloom = rex::cvar::Query<double>("majin_bloom");
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
  char bloom_buf[32];
  snprintf(bloom_buf, sizeof(bloom_buf), "%.2f", bloom);
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
  ReplaceJsonField(json, "DepthOfField", dof ? "true" : "false");
  ReplaceJsonField(json, "Bloom", bloom > 0.0 ? "true" : "false");
  ReplaceJsonField(json, "BloomIntensity", bloom_buf);
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
    if (ImGui::BeginMenuBar()) {
      ImGui::TextColored(ImVec4(0.22f, 0.74f, 0.97f, 1.0f), "MAJISTIC RECOMP");
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

    // Tab Bar for categorized configuration
    if (ImGui::BeginTabBar("MajinInGameTabs", ImGuiTabBarFlags_None)) {
      if (ImGui::BeginTabItem("Gameplay & Controls")) {
        DrawGameplayTab();
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Visuals & Graphics")) {
        DrawVisualsTab();
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Fixes & Patches")) {
        DrawFixesTab();
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("System & Info")) {
        DrawSystemTab();
        ImGui::EndTabItem();
      }
      ImGui::EndTabBar();
    }

    ImGui::Spacing();
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

void InGameMenuDialog::DrawGameplayTab() {
  ImGui::Spacing();

  // Mouse Look Sensitivity
  float mouse_sens = static_cast<float>(rex::cvar::Query<double>("mnk_sensitivity"));
  ImGui::Text("Mouse Look Sensitivity:");
  ImGui::SetNextItemWidth(320.0f);
  if (ImGui::SliderFloat("##MouseSens", &mouse_sens, 0.1f, 5.0f, "%.1fx")) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.2f", mouse_sens);
    rex::cvar::SetFlagByName("mnk_sensitivity", buf);
  }
  ImGui::TextDisabled("Adjusts raw mouse look responsiveness for right analog stick camera.");

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  // In-game cutscene controls
  bool skip_cutscenes = rex::cvar::Query<bool>("majin_skip_cutscenes");
  if (ImGui::Checkbox("Allow Fast-Forwarding In-Game Cutscenes", &skip_cutscenes)) {
    rex::cvar::SetFlagByName("majin_skip_cutscenes", skip_cutscenes ? "true" : "false");
  }
  ImGui::TextDisabled("Hold [Select/Back] on controller or [Tab/Backspace] on keyboard to speed through cutscenes.");

  ImGui::Spacing();

  int32_t cutscene_speed = rex::cvar::Query<int32_t>("majin_cutscene_speed");
  const char* speed_items[] = {"1x (Normal Speed)", "2x Speed", "4x Fast-Forward", "8x Hyper Speed"};
  int current_speed_idx = (cutscene_speed == 1) ? 0 : (cutscene_speed == 2) ? 1 : (cutscene_speed == 8) ? 3 : 2;

  ImGui::Text("Cutscene Fast-Forward Multiplier:");
  ImGui::SetNextItemWidth(260.0f);
  if (ImGui::Combo("##CutsceneSpeed", &current_speed_idx, speed_items, IM_ARRAYSIZE(speed_items))) {
    const int new_speed = (current_speed_idx == 0) ? 1 : (current_speed_idx == 1) ? 2 : (current_speed_idx == 3) ? 8 : 4;
    rex::cvar::SetFlagByName("majin_cutscene_speed", std::to_string(new_speed));
  }
}

void InGameMenuDialog::DrawVisualsTab() {
  ImGui::Spacing();

  // Anti-Aliasing & De-Dither (swap_post_effect)
  const std::string current_post = rex::cvar::GetFlagByName("swap_post_effect");
  const char* post_effect_labels[] = {
      "Off (Native Raw)",
      "FXAA Fast (FXAA 3.11 Default)",
      "FXAA High (FXAA 3.11 Quality)",
      "De-Dither (Edge-Preserving Cross Filter)",
      "De-Dither + FXAA Fast (Recommended)",
      "De-Dither + FXAA High",
      "De-Dither Debug Heatmap (Magenta = Dither Detected)",
  };
  const char* post_effect_values[] = {
      "none",
      "fxaa",
      "fxaa_extreme",
      "dedither",
      "dedither_fxaa",
      "dedither_fxaa_extreme",
      "dedither_debug",
  };

  int post_idx = 0;
  for (int i = 0; i < IM_ARRAYSIZE(post_effect_values); ++i) {
    if (current_post == post_effect_values[i]) {
      post_idx = i;
      break;
    }
  }

  ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.90f, 1.0f), "Anti-Aliasing & De-Dither Pass (swap_post_effect):");
  ImGui::SetNextItemWidth(400.0f);
  if (ImGui::Combo("##SwapPostEffect", &post_idx, post_effect_labels, IM_ARRAYSIZE(post_effect_labels))) {
    rex::cvar::SetFlagByName("swap_post_effect", post_effect_values[post_idx]);
  }
  ImGui::TextDisabled("Removes checkerboard dither transparency and smooths geometric aliasing in real-time.");

  const bool uses_dedither = (post_idx >= 3 && post_idx <= 6);
  if (uses_dedither) {
    ImGui::Spacing();
    double current_strength = rex::cvar::Query<double>("swap_dedither_strength");
    float slider_strength = static_cast<float>(current_strength);
    ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.90f, 1.0f), "De-Dither Intensity / Strength (swap_dedither_strength):");
    ImGui::SetNextItemWidth(400.0f);
    if (ImGui::SliderFloat("##DeDitherStrength", &slider_strength, 0.0f, 1.0f, "%.2f")) {
      char str_buf[32];
      snprintf(str_buf, sizeof(str_buf), "%.2f", slider_strength);
      rex::cvar::SetFlagByName("swap_dedither_strength", str_buf);
    }
    ImGui::TextDisabled("Controls cross-blend strength (0.00 = off, 1.00 = maximum foliage smoothing).");
  }

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  // Presenter Upscaler (present_effect)
  const std::string current_upscaler = rex::cvar::GetFlagByName("present_effect");
  const char* upscaler_labels[] = {
      "Bilinear (Direct Blit / Smooth Downsample)",
      "AMD FidelityFX Super Resolution 1.0 (FSR)",
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

  ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.90f, 1.0f), "Presenter Upscaler & Scaling Filter (present_effect):");
  ImGui::SetNextItemWidth(400.0f);
  if (ImGui::Combo("##PresentEffect", &upscaler_idx, upscaler_labels, IM_ARRAYSIZE(upscaler_labels))) {
    rex::cvar::SetFlagByName("present_effect", upscaler_values[upscaler_idx]);
  }
  ImGui::TextDisabled("Bilinear blit is automatically enforced when downsampling to avoid sharpening dither patterns.");

  // Sharpening controls based on active upscaler
  if (current_upscaler == "cas") {
    float cas_sharp = static_cast<float>(rex::cvar::Query<double>("present_cas_additional_sharpness"));
    ImGui::Text("CAS Additional Sharpness:");
    ImGui::SetNextItemWidth(320.0f);
    if (ImGui::SliderFloat("##CasSharpness", &cas_sharp, 0.0f, 1.0f, "%.2f")) {
      char buf[32];
      snprintf(buf, sizeof(buf), "%.2f", cas_sharp);
      rex::cvar::SetFlagByName("present_cas_additional_sharpness", buf);
    }
    ImGui::TextDisabled("Adjusts Contrast Adaptive Sharpening intensity (0.00 = default, 1.00 = max).");
  } else if (current_upscaler == "fsr") {
    float fsr_reduction = static_cast<float>(rex::cvar::Query<double>("present_fsr_sharpness_reduction"));
    ImGui::Text("FSR RCAS Sharpness Reduction:");
    ImGui::SetNextItemWidth(320.0f);
    if (ImGui::SliderFloat("##FsrSharpnessReduction", &fsr_reduction, 0.0f, 2.0f, "%.2f stops")) {
      char buf[32];
      snprintf(buf, sizeof(buf), "%.2f", fsr_reduction);
      rex::cvar::SetFlagByName("present_fsr_sharpness_reduction", buf);
    }
    ImGui::TextDisabled("0.00 = Maximum sharpness; higher values reduce RCAS sharpening strength.");
  }

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  // Field of View
  float fov_scale = static_cast<float>(rex::cvar::Query<double>("majin_fov_scale"));
  ImGui::Text("Camera Field of View (FOV Multiplier):");
  ImGui::SetNextItemWidth(320.0f);
  if (ImGui::SliderFloat("##FovScale", &fov_scale, 0.80f, 1.50f, "%.2fx")) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.2f", fov_scale);
    rex::cvar::SetFlagByName("majin_fov_scale", buf);
  }
  ImGui::TextDisabled("Dynamically widens view frustum and projection culling (default: 1.00x).");

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  // Draw Distance Multiplier
  float draw_distance_scale =
      static_cast<float>(rex::cvar::Query<double>("majin_draw_distance_scale"));
  ImGui::Text("Draw Distance & LOD Multiplier:");
  ImGui::SetNextItemWidth(320.0f);
  if (ImGui::SliderFloat("##DrawDistanceScale", &draw_distance_scale, 0.50f, 3.00f, "%.2fx")) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.2f", draw_distance_scale);
    rex::cvar::SetFlagByName("majin_draw_distance_scale", buf);
  }
  ImGui::TextDisabled("Scales far clip planes and object draw/fade distances (default: 1.00x).");

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.90f, 1.0f), "In-Engine Shaders & Effects:");

  // Depth of field
  bool dof = rex::cvar::Query<bool>("majin_dof");
  if (ImGui::Checkbox("Depth of Field (Background Distance Blur)", &dof)) {
    rex::cvar::SetFlagByName("majin_dof", dof ? "true" : "false");
  }

  // Specular Bloom & Glare Slider
  float bloom_intensity = static_cast<float>(rex::cvar::Query<double>("majin_bloom"));
  ImGui::SetNextItemWidth(320.0f);
  if (ImGui::SliderFloat("Glare & Specular Bloom", &bloom_intensity, 0.0f, 2.0f, "%.2f")) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.2f", bloom_intensity);
    rex::cvar::SetFlagByName("majin_bloom", buf);
  }

  // Pause & Map Wave Distortion
  bool pause_distortion = rex::cvar::Query<bool>("majin_pause_distortion");
  if (ImGui::Checkbox("Pause & Map Screen Wave Distortion", &pause_distortion)) {
    rex::cvar::SetFlagByName("majin_pause_distortion", pause_distortion ? "true" : "false");
  }

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.90f, 1.0f), "Shadows & Lighting:");

  // Shadow Map Resolution Scale
  const int32_t current_shadow_scale = rex::cvar::Query<int32_t>("majin_shadow_scale");
  const char* shadow_scale_labels[] = {
      "1x Native (Vanilla 512-1024)",
      "2x Enhanced (Crisp 1024-2048)",
      "4x Ultra Clean (Sharp 2048-4096)",
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

  ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.90f, 1.0f), "Shadow Map Resolution Scale (majin_shadow_scale):");
  ImGui::SetNextItemWidth(340.0f);
  if (ImGui::Combo("##MajinShadowScale", &shadow_idx, shadow_scale_labels, IM_ARRAYSIZE(shadow_scale_labels))) {
    const int new_val = shadow_scale_values[shadow_idx];
    rex::cvar::SetFlagByName("majin_shadow_scale", std::to_string(new_val));
    if (on_clear_caches_) {
      on_clear_caches_();
    }
  }
  ImGui::TextDisabled("Scales depth-only shadow passes in real-time, eliminating staircasing/pixelation on characters and geometry.");

  ImGui::Spacing();

  // Shadow Map Slope-Scaled Depth Bias
  double current_slope_bias = 0.30;
  const std::string slope_bias_str = rex::cvar::GetFlagByName("majin_shadow_slope_bias");
  if (!slope_bias_str.empty()) {
    current_slope_bias = rex::cvar::Query<double>("majin_shadow_slope_bias");
  }
  float slope_val = static_cast<float>(current_slope_bias);
  ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.90f, 1.0f), "Shadow Slope-Scaled Depth Bias (majin_shadow_slope_bias):");
  ImGui::SetNextItemWidth(300.0f);
  if (ImGui::SliderFloat("##ShadowSlopeBias", &slope_val, 0.0f, 2.0f, "%.2f")) {
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
  ImGui::TextDisabled("Eliminates shadow acne/flickering on angled walls as sunlight rotates (default: 0.30).");

  ImGui::Spacing();

  // Shadow Map Constant Depth Bias
  double current_const_bias = 0.000;
  const std::string const_bias_str = rex::cvar::GetFlagByName("majin_shadow_constant_bias");
  if (!const_bias_str.empty()) {
    current_const_bias = rex::cvar::Query<double>("majin_shadow_constant_bias");
  }
  float const_val = static_cast<float>(current_const_bias);
  ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.90f, 1.0f), "Shadow Constant Depth Bias (majin_shadow_constant_bias):");
  ImGui::SetNextItemWidth(300.0f);
  if (ImGui::SliderFloat("##ShadowConstBias", &const_val, 0.000f, 0.050f, "%.3f")) {
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
  ImGui::TextDisabled("Fine constant offset added to shadow depth passes (default: 0.000).");

  ImGui::Spacing();

  // Shadow Map Depth Bias Clamp
  double current_bias_clamp = 0.002;
  const std::string bias_clamp_str = rex::cvar::GetFlagByName("majin_shadow_bias_clamp");
  if (!bias_clamp_str.empty()) {
    current_bias_clamp = rex::cvar::Query<double>("majin_shadow_bias_clamp");
  }
  float clamp_val = static_cast<float>(current_bias_clamp);
  ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.90f, 1.0f), "Shadow Depth Bias Clamp (majin_shadow_bias_clamp):");
  ImGui::SetNextItemWidth(300.0f);
  if (ImGui::SliderFloat("##ShadowBiasClamp", &clamp_val, 0.000f, 0.020f, "%.3f")) {
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
  ImGui::TextDisabled("Caps maximum slope bias on silhouette edges, eliminating glowing halos/glow (default: 0.002).");

  ImGui::Spacing();

  // Quick Action Buttons
  if (ImGui::Button("Reset Shadow Bias (0.30 / 0.000 / 0.002)", ImVec2(275, 26))) {
    rex::cvar::SetFlagByName("majin_shadow_slope_bias", "0.30");
    rex::cvar::SetFlagByName("majin_shadow_constant_bias", "0.000");
    rex::cvar::SetFlagByName("majin_shadow_bias_clamp", "0.002");
  }
  ImGui::SameLine();
  if (ImGui::Button("Zero Bias (Original) (0.00 / 0.000 / 0.000)", ImVec2(275, 26))) {
    rex::cvar::SetFlagByName("majin_shadow_slope_bias", "0.00");
    rex::cvar::SetFlagByName("majin_shadow_constant_bias", "0.000");
    rex::cvar::SetFlagByName("majin_shadow_bias_clamp", "0.000");
  }

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.90f, 1.0f), "Display & Swapchain:");

  // Output Dithering
  bool dither = rex::cvar::Query<bool>("present_dither");
  if (ImGui::Checkbox("Swapchain Color Dithering (present_dither)", &dither)) {
    rex::cvar::SetFlagByName("present_dither", dither ? "true" : "false");
  }
  ImGui::TextDisabled("Apply output dithering pass. Keep disabled to eliminate screen grain/noise.");

  // VSync
  bool vsync = rex::cvar::Query<bool>("vsync");
  if (ImGui::Checkbox("Vertical Synchronization (VSync)", &vsync)) {
    rex::cvar::SetFlagByName("vsync", vsync ? "true" : "false");
  }
}

void InGameMenuDialog::DrawFixesTab() {
  ImGui::Spacing();

  // Step Fix
  bool step_fix = rex::cvar::Query<bool>("majin_step_fix");
  if (ImGui::Checkbox("High FPS Edge Climbing (Step Fix)", &step_fix)) {
    rex::cvar::SetFlagByName("majin_step_fix", step_fix ? "true" : "false");
  }
  ImGui::TextDisabled("Prevents Tepeu and enemies from getting stuck against small curbs and ledges above 30 FPS.");

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  // Engine Delta-Time Smoothing
  bool dt_smooth = rex::cvar::Query<bool>("majin_dt_smooth");
  if (ImGui::Checkbox("Engine Delta-Time Smoothing (dt_smooth)", &dt_smooth)) {
    rex::cvar::SetFlagByName("majin_dt_smooth", dt_smooth ? "true" : "false");
  }
  ImGui::TextDisabled("Replaces guest frame delta-time with a smoothed moving average. Leave off for purest vanilla console physics pacing.");

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  // Sub-Motion Fix
  bool submotion_fix = rex::cvar::Query<bool>("majin_submotion_fix");
  if (ImGui::Checkbox("Sub-Motion Animation Normalization (Experimental)", &submotion_fix)) {
    rex::cvar::SetFlagByName("majin_submotion_fix", submotion_fix ? "true" : "false");
  }
  ImGui::TextDisabled("Normalizes partial skeleton animation steps (facial expressions, healing) to console rate.");
}

void InGameMenuDialog::DrawSystemTab() {
  ImGui::Spacing();

  ImGui::TextColored(ImVec4(0.35f, 0.82f, 1.0f, 1.0f), "Engine & Session Information");
  ImGui::Spacing();

  const std::string backend = rex::cvar::GetFlagByName("gpu_backend");
  const std::string res_scale = rex::cvar::GetFlagByName("resolution_scale");
  const std::string upscaler = rex::cvar::GetFlagByName("present_effect");
  const std::string aa = rex::cvar::GetFlagByName("swap_post_effect");
  const int32_t active_shadow_scale = rex::cvar::Query<int32_t>("majin_shadow_scale");

  ImGui::BulletText("Active Graphics Backend: %s", backend.empty() ? "vulkan" : backend.c_str());
  ImGui::BulletText("Internal Render Scale: %sx (Requires launcher restart to modify)", res_scale.empty() ? "1" : res_scale.c_str());
  ImGui::BulletText("Shadow Map Resolution Scale: %dx (Live dynamic scaling)", active_shadow_scale <= 1 ? 1 : (active_shadow_scale >= 4 ? 4 : 2));
  ImGui::BulletText("Presenter Upscaler: %s", upscaler.empty() ? "bilinear" : upscaler.c_str());
  ImGui::BulletText("Anti-Aliasing Pass: %s", aa.empty() ? "none" : aa.c_str());

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  // Window title FPS toggle
  bool show_fps = rex::cvar::Query<bool>("majin_window_fps");
  if (ImGui::Checkbox("Show Live Guest FPS in Window Title", &show_fps)) {
    rex::cvar::SetFlagByName("majin_window_fps", show_fps ? "true" : "false");
  }
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
