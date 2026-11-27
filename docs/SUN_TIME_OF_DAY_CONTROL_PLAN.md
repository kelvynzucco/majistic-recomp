# Sun & Time-of-Day Control — Implementation Plan

> **Purpose:** Detailed technical blueprint to add live (hot-reload) **Sun / Time-of-Day** controls to **Majistic Recomp** (Freeze Sun, Time-of-Day `00:00`–`24:00` scrubber + presets, Solar Cycle Speed multiplier, and optional Free Azimuth/Elevation override) for repeatable lighting and shadow evaluation.
>
> **Status:** Reverse-engineering complete and verified on both **PAL (`0.0.0.3`)** and **USA (`0.0.0.2`)** binaries. Ready for direct implementation.

---

## 1. Executive Summary

*Majin and the Forsaken Kingdom* drives its entire outdoor lighting pipeline—sun/moon direction vector, real-time shadows, directional light color, day↔night transition, atmospheric scattering/sky dome, clouds, and the HUD sundial—from a single per-frame Sky/Solar controller function:

- **Function:** `sub_82189A88` (`prototype/generated/default/majin_recomp.125.cpp`, line 475)
- **Address range:** `0x82189A88` – `0x8218A428` (**`< 0x821C0000`**, confirmed **100% byte-identical** in both PAL and USA executables!)
- **Implementation footprint:**
  - **Zero ReXGlue SDK patches required** (100% game-side `midasm_hook` + CVars + Dear ImGui UI).
  - Single primary hook at `0x82189AF0` (`after_instruction = true`) inside `sub_82189A88`.
  - Optional Phase 2 hook at `0x82189FD4` (`after_instruction = false`) for arbitrary 360° Azimuth/Elevation sun rotation independent of the sky's astronomical path.

---

## 2. Reverse-Engineered Engine Architecture

### 2.1 Per-Frame Sky & Lighting Call Chain

Called once per frame from `prototype/generated/default/majin_recomp.38.cpp:737-770` with `r28` pointing to the active `SkySystem` singleton:

| Order | Function | Return PC | Role |
|------:|----------|-----------|------|
| 1 | `sub_821887E8(r28)` | `0x8218A948` | Pre-sky parameter preparation |
| 2 | **`sub_82189A88(r28)`** | `0x8218A950` | **Advances solar clock, computes astronomical sun/moon vector, updates directional light & day/night blend** |
| 3 | `sub_8218A428(r28)` | `0x8218A958` | Sky scattering, horizon, and ambient/fog colors derived from the updated sun state |
| 4 | `sub_82189788(r28)` | `0x8218A960` | Sky dome / sun & moon disc geometry |
| 5 | `sub_82188B00(r28)` | `0x8218A968` | Clouds / upper atmosphere |
| 6 | `sub_82188C90(r28)` | `0x8218A970` | Secondary sky/weather pass |
| 7 | `sub_821893C0(r28)` | `0x8218A978` | Final atmosphere / stars |

Because `sub_82189A88` runs **first** before all sky, scattering, and shadow passes (`sub_8218F0D0` / `sub_8218FE38`), modifying the solar clock at the top of `sub_82189A88` updates **every visual system in the same frame** with zero 1-frame lag or lighting/shadow mismatch.

### 2.2 `SkySystem` (`r31`) Memory Layout (Big-Endian Guest Memory)

Verified across `majin_recomp.125.cpp`, `majin_recomp.151.cpp`, and `majin_recomp.162.cpp`:

| Offset (`r31 + off`) | Type | Meaning | Notes |
|---:|---|---|---|
| `+100` (`0x0064`) | `uint32` | Per-frame reset flag | Cleared to `0` at `0x82189AF4` |
| `+104` (`0x0068`) | `uint32` | `is_night` flag | `0` = Sun (day), `1` = Moon (night) — written at `loc_8218A028` / `loc_8218A1D8` |
| `+108` (`0x006C`) | `float` | Sun/moon elevation intensity factor | Derived from `f24` (Y component of light dir) and `+3468` |
| `+3192` (`0x0C78`) | `uint32` | Skip directional light creation flag | Checked at `loc_82189C58` |
| `+3196` (`0x0C7C`) | `uint32` | `sync_day_of_year` flag | If non-zero, increments `+3200` on day wrap and adds fractional day `(+3208) * (1/period)` |
| **`+3200` (`0x0C80`)** | **`float`** | **`day_of_year`** | Seasonal sun declination (`[0, period_year)` where `period_year = *(float*)(r27 + 2796)`, wraps around year) |
| **`+3204` (`0x0C84`)** | **`uint32`** | **`clock_enabled`** | `0` = game paused clock (e.g., dungeons/scripted events), non-zero = clock advances |
| **`+3208` (`0x0C88`)** | **`float`** | **`time_of_day`** | Current time in `[0, day_period)` where `day_period = *(float*)(r27 + 1372)` |
| **`+3212` (`0x0C8C`)** | **`float`** | **`time_speed`** | Rate of change per second (`time_of_day += time_speed * dt`). **Overwritten every frame** by `majin_recomp.151.cpp:242` (`0x82168BBC`) based on day/night speed rules! |
| `+3272..3275` | `u8[4]` | Sun color (RGBA/ARGB bytes) | Used when `is_night == 0` |
| `+3276..3279` | `u8[4]` | Moon color (RGBA/ARGB bytes) | Used when `is_night == 1` |
| `+3280` (`0x0CD0`) | `float` | Directional light intensity scale | Multiplied into final RGB |
| `+3344` (`0x0D10`) | `float` | Horizon/dawn-dusk blend factor | Computed from sun elevation `f30` |
| `+3348` (`0x0D14`) | `float` | Solar elevation scalar | Used for day/night color ramp |
| `+3352` (`0x0D18`) | `uint32` | Active night/moon state copy | Mirror of `r30` (`0` day / `1` night) |
| `+3356` (`0x0D1C`) | `float` | Smoothed night transition `[0..1]` | Exponentially lerped toward `0` (day) or `1` (night) each frame |

### 2.3 Constant Table (`r27 = 0x820E2BA0`)

Computed at `0x82189AE4`–`0x82189AF0`:
```asm
lis  r11, -32242        ; 0x820E0000
addi r27, r11, 11168    ; r27 = 0x820E2BA0
```

| Offset (`r27 + off`) | Guest Address | Meaning | Usage in `sub_82189A88` |
|---:|---|---|---|
| `+44` | `0x820E2BCC` | `inv_day_period` (`1.0 / day_period`) | `f30`: converts `+3208` to fractional day `[0, 1)` added to `floor(+3200)` |
| `+1372` | `0x820E30FC` | **`day_period`** | Wrap modulus for `+3208` (`if (t >= day_period) t -= day_period`) |
| `+2796` | `0x820E368C` | `year_period` (`365.0` or similar) | Wrap modulus for `+3200` (`day_of_year`) |
| `+1164` | `0x820E302C` | Equinox offset | Subtracted from `day_of_year` (`+3200`) for solar declination |
| `+2800` | `0x820E3690` | Declination angular scale | Multiplied before `sin` (`sub_827DA3E8`) |
| `+2804` | `0x820E3694` | Axial tilt amplitude | Controls seasonal sun height |
| `+2024` | `0x820E3388` | Day/night horizon threshold | `f25`: if `f30 < f25` → Day (Sun), else → Night (Moon) |

> **Critical Design Advantage (Unit-Agnostic Conversion):**
> Because `day_period = LoadGuestFloatBE(r27.u32 + 1372)` is read directly from guest memory at runtime, our code does **not** need to hardcode whether `+3208` is stored in hours (`24.0`), minutes (`1440.0`), seconds (`86400.0`), or normalized `[0..1)`:
> - **Read live game hour (`0.0 .. 24.0`):** `live_hour = (raw_3208 / day_period) * 24.0f`
> - **Write target hour (`0.0 .. 24.0`):** `raw_3208 = (target_hour / 24.0f) * day_period`

### 2.4 Why Modifying `time_speed` (`+3212`) Directly Does NOT Work

In `prototype/generated/default/majin_recomp.151.cpp:175-242` (`loc_82168B8C`..`loc_82168BBC`), the game's world script controller evaluates `time_of_day (+3208)` and **unconditionally overwrites `*(float*)(sky + 3212)` every frame** with either the day speed (`r29 + 0x15CF8`) or night speed (`r29 + 0x15CFC`) — or `0.0` during scripted pauses.

Therefore, **never** try to freeze or scale time by patching `+3212` externally. Instead, intercept `f0` (`dt`) **inside `sub_82189A88` right before line 564 (`fmadds f0, f12, f0, f13`)**!

### 2.5 Exact Disassembly Around the Primary Hook (`sub_82189A88`)

Verified byte-exact in both `prototype/generated/default/majin_recomp.125.cpp` and `prototype-usa/generated/default/majin_recomp.125.cpp`:

```asm
; Pause check earlier in sub_82189A88 (lines 509-534):
;   f0  = frame dt in seconds (loaded from engine global +0x150F8)
;   f31 = 0.0f (loaded from 3280(r6))
;   if (is_paused) f0 = 0.0f

loc_82189AE4:
  0x82189AE4: lis    r11, -32242        ; 0x820E0000
  0x82189AE8: lwz    r10, 3204(r31)     ; r10 = sky->clock_enabled
  0x82189AEC: li     r30, 0
  0x82189AF0: addi   r27, r11, 11168    ; r27 = 0x820E2BA0  <=== PRIMARY MIDASM HOOK (after_instruction = true)
  0x82189AF4: stw    r30, 100(r31)
  0x82189AF8: cmplwi cr6, r10, 0        ; if (!clock_enabled) goto loc_82189BB0
  0x82189AFC: lfs    f30, 44(r27)       ; f30 = 1.0 / day_period
  0x82189B00: lfs    f29, 2796(r27)     ; f29 = year_period
  0x82189B04: beq    cr6, 0x82189BB0
  0x82189B08: lfs    f13, 3208(r31)     ; f13 = sky->time_of_day
  0x82189B0C: lfs    f12, 3212(r31)     ; f12 = sky->time_speed
  0x82189B10: fmadds f0, f12, f0, f13   ; f0 = time_speed * dt + time_of_day
  0x82189B14: lfs    f13, 1372(r27)     ; f13 = day_period
  0x82189B18: stfs   f0, 3208(r31)      ; sky->time_of_day = f0
  ...
loc_82189B74:
  ; If sky->sync_day_of_year (+3196) != 0:
  ;   sky->time_of_year (+3200) = floor(sky->time_of_year) + sky->time_of_day * (1 / day_period)
loc_82189BB0:
  ; Finds active DirectionalLight (r28) via global scene manager (29660)
loc_82189CB8:
  ; Computes astronomical sun direction (f29, f30, f28) from +3200 and +3208
loc_82189FD4:
  ; Stores sun dir (f29, f30, f28) into stack r1+112..120 and calls sub_82199088(dirLight, &dir)
  ;   <=== OPTIONAL PHASE 2 HOOK FOR FREE 360° AZIMUTH/ELEVATION OVERRIDE
```

> **Note on `r31 + 3204 == 0` (Dungeons / Scripted Areas):**
> Because `0x82189AF0` executes **before** `cmplwi cr6, r10, 0` (`0x82189AF8`), setting `+3208` (and updating the fractional part of `+3200`) inside our hook still works even in areas where the game paused its own clock — and if `r10 == 0`, the game skips `+3208 += speed * dt` and still jumps straight to `loc_82189BB0` → `loc_82189CB8` to recompute sun position and lighting from `+3208`!

### 2.6 Directional Light & Shadow Pass Connection

1. `sub_82189A88` calls `sub_82199088(dirLight, &sunDir)` at `0x82189FEC` (day) or `0x8218A20C` (night), writing the normalized 3D light direction to `dirLight + 432/+436/+440`.
2. Shadow cascade builders `sub_8218F0D0` (`majin_recomp.40.cpp:1530-1550`, Ortho PSSM) and `sub_8218FE38` (`majin_recomp.90.cpp:1635`, LiSPSM) both call `sub_82198B50(sceneMgr->field_84)` and read `dirLight + 432/+436/+440` directly to construct `M_lightView`.
3. Therefore, **any change to `+3208` (or to `f29/f30/f28` in Phase 2) automatically rotates both the lighting AND all 3 shadow cascades in complete lockstep**.

---

## 3. Proposed Feature Design & CVars

All CVars use `rex::cvar::Lifecycle::kHotReload` so changes take effect instantaneously in the **F10 In-Game Settings** overlay while the game runs behind the menu.

| CVar Name | Type | Default | Range | Behavior |
|---|---|---|---|---|
| `majin_sun_freeze` | `bool` | `false` | `false` / `true` | Freezes solar clock (`f0 = 0.0`) at the exact current moment. Gameplay, physics, animations, and 60 FPS continue normally. |
| `majin_sun_hour` | `double` | `-1.0` | `-1.0` (Auto), `0.0` .. `24.0` | When `>= 0.0`, overrides `sky->time_of_day` (`+3208`) to `(hour / 24.0) * day_period` and forces `f0 = 0.0` so the sun stays locked at the chosen hour. Setting back to `-1.0` releases control to the game clock. |
| `majin_sun_speed` | `double` | `1.0` | `0.0` .. `10.0` | Multiplies solar `dt` (`f0 *= speed`) when neither frozen nor locked to a manual hour. (`0.1x` = 10× slower sun for smooth shadow-motion inspection; `5.0x` = fast day/night timelapse.) |
| `majin_sun_angle_override` *(Phase 2, optional)* | `bool` | `false` | `false` / `true` | Enables manual spherical sun vector override at `0x82189FD4`. |
| `majin_sun_azimuth` *(Phase 2, optional)* | `double` | `0.0` | `0.0` .. `360.0` | Horizontal compass angle in degrees. |
| `majin_sun_elevation` *(Phase 2, optional)* | `double` | `45.0` | `5.0` .. `89.0` | Elevation above horizon in degrees (ideal for testing grazing 10°–20° shadows vs steep 75° noon shadows on any specific wall!). |

### Live Readback Helper (for F10 UI)

Expose two lightweight atomic getters in `src/sun_control.h` so `src/in_game_menu.cpp` can show the live in-game clock (`HH:MM`, Day/Night indicator) and implement a **"Lock at Current Time"** button seamlessly:

```cpp
namespace majin {
  float GetLiveSunHour();      // [0.0, 24.0), updated every frame by Majin_SunClockControl; -1.0f if not yet seen
  bool  IsLiveSunNight();      // Reads sky->is_night (+104)
  bool  HasLiveSunClock();     // True once sub_82189A88 has executed at least once
}
```

> **Smart UX Detail for "Freeze Sun" vs "Manual Hour":**
> - Clicking **"Freeze Sun"** keeps the exact current in-game hour without jumping.
> - Dragging the **"Time of Day (HH:MM)"** slider immediately sets `majin_sun_hour` to that hour (and automatically displays the exact `HH:MM` label, e.g. `14:30`).
> - Clicking **"Resume Game Clock"** resets `majin_sun_hour = -1.0` and `majin_sun_freeze = false`, letting time continue seamlessly from the current hour (no sudden jump back to the old hour, because `+3208` was already updated in guest memory!).

---

## 4. Detailed Implementation Steps

### Step 1 — Add `midasm_hook` to Both Probe Files

Add to **both** `prototype/probes.toml` AND `prototype-usa/probes.toml`:

```toml
# 0x82189AF0 (sub_82189A88, right after `addi r27,r11,11168`, < 0x821C0000: identical in PAL and USA):
# Intercept solar clock dt (f0) and SkySystem time_of_day (+3208) for live sun freeze / time-of-day control.
[[midasm_hook]]
address = 0x82189AF0
name = "Majin_SunClockControl"
registers = ["r31", "r27", "r10", "f0"]
after_instruction = true
```

*(Optional Phase 2 hook — also `< 0x821C0000`, identical in PAL and USA:)*
```toml
# 0x82189FD4 (sub_82189A88, right before storing sun direction f29/f30/f28 to stack for sub_82199088):
# Optional free spherical Azimuth/Elevation sun direction override for shadow testing.
[[midasm_hook]]
address = 0x82189FD4
name = "Majin_SunDirectionOverride"
registers = ["f29", "f30", "f28"]
after_instruction = false
```

> **Important Note on Phase 2 (`0x82189FD4`):**
> Check what `sub_82189A88` does right after `0x82189FEC` (`lines 1283-1335`): it calculates `f30_elev = (f30 * 0.5 + ...)` and checks `f30 < f25` to decide Day vs Night (`loc_8218A1D8`). In Majin's coordinate system, **Y is up** (`f30` is `dir.y`), and light direction points **from the sun toward the ground** (`dir.y < 0` during daytime, verified because `sub_8218F0D0` sets `at = eye + dir`, looking along the light rays, AND `loc_8218A1D8` flips `f29, f28` and swaps to moon when `f30 >= f25`!). Verify the exact sign of `(f29, f30, f28)` once in the first runtime log (`REXLOG_INFO` on first frame!) before enabling Phase 2.

---

### Step 2 — Create `src/sun_control.h` and `src/sun_control.cpp`

Create two clean, self-contained files following the exact style of `src/sssm_uv_align.cpp` and `src/post_effects.cpp`:

#### `src/sun_control.h`
```cpp
#pragma once

namespace majin {

// Live readback of the game's SkySystem solar clock (updated every frame by Majin_SunClockControl).
bool HasLiveSunClock();
float GetLiveSunHour();   // [0.0, 24.0) or -1.0f if unavailable
bool IsLiveSunNight();

// Helper for UI: format fractional hour [0, 24) as "HH:MM" (e.g. 14.5 -> "14:30").
void FormatSunHour(float hour, char* out_buf, size_t out_size);

}  // namespace majin
```

#### Core Hook Logic in `src/sun_control.cpp`
```cpp
// Big-endian guest float load/store helpers (same pattern as src/sssm_uv_align.cpp):
static float LoadGuestFloatBE(const uint8_t* ptr);
static void StoreGuestFloatBE(uint8_t* ptr, float value);

void Majin_SunClockControl(PPCRegister& r31, PPCRegister& r27, PPCRegister& r10, PPCRegister& f0) {
  const uint32_t sky_addr = r31.u32;
  const uint32_t const_addr = r27.u32;
  if (sky_addr < 0x10000 || const_addr < 0x10000) {
    return;
  }

  auto* mem = REX_KERNEL_MEMORY();
  uint8_t* sky = mem->TranslateVirtual<uint8_t*>(sky_addr);
  const uint8_t* consts = mem->TranslateVirtual<const uint8_t*>(const_addr);
  if (!sky || !consts) {
    return;
  }

  const float day_period = LoadGuestFloatBE(consts + 1372);
  if (!std::isfinite(day_period) || day_period <= 1e-4f) {
    return;
  }

  // One-time diagnostic log so we verify exact native units (e.g. 24.0 vs 1440.0 vs 86400.0).
  static std::atomic<bool> s_logged_init{false};
  const float raw_tod = LoadGuestFloatBE(sky + 3208);
  const float raw_day = LoadGuestFloatBE(sky + 3200);
  const float raw_spd = LoadGuestFloatBE(sky + 3212);
  if (!s_logged_init.exchange(true, std::memory_order_relaxed)) {
    REXLOG_INFO(
        "majin_sun_control active: sky=0x{:08X}, day_period={:.3f}, time_of_day={:.3f}, "
        "day_of_year={:.3f}, time_speed={:.6f}, clock_enabled={}",
        sky_addr, day_period, raw_tod, raw_day, raw_spd, r10.u32);
  }

  const double override_hour = REXCVAR_GET(majin_sun_hour);
  const bool freeze_sun = REXCVAR_GET(majin_sun_freeze);
  const double sun_speed = std::clamp(REXCVAR_GET(majin_sun_speed), 0.0, 10.0);

  float effective_tod = raw_tod;
  if (override_hour >= 0.0 && std::isfinite(override_hour)) {
    // Wrap hour into [0, 24) and convert to native SkySystem units.
    double wrapped = std::fmod(override_hour, 24.0);
    if (wrapped < 0.0) wrapped += 24.0;
    effective_tod = static_cast<float>((wrapped / 24.0) * static_cast<double>(day_period));
    StoreGuestFloatBE(sky + 3208, effective_tod);

    // Keep fractional part of day_of_year (+3200) synchronized when sync flag (+3196) is active,
    // so solar declination matches the chosen hour even if clock_enabled == 0.
    if (std::isfinite(raw_day)) {
      const float frac_day = static_cast<float>(wrapped / 24.0);
      StoreGuestFloatBE(sky + 3200, std::floor(raw_day) + frac_day);
    }

    // Prevent the game from advancing time_of_day this frame.
    f0.f64 = 0.0;
  } else if (freeze_sun) {
    f0.f64 = 0.0;
  } else if (sun_speed != 1.0) {
    f0.f64 = double(float(f0.f64 * sun_speed));
  }

  // Publish live readback for the F10 UI.
  float live_hour = (effective_tod / day_period) * 24.0f;
  live_hour = std::fmod(live_hour, 24.0f);
  if (live_hour < 0.0f) live_hour += 24.0f;
  g_live_sun_hour.store(live_hour, std::memory_order_relaxed);
  g_live_is_night.store(LoadGuestU32BE(sky + 104) != 0, std::memory_order_relaxed);
  g_has_live_sun.store(true, std::memory_order_relaxed);
}
```

#### Important Edge Cases Handled
1. **Sun-color / night-transition smoothing (`sky + 3356`):**
   - Look at `majin_recomp.125.cpp:1867-1896`: `sky + 3356` is exponentially lerped by `84(r1)` each frame toward `0.0` (day) or `1.0` (night). When scrubbing from Day to Night in F10 (while the game is **not** paused, or while the overlay is open), `sub_82189A88` still runs every frame so `3356` smoothly transitions within ~0.5s!
   - **Check:** Does opening the **F10 In-Game Settings** pause the game loop? **No!** As verified during shadow testing (`in_game_menu.cpp`), the game continues rendering live at 60 FPS behind the F10 window, so dragging the **Time of Day** slider moves the sun, shadows, sky colors, and HUD clock in real time behind the menu!
2. **Game pause / cutscenes (`sub_8216B140`):**
   - When the game itself is paused (`Start` menu) or in a scripted cutscene where `sub_8216B140` sets `f0 = 0.0`, multiplying `f0 * sun_speed` preserves `0.0` safely!
   - **Save-file safety:** Because `majin_sun_hour` defaults to `-1.0` (Auto) and `majin_sun_freeze` defaults to `false`, we should **NOT** persist `majin_sun_freeze` / `majin_sun_hour` into `launcher-usa.json` by default (keep them as live testing/debug session tools so a player never accidentally gets permanently stuck at 14:00 across sessions — or only persist if explicitly desired!).
3. **Interaction with `Majin_ShadowCascadeTexelSnap`:**
   - When the sun is **frozen**, the directional light vector `(f29, f30, f28)` is 100% constant -> `M_lightView`'s rotation axes `(X, Y, Z)` are 100% constant -> `Majin_ShadowCascadeTexelSnap` locks every shadow edge completely rock-solid with zero drift!

---

### Step 3 — Register `sun_control.cpp` in Build Systems

Add `${MAJIN_SHARED_DIR}/sun_control.cpp` to `MAJIN_SOURCES` in **both**:
1. `prototype/CMakeLists.txt` (around line 72, right after `sssm_uv_align.cpp`)
2. `prototype-usa/CMakeLists.txt` (matching entry)

---

### Step 4 — Add Live Sun Controls to In-Game Menu (`src/in_game_menu.cpp`)

Include `#include "sun_control.h"` in `src/in_game_menu.cpp` and add a dedicated **"SUN & TIME OF DAY"** subsection in `DrawGraphicsTab()` (right below `SHADOW DEPTH BIAS`, or gated behind `if (dev_mode)` depending on user preference — recommend showing under `if (dev_mode)` with a `[DEV]` badge, OR always visible under `SHADOW FILTERING & STABILITY` if the user wants it accessible without Dev Mode).

#### Recommended ImGui Layout
```cpp
ImGui::Spacing();
ImGui::Separator();
ImGui::Spacing();
ImGui::TextColored(ImVec4(0.98f, 0.78f, 0.30f, 1.0f), "SUN & TIME OF DAY");
ImGui::SameLine();
if (majin::HasLiveSunClock()) {
  char clock_buf[16];
  majin::FormatSunHour(majin::GetLiveSunHour(), clock_buf, sizeof(clock_buf));
  ImGui::TextDisabled("(Live: %s - %s)", clock_buf, majin::IsLiveSunNight() ? "Night" : "Day");
} else {
  ImGui::TextDisabled("(Waiting for outdoor scene)");
}
ImGui::Spacing();

// 1. Freeze Sun checkbox
bool sun_freeze = rex::cvar::Query<bool>("majin_sun_freeze");
if (ImGui::Checkbox("Freeze Sun / Time of Day", &sun_freeze)) {
  rex::cvar::SetFlagByName("majin_sun_freeze", sun_freeze ? "true" : "false");
}
ImGui::SameLine();
if (ImGui::Button("Resume Natural Cycle")) {
  rex::cvar::SetFlagByName("majin_sun_hour", "-1.0");
  rex::cvar::SetFlagByName("majin_sun_freeze", "false");
  rex::cvar::SetFlagByName("majin_sun_speed", "1.0");
}

// 2. Time of Day slider (00:00 - 24:00)
double override_hour = rex::cvar::Query<double>("majin_sun_hour");
float display_hour = (override_hour >= 0.0)
                         ? static_cast<float>(override_hour)
                         : (majin::HasLiveSunClock() ? majin::GetLiveSunHour() : 12.0f);
char time_fmt[32];
majin::FormatSunHour(display_hour, time_fmt, sizeof(time_fmt));
char label_buf[48];
snprintf(label_buf, sizeof(label_buf), "%s%s", time_fmt, (override_hour >= 0.0) ? " (Locked)" : " (Auto)");

ImGui::Text("Time of Day:");
ImGui::SetNextItemWidth(340.0f);
if (ImGui::SliderFloat("##MajinSunHour", &display_hour, 0.0f, 23.98f, label_buf)) {
  char buf[32];
  snprintf(buf, sizeof(buf), "%.3f", display_hour);
  rex::cvar::SetFlagByName("majin_sun_hour", buf);
}
ImGui::SameLine();
if (ImGui::Button("-15m##SunMinus")) {
  float next = std::fmod(display_hour - 0.25f + 24.0f, 24.0f);
  char buf[32];
  snprintf(buf, sizeof(buf), "%.3f", next);
  rex::cvar::SetFlagByName("majin_sun_hour", buf);
}
ImGui::SameLine();
if (ImGui::Button("+15m##SunPlus")) {
  float next = std::fmod(display_hour + 0.25f, 24.0f);
  char buf[32];
  snprintf(buf, sizeof(buf), "%.3f", next);
  rex::cvar::SetFlagByName("majin_sun_hour", buf);
}

// 3. Quick Presets
if (ImGui::Button("Dawn (06:30)", ImVec2(105, 24)))     rex::cvar::SetFlagByName("majin_sun_hour", "6.5");
ImGui::SameLine();
if (ImGui::Button("Morning (09:30)", ImVec2(115, 24)))  rex::cvar::SetFlagByName("majin_sun_hour", "9.5");
ImGui::SameLine();
if (ImGui::Button("Noon (12:00)", ImVec2(100, 24)))     rex::cvar::SetFlagByName("majin_sun_hour", "12.0");
ImGui::SameLine();
if (ImGui::Button("Sunset (17:30)", ImVec2(110, 24)))   rex::cvar::SetFlagByName("majin_sun_hour", "17.5");
ImGui::SameLine();
if (ImGui::Button("Night (22:00)", ImVec2(100, 24)))    rex::cvar::SetFlagByName("majin_sun_hour", "22.0");

// 4. Solar Cycle Speed slider (when not locked/frozen)
ImGui::Spacing();
float sun_speed = static_cast<float>(rex::cvar::Query<double>("majin_sun_speed"));
ImGui::BeginDisabled(sun_freeze || override_hour >= 0.0);
ImGui::Text("Solar Cycle Speed:");
ImGui::SetNextItemWidth(340.0f);
if (ImGui::SliderFloat("##MajinSunSpeed", &sun_speed, 0.0f, 5.0f, "%.2fx")) {
  char buf[32];
  snprintf(buf, sizeof(buf), "%.2f", sun_speed);
  rex::cvar::SetFlagByName("majin_sun_speed", buf);
}
ImGui::EndDisabled();
DrawHintText("Freeze or scrub the sun/moon position live to test shadows and lighting under steady conditions.");
```

*(Note: Verify actual dawn/noon/sunset hours on the first test run! Depending on the game's internal clock epoch — e.g., whether `0.0` represents `00:00` midnight or `06:00` sunrise — check the one-time `majin_sun_control active` log against the HUD sundial and adjust preset hours / offset by a constant if needed!)*

---

## 5. Build & Verification Checklist

1. **Codegen & Build (`majin.exe`):**
   ```powershell
   pwsh -NoProfile -ExecutionPolicy Bypass -File "scripts/build-usa.ps1" -Configuration Release
   ```
   Verify `prototype/generated/default/majin_recomp.125.cpp` now contains:
   ```cpp
   extern void Majin_SunClockControl(PPCRegister& r31, PPCRegister& r27, PPCRegister& r10, PPCRegister& f0);
   ...
   // addi r27,r11,11168
   ctx.r27.s64 = ctx.r11.s64 + 11168;
   Majin_SunClockControl(ctx.r31, ctx.r27, ctx.r10, ctx.f0);
   ```
2. **Runtime Verification Checklist (in-game):**
   - Check the runtime log for `majin_sun_control active: sky=0x..., day_period=..., time_of_day=..., day_of_year=..., time_speed=..., clock_enabled=...`.
   - Confirm whether `0.0` corresponds to midnight (`00:00`, moon straight up) or dawn (`06:00`, sunrise) by comparing `time_of_day / day_period * 24` against the top-right HUD sundial icon; apply an epoch offset constant if needed so `12:00` is exact solar noon!
   - Toggle **Freeze Sun**: verify shadows stop moving completely while Tepeu/Majin/camera move smoothly at 60 FPS.
   - Scrub **Time of Day** from `06:00` → `12:00` → `17:30` → `22:00`: verify sun arc, shadows, sky colors, moon transition, and HUD sundial all update live without crashes or visual glitches.
   - Click **Resume Natural Cycle**: verify the sun resumes advancing smoothly from the current hour.
   - Test entering an indoor area / cutscene / pause menu while `Freeze Sun` or `Manual Hour` is active to verify no softlocks or script hangs (remember: in Majin, some enemies/events depend on Day vs Night, so releasing `Manual Hour` restores normal progression).

---

## 6. Optional Phase 2 Enhancements (If Needed Later)

- **Free 360° Sun Direction Override (`0x82189FD4`):** Hook `f29, f30, f28` right before `sub_82199088` to point the directional light at any arbitrary `(azimuth, elevation)` angle — useful for deliberately sweeping shadows across a specific wall seam at grazing angles (`10°–20°`) even if the natural east-west solar arc never hits that wall at that angle.
- **Hotkey Toggle (e.g., `F6`):** Quick one-key freeze/unfreeze of the sun without opening the F10 overlay.
- **Season / Declination Slider (`r31 + 3200`):** Adjust the north-south tilt of the sun's arc (`day_of_year`).
