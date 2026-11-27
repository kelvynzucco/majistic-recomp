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
  enum class Tab : int {
    kGraphics = 0,
    kShadow,
    kAtmosphere,
    kControls,
    kFixes,
    kPreferences,
  };

  void DrawSidebar();
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

  Tab active_tab_ = Tab::kGraphics;
  bool show_quit_confirm_ = false;
  bool apply_windowed_rect_ = false;
  bool dragging_window_ = false;
  ImVec2 drag_offset_ = ImVec2(0.0f, 0.0f);
  std::string save_status_msg_;
  float save_status_timer_ = 0.0f;
};

}  // namespace majin
