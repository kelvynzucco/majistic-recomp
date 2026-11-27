// SPDX-License-Identifier: GPL-3.0-only
// Majistic Recomp - In-Game Configuration & Settings Overlay Menu
// Clean Dear ImGui presentation layer for live in-game adjustments (Issue #10)

#pragma once

#include <rex/ui/imgui_dialog.h>
#include <rex/ui/imgui_drawer.h>

#include <functional>
#include <string>

namespace majin {

class InGameMenuDialog : public rex::ui::ImGuiDialog {
 public:
  InGameMenuDialog(rex::ui::ImGuiDrawer* imgui_drawer,
                   std::function<void()> on_resume,
                   std::function<void()> on_quit,
                   std::function<void()> on_clear_caches = nullptr);
  ~InGameMenuDialog() override;

 protected:
  void OnDraw(ImGuiIO& io) override;

 private:
  void DrawGraphicsTab();
  void DrawShadowTab();
  void DrawAtmosphereTab();
  void DrawControlsTab();
  void DrawFixesTab();
  void DrawPreferencesTab();
  void DrawQuitConfirmModal();

  bool SaveSettingsToLauncherConfig();

  std::function<void()> on_resume_;
  std::function<void()> on_quit_;
  std::function<void()> on_clear_caches_;

  bool show_quit_confirm_ = false;
  std::string save_status_msg_;
  float save_status_timer_ = 0.0f;
};

}  // namespace majin
