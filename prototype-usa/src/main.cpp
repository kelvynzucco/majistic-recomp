// majin - ReXGlue Recompiled Project

#include "generated/default/majin_init.h"

#include "majin_app.h"

REXCVAR_DEFINE_BOOL(majin_record_performance, true, "Diagnostics",
                    "Enable local guest-frame CSV recording (no automatic upload)");

REXCVAR_DEFINE_BOOL(majin_window_fps, false, "Diagnostics",
                    "Show live guest FPS in the window title only");

REXCVAR_DEFINE_STRING(gpu_backend, "d3d12", "GPU",
                      "GPU backend on Windows: vulkan or d3d12")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

REX_DEFINE_APP(majin, MajinApp::Create)
