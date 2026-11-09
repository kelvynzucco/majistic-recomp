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
  if (pos == std::string::npos) return;
  size_t val_start = json.find_first_not_of(" \t", pos + pattern.size());
  if (val_start == std::string::npos) return;
  size_t val_end = json.find_first_of(",\r\n}", val_start);
  if (val_end == std::string::npos) val_end = json.size();
  json.replace(val_start, val_end - val_start, new_val);
}

}  // namespace

InGameMenuDialog::InGameMenuDialog(rex::ui::ImGuiDrawer* imgui_drawer,
                                   std::function<void()> on_resume,
                                   std::function<void()> on_quit)
    : rex::ui::ImGuiDialog(imgui_drawer),
      on_resume_(std::move(on_resume)),
      on_quit_(std::move(on_quit)) {
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
  const double mouse_sens = rex::cvar::Query<double>("mnk_sensitivity");
  const double fov_scale = rex::cvar::Query<double>("majin_fov_scale");
  const bool dof = rex::cvar::Query<bool>("majin_dof");
  const bool bloom = rex::cvar::Query<bool>("majin_bloom");
  const bool pause_blur = rex::cvar::Query<bool>("majin_pause_blur");
  const bool dither = rex::cvar::Query<bool>("present_dither");
  const bool vsync = rex::cvar::Query<bool>("vsync");
  const bool step_fix = rex::cvar::Query<bool>("majin_step_fix");
  const bool anim_fix = rex::cvar::Query<bool>("majin_submotion_fix");
  const bool skip_cutscenes = rex::cvar::Query<bool>("majin_skip_cutscenes");
  const int32_t cutscene_speed = rex::cvar::Query<int32_t>("majin_cutscene_speed");

  char sens_buf[32];
  snprintf(sens_buf, sizeof(sens_buf), "%.1f", mouse_sens);
  char fov_buf[32];
  snprintf(fov_buf, sizeof(fov_buf), "%.2f", fov_scale);

  ReplaceJsonField(json, "MouseSensitivity", sens_buf);
  ReplaceJsonField(json, "FovScale", fov_buf);
  ReplaceJsonField(json, "DepthOfField", dof ? "true" : "false");
  ReplaceJsonField(json, "Bloom", bloom ? "true" : "false");
  ReplaceJsonField(json, "PauseBlur", pause_blur ? "true" : "false");
  ReplaceJsonField(json, "PresentDither", dither ? "true" : "false");
  ReplaceJsonField(json, "VSync", vsync ? "true" : "false");
  ReplaceJsonField(json, "StepFix", step_fix ? "true" : "false");
  ReplaceJsonField(json, "AnimSpeedFix", anim_fix ? "true" : "false");
  ReplaceJsonField(json, "SkipCutscenes", skip_cutscenes ? "true" : "false");
  ReplaceJsonField(json, "CutsceneSpeed", std::to_string(cutscene_speed));

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

  ImGui::SetNextWindowSize(ImVec2(640, 520), ImGuiCond_FirstUseEver);
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

  ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.90f, 1.0f), "Post-Processing Effects:");

  // Depth of field
  bool dof = rex::cvar::Query<bool>("majin_dof");
  if (ImGui::Checkbox("Depth of Field (Background Distance Blur)", &dof)) {
    rex::cvar::SetFlagByName("majin_dof", dof ? "true" : "false");
  }

  // Specular Bloom
  bool bloom = rex::cvar::Query<bool>("majin_bloom");
  if (ImGui::Checkbox("Glare & Specular Bloom", &bloom)) {
    rex::cvar::SetFlagByName("majin_bloom", bloom ? "true" : "false");
  }

  // Pause Blur
  bool pause_blur = rex::cvar::Query<bool>("majin_pause_blur");
  if (ImGui::Checkbox("Pause Screen Background Blur", &pause_blur)) {
    rex::cvar::SetFlagByName("majin_pause_blur", pause_blur ? "true" : "false");
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

  ImGui::BulletText("Active Graphics Backend: %s", backend.empty() ? "vulkan" : backend.c_str());
  ImGui::BulletText("Internal Render Scale: %sx (Requires launcher restart to modify)", res_scale.empty() ? "1" : res_scale.c_str());
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
