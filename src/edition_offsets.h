// Majistic Recomp - Unified Edition Offsets and Symbol Mappings
// Bridges American (USA 0.0.0.2, Media ID 76FDB642) and European (PAL 0.0.0.3, Media ID 67BD87EF)
#pragma once

#include <cstdint>

namespace majin::edition {
inline constexpr uint32_t kTitleId = 0x4E4D0816;
inline constexpr uint32_t kMediaIdUsa = 0x76FDB642;
inline constexpr uint32_t kMediaIdPal = 0x67BD87EF;
}  // namespace majin::edition

#if defined(MAJIN_EDITION_PAL)
// ============================================================================
// European / PAL (0.0.0.3, Media ID 67BD87EF)
// Functions >= 0x821C0000 are shifted by +0x70 relative to USA.
// ============================================================================

#define MAJIN_EDITION_NAME "Europe / PAL (0.0.0.3)"

// Timing and Idle Reset
#define MAJIN_FN_TITLE_UPDATE sub_821CC390
#define MAJIN_IMP_TITLE_UPDATE __imp__sub_821CC390

// Intro Logo Skip
#define MAJIN_FN_TITLE_LOGO sub_821CEB68
#define MAJIN_IMP_TITLE_LOGO __imp__sub_821CEB68
#define MAJIN_FN_SCALEFORM_INVOKE sub_823D4B80
#define MAJIN_IMP_SCALEFORM_INVOKE __imp__sub_823D4B80

// D3D9 Present Telemetry
#define MAJIN_FN_D3D_PRESENT sub_824757B8
#define MAJIN_IMP_D3D_PRESENT __imp__sub_824757B8

// Save / Content Diagnostics
#define MAJIN_ADDR_CONTENT_CREATE_EX 8267AFE0
#define MAJIN_ADDR_CONTENT_CLOSE 8267B070
#define MAJIN_ADDR_CONTENT_SET_THUMBNAIL 8267B078
#define MAJIN_ADDR_GET_OVERLAPPED_RESULT 8267B510
#define MAJIN_FN_WRITE_FILE sub_8267B6C8
#define MAJIN_IMP_WRITE_FILE __imp__sub_8267B6C8
#define MAJIN_FN_GET_LAST_ERROR sub_8267B190
#define MAJIN_IMP_GET_LAST_ERROR __imp__sub_8267B190
#define MAJIN_FN_SAVE_FLOW_A sub_8236FC60
#define MAJIN_IMP_SAVE_FLOW_A __imp__sub_8236FC60
#define MAJIN_FN_SAVE_FLOW_B sub_822E91F8
#define MAJIN_IMP_SAVE_FLOW_B __imp__sub_822E91F8
#define MAJIN_FN_SAVE_FLOW_C sub_826D3C20
#define MAJIN_IMP_SAVE_FLOW_C __imp__sub_826D3C20

namespace majin::offsets {
// Scenario root pointer in .data is identical across both USA and PAL editions (0x82BA73D0).
// In PPC asm: lis r31, -32070 (0x82BA0000) -> lwz r11, 29648(r31) (0x82BA73D0).
inline constexpr uint32_t kScenarioRootPtr = 0x82BA73D0;
inline constexpr uint32_t kLogoEndFlagCallLR = 0x821CECE0;
}  // namespace majin::offsets

#else
// ============================================================================
// American / NTSC-U (0.0.0.2, Media ID 76FDB642)
// Base reference layout.
// ============================================================================

#define MAJIN_EDITION_NAME "USA / NTSC-U (0.0.0.2)"

// Timing and Idle Reset
#define MAJIN_FN_TITLE_UPDATE sub_821CC320
#define MAJIN_IMP_TITLE_UPDATE __imp__sub_821CC320

// Intro Logo Skip
#define MAJIN_FN_TITLE_LOGO sub_821CEAF8
#define MAJIN_IMP_TITLE_LOGO __imp__sub_821CEAF8
#define MAJIN_FN_SCALEFORM_INVOKE sub_823D4B10
#define MAJIN_IMP_SCALEFORM_INVOKE __imp__sub_823D4B10

// D3D9 Present Telemetry
#define MAJIN_FN_D3D_PRESENT sub_82475748
#define MAJIN_IMP_D3D_PRESENT __imp__sub_82475748

// Save / Content Diagnostics
#define MAJIN_ADDR_CONTENT_CREATE_EX 8267AF70
#define MAJIN_ADDR_CONTENT_CLOSE 8267B000
#define MAJIN_ADDR_CONTENT_SET_THUMBNAIL 8267B008
#define MAJIN_ADDR_GET_OVERLAPPED_RESULT 8267B4A0
#define MAJIN_FN_WRITE_FILE sub_8267B658
#define MAJIN_IMP_WRITE_FILE __imp__sub_8267B658
#define MAJIN_FN_GET_LAST_ERROR sub_8267B120
#define MAJIN_IMP_GET_LAST_ERROR __imp__sub_8267B120
#define MAJIN_FN_SAVE_FLOW_A sub_8236FBF0
#define MAJIN_IMP_SAVE_FLOW_A __imp__sub_8236FBF0
#define MAJIN_FN_SAVE_FLOW_B sub_822E9188
#define MAJIN_IMP_SAVE_FLOW_B __imp__sub_822E9188
#define MAJIN_FN_SAVE_FLOW_C sub_826D3BB0
#define MAJIN_IMP_SAVE_FLOW_C __imp__sub_826D3BB0

namespace majin::offsets {
inline constexpr uint32_t kScenarioRootPtr = 0x82BA73D0;
inline constexpr uint32_t kLogoEndFlagCallLR = 0x821CEC70;
}  // namespace majin::offsets

#endif

namespace majin::offsets {
// Offsets below 0x821C0000 and .rdata are identical across both editions:
inline constexpr uint32_t kGuestTrueString = 0x820EB20C;
inline constexpr uint32_t kTitleIdleVtable = 0x820EC3C4;
inline constexpr uint32_t kTitleLogoVtable = 0x820EC274;

struct EditionOffsets {
  const char* edition_name;
  uint32_t scenario_root;
  uint32_t logo_end_flag_call_lr;
  uint32_t guest_true_string;
  uint32_t title_idle_vtable;
};

inline EditionOffsets GetActiveOffsets() {
  return {
      MAJIN_EDITION_NAME,
      kScenarioRootPtr,
      kLogoEndFlagCallLR,
      kGuestTrueString,
      kTitleIdleVtable,
  };
}
}  // namespace majin::offsets
