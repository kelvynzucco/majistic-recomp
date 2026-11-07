// Majistic Recomp - switches for the game's post effects
// Adapted from ArthurMS15/MajinRecomp
//
// SysKemumaki (the post effect system) draws its passes from one function
// (sub_821A00E0), each behind the "enabled" flag of the frame's parameters
// (r28): +0x418 the pause screen blur (sub_821A1530), +0x448 depth of field
// (sub_821A4028), +0x3A4 glare and bloom (sub_821A2C10). Each hook runs between
// the flag's load and its test, and clears it when the effect is switched off.

#include <rex/cvar.h>
#include <rex/ppc/context.h>

REXCVAR_DEFINE_BOOL(majin_pause_blur, true, "Majin", "Blur the game behind the pause menu");
REXCVAR_DEFINE_BOOL(majin_dof, true, "Majin", "Depth of field (background blur)");
REXCVAR_DEFINE_BOOL(majin_bloom, true, "Majin", "Glare and bloom around bright light");
REXCVAR_DEFINE_BOOL(majin_force_pause_blur, false, "Majin",
                    "Draw the pause screen blur all the time (testing)");

void Majin_PauseBlurEnabled(PPCRegister& r11) {
  if (!REXCVAR_GET(majin_pause_blur)) r11.u64 = 0;
  else if (REXCVAR_GET(majin_force_pause_blur)) r11.u64 = 1;
}

void Majin_DofEnabled(PPCRegister& r11) {
  if (!REXCVAR_GET(majin_dof)) r11.u64 = 0;
}

void Majin_BloomEnabled(PPCRegister& r11) {
  if (!REXCVAR_GET(majin_bloom)) r11.u64 = 0;
}
