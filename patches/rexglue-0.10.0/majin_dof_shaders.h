// SPDX-License-Identifier: GPL-3.0-only
// Majistic Recomp - Optical Bokeh Depth of Field shader overrides (Vulkan GLSL + D3D12 HLSL)
//
// Replaces the 4 pixel shader passes of KB_shader_filterKBDOF.bin (TKemumakiKBDOF)
// in-place during shader translation (non-FSI/non-ROV host render target paths).
// Supports real-time hot-switching between:
//   - Optical Bokeh: 32-tap Golden-Angle Vogel Spiral depth-aware circular disc gather
//     with Interleaved Gradient Noise, bilateral foreground halo rejection, skybox cliff
//     removal, and dither-aware CoC smoothing (eliminating black stipple dots).
//   - Native: faithful reproduction of the original Xbox 360 3-layer crossfade scaled by
//     the real-time DoF intensity slider.
#pragma once

#include <cstdint>
#include <string>

#include <fmt/format.h>

namespace rex::graphics::majin {

// XXH3-64 microcode hashes of the 4 pixel shader passes in KB_shader_filterKBDOF.bin.
inline constexpr uint64_t kMajinDofHashPass0 = UINT64_C(0xA7179FD3F3918CDC);  // Linear near/far (blur1 uv)
inline constexpr uint64_t kMajinDofHashPass1 = UINT64_C(0x948C0BD36EC7C53C);  // Linear near/far (blur1 uv-0.5)
inline constexpr uint64_t kMajinDofHashPass2 = UINT64_C(0x8D7094D579BB4A7B);  // Thin-lens physical DoF
inline constexpr uint64_t kMajinDofHashPass3 = UINT64_C(0xF838CEF31FA2F96B);  // Fast symmetric DoF

inline int GetMajinDofPassVariant(uint64_t ucode_data_hash) {
  switch (ucode_data_hash) {
    case kMajinDofHashPass0:
      return 0;
    case kMajinDofHashPass1:
      return 1;
    case kMajinDofHashPass2:
      return 2;
    case kMajinDofHashPass3:
      return 3;
    default:
      return -1;
  }
}

struct MajinDofVulkanBindings {
  int pass_variant = -1;
  uint32_t float_count = 0;
  uint32_t tex_color = 0;
  uint32_t tex_depth = 0;
  uint32_t tex_blur1 = 0;
  uint32_t tex_blur2 = 0;
  uint32_t samp_color = 0;
  uint32_t samp_depth = 0;
  uint32_t samp_blur1 = 0;
  uint32_t samp_blur2 = 0;
  uint32_t offset_flags = 0;
  uint32_t offset_color_exp_bias = 0;
  uint32_t offset_dof_packed = 0;
  uint32_t flag_convert_color0_to_gamma = 0;
};

inline std::string BuildMajinDofGlslSource(const MajinDofVulkanBindings& b) {
  const std::string header = fmt::format(
      R"(#version 450
#define MAJIN_DOF_PASS {pass_variant}
#define MAJIN_DOF_FLOAT_COUNT {float_count}
#define MAJIN_FLAG_GAMMA0 {gamma_flag}u

layout(std140, set = 1, binding = 0) uniform XeSystemConstants {{
  layout(offset = {off_flags}) uint xe_flags;
  layout(offset = {off_exp}) vec4 xe_color_exp_bias;
  layout(offset = {off_dof}) uint xe_majin_dof_packed;
}};

layout(std140, set = 1, binding = 2) uniform XeFloatConstants {{
  vec4 xe_c[MAJIN_DOF_FLOAT_COUNT];
}};

layout(set = 3, binding = {tex_color}) uniform texture2DArray xe_tex_color;
layout(set = 3, binding = {tex_depth}) uniform texture2DArray xe_tex_depth;
layout(set = 3, binding = {tex_blur1}) uniform texture2DArray xe_tex_blur1;
layout(set = 3, binding = {tex_blur2}) uniform texture2DArray xe_tex_blur2;

layout(set = 3, binding = {samp_color}) uniform sampler xe_samp_color;
layout(set = 3, binding = {samp_depth}) uniform sampler xe_samp_depth;
layout(set = 3, binding = {samp_blur1}) uniform sampler xe_samp_blur1;
layout(set = 3, binding = {samp_blur2}) uniform sampler xe_samp_blur2;

layout(location = 0) in vec4 xe_in_interpolator_0;
layout(location = 0) out vec4 xe_out_fragment_data_0;
)",
      fmt::arg("pass_variant", b.pass_variant),
      fmt::arg("float_count", b.float_count),
      fmt::arg("gamma_flag", b.flag_convert_color0_to_gamma),
      fmt::arg("off_flags", b.offset_flags),
      fmt::arg("off_exp", b.offset_color_exp_bias),
      fmt::arg("off_dof", b.offset_dof_packed),
      fmt::arg("tex_color", b.tex_color),
      fmt::arg("tex_depth", b.tex_depth),
      fmt::arg("tex_blur1", b.tex_blur1),
      fmt::arg("tex_blur2", b.tex_blur2),
      fmt::arg("samp_color", b.samp_color),
      fmt::arg("samp_depth", b.samp_depth),
      fmt::arg("samp_blur1", b.samp_blur1),
      fmt::arg("samp_blur2", b.samp_blur2));

  static constexpr const char* kBody = R"(
const float kGoldenAngle = 2.39996323;
const int kBokehTaps = 32;
const vec3 kLumaWeights = vec3(0.299, 0.587, 0.114);

vec3 LinearToPWLGamma(vec3 c) {
  c = clamp(c, 0.0, 1.0);
  vec3 g0 = c * 4.0;
  vec3 g1 = c * 2.0 + 0.125;
  vec3 g2 = c + 0.25;
  vec3 g3 = c * 0.5 + 0.5;
  vec3 r = mix(g3, g2, vec3(lessThan(c, vec3(0.5))));
  r = mix(r, g1, vec3(lessThan(c, vec3(0.125))));
  r = mix(r, g0, vec3(lessThan(c, vec3(0.0625))));
  return r;
}

float InterleavedGradientNoise(vec2 pos) {
  return fract(52.9829189 * fract(dot(pos, vec2(0.06711056, 0.00583715))));
}

float LinearizeDepth(float raw_d) {
  float near_z = xe_c[0].z;
  float far_z = xe_c[0].w;
  float denom = (1.0 - raw_d) * (far_z - near_z) - far_z;
  if (abs(denom) < 1e-5) {
    denom = denom < 0.0 ? -1e-5 : 1e-5;
  }
  return -(far_z * near_z) / denom;
}

// Signed CoC in [-1.0, +1.0]: negative = near field, 0 = in focus, positive = far field.
float ComputeSignedCoC(float lin_z, bool apply_sky_cliff) {
  float signed_coc = 0.0;
#if MAJIN_DOF_PASS == 0 || MAJIN_DOF_PASS == 1
  vec4 p1 = xe_c[1];
  vec4 p2 = xe_c[2];
  if (lin_z > p1.z) {
    float inv_far = abs(p2.x) > 1e-5 ? (1.0 / p2.x) : 0.0;
    signed_coc = clamp((lin_z - p1.z) * inv_far, 0.0, 1.0);
  } else {
    float inv_near = abs(p2.y) > 1e-5 ? (1.0 / p2.y) : 0.0;
    signed_coc = -clamp((p1.x - lin_z) * inv_near, 0.0, 1.0);
  }
  if (apply_sky_cliff && lin_z > 25000.0) {
    return 0.05;
  }
#elif MAJIN_DOF_PASS == 2
  vec4 p1 = xe_c[1];
  float z_m = lin_z * 0.01;
  if (z_m > p1.y) {
    if (z_m >= p1.w) {
      float zc = min(z_m, p1.x);
      float raw_coc = clamp((zc - p1.y) / max(zc, 1e-4), 0.0, 1.0);
      if (!apply_sky_cliff) {
        float ramp_w = max((p1.w - p1.y) * 1.5, 0.5);
        raw_coc *= smoothstep(p1.w, p1.w + ramp_w, z_m);
      }
      signed_coc = raw_coc;
    }
  } else {
    if (z_m <= p1.z) {
      float raw_coc = clamp((p1.y - z_m) / max(z_m, 1e-4), 0.0, 1.0);
      if (!apply_sky_cliff) {
        float ramp_w = max((p1.y - p1.z) * 1.5, 0.5);
        float near_lo = max(p1.z - ramp_w, 0.0);
        float near_hi = max(p1.z, near_lo + 1e-4);
        raw_coc *= (1.0 - smoothstep(near_lo, near_hi, z_m));
      }
      signed_coc = -raw_coc;
    }
  }
#else
  vec4 p1 = xe_c[1];
  float z_m = lin_z * 0.01;
  float inv_range = abs(p1.y) > 1e-5 ? (1.0 / p1.y) : 0.0;
  signed_coc = clamp((z_m - p1.x) * inv_range, -1.0, 1.0);
#endif
  if (!apply_sky_cliff && signed_coc > 0.0) {
    // Gently soften skybox/clouds to ~55% of far-terrain blur so the sky
    // separates atmospherically from distant hills/trees without harsh cutouts.
    float sky_t = smoothstep(18000.0, 28000.0, lin_z);
    signed_coc *= mix(1.0, 0.55, sky_t);
  }
  return signed_coc;
}

// Measures non-monotonic depth reversals across 5 collinear samples [-2, -1, 0, +1, +2].
// Returns 0 on flat/slanted walls and straight silhouette edges; returns strong signal on
// both hole pixels and adjacent leaf pixels of 2x2 / 4x4 Bayer dithered foliage canopies.
float AxisDepthReversal(float rm2, float rm1, float r0, float rp1, float rp2) {
  float d0 = rm1 - rm2;
  float d1 = r0  - rm1;
  float d2 = rp1 - r0;
  float d3 = rp2 - rp1;
  float rev1 = (d0 * d1 < 0.0) ? min(abs(d0), abs(d1)) : 0.0;
  float rev2 = (d1 * d2 < 0.0) ? min(abs(d1), abs(d2)) : 0.0;
  float rev3 = (d2 * d3 < 0.0) ? min(abs(d2), abs(d3)) : 0.0;
  float dw2 = rm2 - r0;
  float de2 = rp2 - r0;
  float rev_wide = (dw2 * de2 > 0.0) ? min(abs(dw2), abs(de2)) : 0.0;
  return max(max(rev1, rev2), max(rev3, rev_wide));
}

float SampleCanopyDepthReversal(vec2 uv, vec2 step_uv, vec2 half_texel,
                                float r0, float rN1, float rS1, float rE1, float rW1,
                                float d_scale) {
  vec2 step2 = step_uv * 2.0;
  float rN2  = textureLod(sampler2DArray(xe_tex_depth, xe_samp_depth), vec3(clamp(uv + vec2( 0.0, -step2.y), half_texel, 1.0 - half_texel), 0.0), 0.0).r;
  float rS2  = textureLod(sampler2DArray(xe_tex_depth, xe_samp_depth), vec3(clamp(uv + vec2( 0.0,  step2.y), half_texel, 1.0 - half_texel), 0.0), 0.0).r;
  float rE2  = textureLod(sampler2DArray(xe_tex_depth, xe_samp_depth), vec3(clamp(uv + vec2( step2.x, 0.0), half_texel, 1.0 - half_texel), 0.0), 0.0).r;
  float rW2  = textureLod(sampler2DArray(xe_tex_depth, xe_samp_depth), vec3(clamp(uv + vec2(-step2.x, 0.0), half_texel, 1.0 - half_texel), 0.0), 0.0).r;
  float rNW1 = textureLod(sampler2DArray(xe_tex_depth, xe_samp_depth), vec3(clamp(uv + vec2(-step_uv.x, -step_uv.y), half_texel, 1.0 - half_texel), 0.0), 0.0).r;
  float rNE1 = textureLod(sampler2DArray(xe_tex_depth, xe_samp_depth), vec3(clamp(uv + vec2( step_uv.x, -step_uv.y), half_texel, 1.0 - half_texel), 0.0), 0.0).r;
  float rSW1 = textureLod(sampler2DArray(xe_tex_depth, xe_samp_depth), vec3(clamp(uv + vec2(-step_uv.x,  step_uv.y), half_texel, 1.0 - half_texel), 0.0), 0.0).r;
  float rSE1 = textureLod(sampler2DArray(xe_tex_depth, xe_samp_depth), vec3(clamp(uv + vec2( step_uv.x,  step_uv.y), half_texel, 1.0 - half_texel), 0.0), 0.0).r;
  float rNW2 = textureLod(sampler2DArray(xe_tex_depth, xe_samp_depth), vec3(clamp(uv + vec2(-step2.x, -step2.y), half_texel, 1.0 - half_texel), 0.0), 0.0).r;
  float rNE2 = textureLod(sampler2DArray(xe_tex_depth, xe_samp_depth), vec3(clamp(uv + vec2( step2.x, -step2.y), half_texel, 1.0 - half_texel), 0.0), 0.0).r;
  float rSW2 = textureLod(sampler2DArray(xe_tex_depth, xe_samp_depth), vec3(clamp(uv + vec2(-step2.x,  step2.y), half_texel, 1.0 - half_texel), 0.0), 0.0).r;
  float rSE2 = textureLod(sampler2DArray(xe_tex_depth, xe_samp_depth), vec3(clamp(uv + vec2( step2.x,  step2.y), half_texel, 1.0 - half_texel), 0.0), 0.0).r;
  float revH  = AxisDepthReversal(rW2,  rW1,  r0, rE1,  rE2);
  float revV  = AxisDepthReversal(rN2,  rN1,  r0, rS1,  rS2);
  float revD1 = AxisDepthReversal(rNW2, rNW1, r0, rSE1, rSE2);
  float revD2 = AxisDepthReversal(rNE2, rNE1, r0, rSW1, rSW2);
  return max(max(revH, revV), max(revD1, revD2)) / d_scale;
}

vec4 FinalizeOutput(vec4 color) {
  vec4 out_c = vec4(color.rgb * xe_color_exp_bias.x, color.a);
  if ((xe_flags & MAJIN_FLAG_GAMMA0) != 0u) {
    out_c.rgb = LinearToPWLGamma(out_c.rgb);
  }
  return out_c;
}

void main() {
  vec2 uv = xe_in_interpolator_0.xy;
  uint packed = xe_majin_dof_packed;
  bool use_bokeh = (packed & 1u) != 0u;
  float dof_intensity = float(packed >> 1u) * 0.001;

  vec4 center_color = textureLod(sampler2DArray(xe_tex_color, xe_samp_color), vec3(uv, 0.0), 0.0);
  if (dof_intensity <= 0.001) {
    xe_out_fragment_data_0 = FinalizeOutput(center_color);
    return;
  }

  float raw_depth_c = textureLod(sampler2DArray(xe_tex_depth, xe_samp_depth), vec3(uv, 0.0), 0.0).r;
  float lin_z_c = LinearizeDepth(raw_depth_c);

  if (!use_bokeh) {
    // Native Xbox 360 3-layer crossfade (scaled by DoF intensity slider).
    vec2 uv_half = uv - 0.5 * xe_c[0].xy;
#if MAJIN_DOF_PASS == 0
    vec4 blur1 = textureLod(sampler2DArray(xe_tex_blur1, xe_samp_blur1), vec3(uv, 0.0), 0.0);
#else
    vec4 blur1 = textureLod(sampler2DArray(xe_tex_blur1, xe_samp_blur1), vec3(uv_half, 0.0), 0.0);
#endif
    vec4 blur2 = textureLod(sampler2DArray(xe_tex_blur2, xe_samp_blur2), vec3(uv_half, 0.0), 0.0);

    float coc_norm = abs(ComputeSignedCoC(lin_z_c, true));
    float coc = clamp(coc_norm * 2.0 * dof_intensity, 0.0, 2.0);
    float t0 = min(coc, 1.0);
    float t1 = max(coc - 1.0, 0.0);
    vec4 cA = mix(center_color, blur1, t0);
    vec4 cB = mix(blur1, blur2, t1);
    vec4 native_out = mix(cA, cB, 0.5 * coc);
    xe_out_fragment_data_0 = FinalizeOutput(native_out);
    return;
  }

  // Optical Bokeh Depth of Field (32-tap Golden-Angle Vogel Spiral).
  vec2 tex_size = vec2(textureSize(sampler2DArray(xe_tex_color, xe_samp_color), 0).xy);
  vec2 inv_tex_size = 1.0 / max(tex_size, vec2(1.0));
  vec2 half_texel = 0.5 * inv_tex_size;

  // Step of 1 guest pixel (at 720p native, scales cleanly with 2x/3x/4x draw resolution).
  float guest_step_px = max(floor(tex_size.y / 720.0 + 0.5), 1.0);
  vec2 d_step = inv_tex_size * guest_step_px;

  float raw_n = textureLod(sampler2DArray(xe_tex_depth, xe_samp_depth), vec3(clamp(uv + vec2(0.0, -d_step.y), half_texel, 1.0 - half_texel), 0.0), 0.0).r;
  float raw_s = textureLod(sampler2DArray(xe_tex_depth, xe_samp_depth), vec3(clamp(uv + vec2(0.0,  d_step.y), half_texel, 1.0 - half_texel), 0.0), 0.0).r;
  float raw_e = textureLod(sampler2DArray(xe_tex_depth, xe_samp_depth), vec3(clamp(uv + vec2( d_step.x, 0.0), half_texel, 1.0 - half_texel), 0.0), 0.0).r;
  float raw_w = textureLod(sampler2DArray(xe_tex_depth, xe_samp_depth), vec3(clamp(uv + vec2(-d_step.x, 0.0), half_texel, 1.0 - half_texel), 0.0), 0.0).r;

  // Detect Xbox 360 screen-door dither canopies purely from non-monotonic depth reversals
  // across 5 collinear samples [-2, -1, 0, +1, +2] on all 4 axes (H, V, D1, D2).
  // Zero false positives on solid brick walls or straight silhouette edges.
  float d_scale = max(min(raw_depth_c, 1.0 - raw_depth_c), 0.0005);
  float dither_osc = SampleCanopyDepthReversal(
      uv, d_step, half_texel, raw_depth_c, raw_n, raw_s, raw_e, raw_w, d_scale);
  float dither_conf = smoothstep(0.004, 0.020, dither_osc);

  float lin_z_n = LinearizeDepth(raw_n);
  float lin_z_s = LinearizeDepth(raw_s);
  float lin_z_e = LinearizeDepth(raw_e);
  float lin_z_w = LinearizeDepth(raw_w);

  // Smooth atmospheric skybox blur (~55% of far terrain blur) without hard cliffs.
  float coc_c = ComputeSignedCoC(lin_z_c, false);
  float coc_n = ComputeSignedCoC(lin_z_n, false);
  float coc_s = ComputeSignedCoC(lin_z_s, false);
  float coc_e = ComputeSignedCoC(lin_z_e, false);
  float coc_w = ComputeSignedCoC(lin_z_w, false);

  float avg_coc = 0.2 * (coc_c + coc_n + coc_s + coc_e + coc_w);
  float min_near_coc = min(coc_c, min(min(coc_n, coc_s), min(coc_e, coc_w)));

  // Continuously blend CoC and reference depth only on genuine depth-dithered canopies.
  float effective_coc = mix(coc_c, avg_coc, dither_conf);
  float center_abs_coc = abs(effective_coc);
  float near_bleed_coc = max(-min_near_coc, 0.0);

  // Aspect-corrected optical aperture radius (vertical fraction of screen height).
  float max_radius_v = 0.0115 * dof_intensity;
  float gather_coc = max(center_abs_coc, near_bleed_coc);

  // Fast early-out for tack-sharp in-focus pixels with no nearby foreground blur.
  if (gather_coc < 0.004 && dither_conf <= 0.001) {
    xe_out_fragment_data_0 = FinalizeOutput(center_color);
    return;
  }

  // Also probe a wider 4-tap ring for near-field foreground blur expansion so
  // out-of-focus foreground silhouettes softly bleed over the sharp focal plane.
  vec2 uv_radius_max = vec2(max_radius_v * (tex_size.y / max(tex_size.x, 1.0)), max_radius_v);
  if (gather_coc < 0.35) {
    vec2 probe_r = uv_radius_max * 0.65;
    float p_d0 = LinearizeDepth(textureLod(sampler2DArray(xe_tex_depth, xe_samp_depth), vec3(clamp(uv + vec2( probe_r.x, 0.0), half_texel, 1.0 - half_texel), 0.0), 0.0).r);
    float p_d1 = LinearizeDepth(textureLod(sampler2DArray(xe_tex_depth, xe_samp_depth), vec3(clamp(uv + vec2(-probe_r.x, 0.0), half_texel, 1.0 - half_texel), 0.0), 0.0).r);
    float p_d2 = LinearizeDepth(textureLod(sampler2DArray(xe_tex_depth, xe_samp_depth), vec3(clamp(uv + vec2(0.0,  probe_r.y), half_texel, 1.0 - half_texel), 0.0), 0.0).r);
    float p_d3 = LinearizeDepth(textureLod(sampler2DArray(xe_tex_depth, xe_samp_depth), vec3(clamp(uv + vec2(0.0, -probe_r.y), half_texel, 1.0 - half_texel), 0.0), 0.0).r);
    float p_near = max(max(-ComputeSignedCoC(p_d0, false), -ComputeSignedCoC(p_d1, false)),
                       max(-ComputeSignedCoC(p_d2, false), -ComputeSignedCoC(p_d3, false)));
    gather_coc = max(gather_coc, p_near);
  }

  if (gather_coc < 0.004) {
    xe_out_fragment_data_0 = FinalizeOutput(center_color);
    return;
  }

  // Continuously blend reference depth on dithered canopy pixels so background taps
  // aren't rejected by a lone leaf pixel, with zero hard step on solid geometry.
  float avg_z = 0.2 * (lin_z_c + lin_z_n + lin_z_s + lin_z_e + lin_z_w);
  float ref_z = mix(lin_z_c, avg_z, dither_conf);

  // Per-pixel rotational jitter via Interleaved Gradient Noise eliminates ring banding.
  float angle_offset = InterleavedGradientNoise(gl_FragCoord.xy) * 6.2831853;
  vec2 disc_scale = uv_radius_max * gather_coc;

  // Continuous Bayer 4x4 canopy cancellation (0.0 on all solid walls/surfaces; smoothly
  // ramps 0.0->1.0 across coc 0.05..0.35 on distant dithered foliage so there is never
  // an abrupt blur jump anywhere in the scene).
  float bayer_w = dither_conf * smoothstep(0.05, 0.35, gather_coc) * smoothstep(0.0, 0.08, dof_intensity);
  disc_scale = mix(disc_scale, max(disc_scale, d_step * 3.25), bayer_w);

  // Clean center color only on depth-dithered canopy pixels (avoids fetching on solid walls).
  vec3 clean_center_rgb = center_color.rgb;
  if (dither_conf > 0.001) {
    vec3 cN = textureLod(sampler2DArray(xe_tex_color, xe_samp_color), vec3(clamp(uv + vec2(0.0, -d_step.y), half_texel, 1.0 - half_texel), 0.0), 0.0).rgb;
    vec3 cS = textureLod(sampler2DArray(xe_tex_color, xe_samp_color), vec3(clamp(uv + vec2(0.0,  d_step.y), half_texel, 1.0 - half_texel), 0.0), 0.0).rgb;
    vec3 cE = textureLod(sampler2DArray(xe_tex_color, xe_samp_color), vec3(clamp(uv + vec2( d_step.x, 0.0), half_texel, 1.0 - half_texel), 0.0), 0.0).rgb;
    vec3 cW = textureLod(sampler2DArray(xe_tex_color, xe_samp_color), vec3(clamp(uv + vec2(-d_step.x, 0.0), half_texel, 1.0 - half_texel), 0.0), 0.0).rgb;
    vec3 card_avg = 0.25 * (cN + cS + cE + cW);
    clean_center_rgb = mix(center_color.rgb, card_avg, dither_conf);
  }

  float luma_c = dot(center_color.rgb, kLumaWeights);
  float center_w = (1.0 - bayer_w) * (1.0 + 0.25 * smoothstep(0.55, 1.8, luma_c) * center_abs_coc);
  vec3 accum_rgb = clean_center_rgb * center_w;
  float accum_w = center_w;
  float inner_r_t = 0.18 * bayer_w;

  for (int i = 0; i < kBokehTaps; ++i) {
    float t = (float(i) + 0.5) / float(kBokehTaps);
    float r = sqrt(mix(inner_r_t, 1.0, t));
    float theta = float(i) * kGoldenAngle + angle_offset;
    vec2 dir = vec2(cos(theta), sin(theta));
    vec2 sample_uv = clamp(uv + dir * (r * disc_scale), half_texel, 1.0 - half_texel);

    // Sample color with linear sampler (xe_samp_blur1) and depth with point sampler (xe_samp_depth).
    vec3 s_rgb = textureLod(sampler2DArray(xe_tex_color, xe_samp_blur1), vec3(sample_uv, 0.0), 0.0).rgb;
    float s_raw_d = textureLod(sampler2DArray(xe_tex_depth, xe_samp_depth), vec3(sample_uv, 0.0), 0.0).r;
    float s_z = LinearizeDepth(s_raw_d);
    float s_signed_coc = ComputeSignedCoC(s_z, false);
    float s_abs_coc = abs(s_signed_coc);

    // Sharpness-aware bilateral weighting:
    // - Near-field blurred foreground bleeds smoothly by its CoC reach.
    // - In-focus foreground occluders (Tepeu/pillars/walls, s_abs_coc << center_abs_coc)
    //   are strictly rejected (zero dark halo!).
    // - Similarly blurred far layers (e.g. overlapping dithered canopy leaves) blend freely
    //   so 4x4 Bayer dither holes dissolve cleanly!
    float w = 1.0;
    if (s_z < ref_z * 0.96) {
      if (s_signed_coc < -0.01) {
        float reach = r * gather_coc;
        w = smoothstep(reach * 0.55, reach * 1.05 + 0.04, s_abs_coc);
      } else {
        float occluder_sharpness =
            clamp((center_abs_coc - s_abs_coc) / max(center_abs_coc, 0.08), 0.0, 1.0);
        w = 1.0 - occluder_sharpness;
      }
    } else if (effective_coc < -0.02) {
      // Center is blurred foreground; slightly temper sharp background bleed.
      w = mix(0.65, 1.0, smoothstep(0.0, 0.35, s_abs_coc));
    }

    // Subtle photographic bokeh highlight weighting (keeps circular specular discs lively).
    float s_luma = dot(s_rgb, kLumaWeights);
    float bokeh_boost = 1.0 + 0.35 * smoothstep(0.55, 1.8, s_luma) * s_abs_coc;
    w *= bokeh_boost;

    accum_rgb += s_rgb * w;
    accum_w += w;
  }

  vec3 bokeh_rgb = accum_rgb / max(accum_w, 1e-4);

  // Seamless ramp from crisp focus plane (0.0 exactly at early-out) into optical blur.
  float blend_t = smoothstep(0.004, 0.08, gather_coc);
  vec3 final_rgb = mix(clean_center_rgb, bokeh_rgb, blend_t);
  xe_out_fragment_data_0 = FinalizeOutput(vec4(final_rgb, center_color.a));
}
)";

  return header + kBody;
}

struct MajinDofD3D12Bindings {
  int pass_variant = -1;
  bool bindless = true;
  uint32_t float_count = 0;
  // Bindless: indices into xe_descriptor_indices (uint32 array).
  // Bindful: SRV register indices (relative to t0) and Sampler register indices (relative to s0).
  uint32_t tex_color = 0;
  uint32_t tex_depth = 0;
  uint32_t tex_blur1 = 0;
  uint32_t tex_blur2 = 0;
  uint32_t samp_color = 0;
  uint32_t samp_depth = 0;
  uint32_t samp_blur1 = 0;
  uint32_t samp_blur2 = 0;
  uint32_t descriptor_indices_vec4_count = 4;
  uint32_t offset_flags = 0;
  uint32_t offset_color_exp_bias = 0;
  uint32_t offset_dof_packed = 0;
  uint32_t flag_convert_color0_to_gamma = 0;
};

inline std::string FormatHlslPackOffset(uint32_t byte_offset) {
  static constexpr char kComps[4] = {'x', 'y', 'z', 'w'};
  return fmt::format("c{}.{}", byte_offset / 16u, kComps[(byte_offset / 4u) & 3u]);
}

inline std::string BuildMajinDofHlslSource(const MajinDofD3D12Bindings& b) {
  std::string header = fmt::format(
      R"(#define MAJIN_DOF_PASS {pass_variant}
#define MAJIN_DOF_FLOAT_COUNT {float_count}
#define MAJIN_FLAG_GAMMA0 {gamma_flag}u

cbuffer XeSystemConstants : register(b0) {{
  uint xe_flags : packoffset({po_flags});
  uint xe_majin_dof_packed : packoffset({po_dof});
  float4 xe_color_exp_bias : packoffset(c{c_exp});
}};

cbuffer XeFloatConstants : register(b1) {{
  float4 xe_c[MAJIN_DOF_FLOAT_COUNT];
}};
)",
      fmt::arg("pass_variant", b.pass_variant),
      fmt::arg("float_count", b.float_count),
      fmt::arg("gamma_flag", b.flag_convert_color0_to_gamma),
      fmt::arg("po_flags", FormatHlslPackOffset(b.offset_flags)),
      fmt::arg("po_dof", FormatHlslPackOffset(b.offset_dof_packed)),
      fmt::arg("c_exp", b.offset_color_exp_bias / 16u));

  if (b.bindless) {
    header += fmt::format(
        R"(
cbuffer XeDescriptorIndices : register(b4) {{
  uint4 xe_descriptor_indices[{desc_vec4_count}];
}};
Texture2DArray<float4> xe_textures_2d[] : register(t0, space1);
SamplerState xe_samplers[] : register(s0, space0);

uint GetDescIndex(uint flat_idx) {{
  return xe_descriptor_indices[flat_idx >> 2u][flat_idx & 3u];
}}

#define TEX_COLOR  xe_textures_2d[GetDescIndex({tex_color}u)]
#define TEX_DEPTH  xe_textures_2d[GetDescIndex({tex_depth}u)]
#define TEX_BLUR1  xe_textures_2d[GetDescIndex({tex_blur1}u)]
#define TEX_BLUR2  xe_textures_2d[GetDescIndex({tex_blur2}u)]
#define SAMP_COLOR xe_samplers[GetDescIndex({samp_color}u)]
#define SAMP_DEPTH xe_samplers[GetDescIndex({samp_depth}u)]
#define SAMP_BLUR1 xe_samplers[GetDescIndex({samp_blur1}u)]
#define SAMP_BLUR2 xe_samplers[GetDescIndex({samp_blur2}u)]
)",
        fmt::arg("desc_vec4_count", b.descriptor_indices_vec4_count),
        fmt::arg("tex_color", b.tex_color),
        fmt::arg("tex_depth", b.tex_depth),
        fmt::arg("tex_blur1", b.tex_blur1),
        fmt::arg("tex_blur2", b.tex_blur2),
        fmt::arg("samp_color", b.samp_color),
        fmt::arg("samp_depth", b.samp_depth),
        fmt::arg("samp_blur1", b.samp_blur1),
        fmt::arg("samp_blur2", b.samp_blur2));
  } else {
    header += fmt::format(
        R"(
Texture2DArray<float4> xe_tex_color : register(t{tex_color});
Texture2DArray<float4> xe_tex_depth : register(t{tex_depth});
Texture2DArray<float4> xe_tex_blur1 : register(t{tex_blur1});
Texture2DArray<float4> xe_tex_blur2 : register(t{tex_blur2});
SamplerState xe_samp_color : register(s{samp_color});
SamplerState xe_samp_depth : register(s{samp_depth});
SamplerState xe_samp_blur1 : register(s{samp_blur1});
SamplerState xe_samp_blur2 : register(s{samp_blur2});

#define TEX_COLOR  xe_tex_color
#define TEX_DEPTH  xe_tex_depth
#define TEX_BLUR1  xe_tex_blur1
#define TEX_BLUR2  xe_tex_blur2
#define SAMP_COLOR xe_samp_color
#define SAMP_DEPTH xe_samp_depth
#define SAMP_BLUR1 xe_samp_blur1
#define SAMP_BLUR2 xe_samp_blur2
)",
        fmt::arg("tex_color", b.tex_color),
        fmt::arg("tex_depth", b.tex_depth),
        fmt::arg("tex_blur1", b.tex_blur1),
        fmt::arg("tex_blur2", b.tex_blur2),
        fmt::arg("samp_color", b.samp_color),
        fmt::arg("samp_depth", b.samp_depth),
        fmt::arg("samp_blur1", b.samp_blur1),
        fmt::arg("samp_blur2", b.samp_blur2));
  }

  static constexpr const char* kBody = R"(
static const float kGoldenAngle = 2.39996323f;
static const int kBokehTaps = 32;
static const float3 kLumaWeights = float3(0.299f, 0.587f, 0.114f);

float3 LinearToPWLGamma(float3 c) {
  c = saturate(c);
  float3 g0 = c * 4.0f;
  float3 g1 = c * 2.0f + 0.125f;
  float3 g2 = c + 0.25f;
  float3 g3 = c * 0.5f + 0.5f;
  float3 r = lerp(g3, g2, (float3)(c < 0.5f));
  r = lerp(r, g1, (float3)(c < 0.125f));
  r = lerp(r, g0, (float3)(c < 0.0625f));
  return r;
}

float InterleavedGradientNoise(float2 pos) {
  return frac(52.9829189f * frac(dot(pos, float2(0.06711056f, 0.00583715f))));
}

float LinearizeDepth(float raw_d) {
  float near_z = xe_c[0].z;
  float far_z = xe_c[0].w;
  float denom = (1.0f - raw_d) * (far_z - near_z) - far_z;
  if (abs(denom) < 1e-5f) {
    denom = denom < 0.0f ? -1e-5f : 1e-5f;
  }
  return -(far_z * near_z) / denom;
}

float ComputeSignedCoC(float lin_z, bool apply_sky_cliff) {
  float signed_coc = 0.0f;
#if MAJIN_DOF_PASS == 0 || MAJIN_DOF_PASS == 1
  float4 p1 = xe_c[1];
  float4 p2 = xe_c[2];
  if (lin_z > p1.z) {
    float inv_far = abs(p2.x) > 1e-5f ? (1.0f / p2.x) : 0.0f;
    signed_coc = saturate((lin_z - p1.z) * inv_far);
  } else {
    float inv_near = abs(p2.y) > 1e-5f ? (1.0f / p2.y) : 0.0f;
    signed_coc = -saturate((p1.x - lin_z) * inv_near);
  }
  if (apply_sky_cliff && lin_z > 25000.0f) {
    return 0.05f;
  }
#elif MAJIN_DOF_PASS == 2
  float4 p1 = xe_c[1];
  float z_m = lin_z * 0.01f;
  if (z_m > p1.y) {
    if (z_m >= p1.w) {
      float zc = min(z_m, p1.x);
      float raw_coc = saturate((zc - p1.y) / max(zc, 1e-4f));
      if (!apply_sky_cliff) {
        float ramp_w = max((p1.w - p1.y) * 1.5f, 0.5f);
        raw_coc *= smoothstep(p1.w, p1.w + ramp_w, z_m);
      }
      signed_coc = raw_coc;
    }
  } else {
    if (z_m <= p1.z) {
      float raw_coc = saturate((p1.y - z_m) / max(z_m, 1e-4f));
      if (!apply_sky_cliff) {
        float ramp_w = max((p1.y - p1.z) * 1.5f, 0.5f);
        float near_lo = max(p1.z - ramp_w, 0.0f);
        float near_hi = max(p1.z, near_lo + 1e-4f);
        raw_coc *= (1.0f - smoothstep(near_lo, near_hi, z_m));
      }
      signed_coc = -raw_coc;
    }
  }
#else
  float4 p1 = xe_c[1];
  float z_m = lin_z * 0.01f;
  float inv_range = abs(p1.y) > 1e-5f ? (1.0f / p1.y) : 0.0f;
  signed_coc = clamp((z_m - p1.x) * inv_range, -1.0f, 1.0f);
#endif
  if (!apply_sky_cliff && signed_coc > 0.0f) {
    float sky_t = smoothstep(18000.0f, 28000.0f, lin_z);
    signed_coc *= lerp(1.0f, 0.55f, sky_t);
  }
  return signed_coc;
}

float AxisDepthReversal(float rm2, float rm1, float r0, float rp1, float rp2) {
  float d0 = rm1 - rm2;
  float d1 = r0  - rm1;
  float d2 = rp1 - r0;
  float d3 = rp2 - rp1;
  float rev1 = (d0 * d1 < 0.0f) ? min(abs(d0), abs(d1)) : 0.0f;
  float rev2 = (d1 * d2 < 0.0f) ? min(abs(d1), abs(d2)) : 0.0f;
  float rev3 = (d2 * d3 < 0.0f) ? min(abs(d2), abs(d3)) : 0.0f;
  float dw2 = rm2 - r0;
  float de2 = rp2 - r0;
  float rev_wide = (dw2 * de2 > 0.0f) ? min(abs(dw2), abs(de2)) : 0.0f;
  return max(max(rev1, rev2), max(rev3, rev_wide));
}

float SampleCanopyDepthReversal(float2 uv, float2 step_uv, float2 half_texel,
                                float r0, float rN1, float rS1, float rE1, float rW1,
                                float d_scale) {
  float2 step2 = step_uv * 2.0f;
  float rN2  = TEX_DEPTH.SampleLevel(SAMP_DEPTH, float3(clamp(uv + float2( 0.0f, -step2.y), half_texel, 1.0f - half_texel), 0.0f), 0.0f).r;
  float rS2  = TEX_DEPTH.SampleLevel(SAMP_DEPTH, float3(clamp(uv + float2( 0.0f,  step2.y), half_texel, 1.0f - half_texel), 0.0f), 0.0f).r;
  float rE2  = TEX_DEPTH.SampleLevel(SAMP_DEPTH, float3(clamp(uv + float2( step2.x, 0.0f), half_texel, 1.0f - half_texel), 0.0f), 0.0f).r;
  float rW2  = TEX_DEPTH.SampleLevel(SAMP_DEPTH, float3(clamp(uv + float2(-step2.x, 0.0f), half_texel, 1.0f - half_texel), 0.0f), 0.0f).r;
  float rNW1 = TEX_DEPTH.SampleLevel(SAMP_DEPTH, float3(clamp(uv + float2(-step_uv.x, -step_uv.y), half_texel, 1.0f - half_texel), 0.0f), 0.0f).r;
  float rNE1 = TEX_DEPTH.SampleLevel(SAMP_DEPTH, float3(clamp(uv + float2( step_uv.x, -step_uv.y), half_texel, 1.0f - half_texel), 0.0f), 0.0f).r;
  float rSW1 = TEX_DEPTH.SampleLevel(SAMP_DEPTH, float3(clamp(uv + float2(-step_uv.x,  step_uv.y), half_texel, 1.0f - half_texel), 0.0f), 0.0f).r;
  float rSE1 = TEX_DEPTH.SampleLevel(SAMP_DEPTH, float3(clamp(uv + float2( step_uv.x,  step_uv.y), half_texel, 1.0f - half_texel), 0.0f), 0.0f).r;
  float rNW2 = TEX_DEPTH.SampleLevel(SAMP_DEPTH, float3(clamp(uv + float2(-step2.x, -step2.y), half_texel, 1.0f - half_texel), 0.0f), 0.0f).r;
  float rNE2 = TEX_DEPTH.SampleLevel(SAMP_DEPTH, float3(clamp(uv + float2( step2.x, -step2.y), half_texel, 1.0f - half_texel), 0.0f), 0.0f).r;
  float rSW2 = TEX_DEPTH.SampleLevel(SAMP_DEPTH, float3(clamp(uv + float2(-step2.x,  step2.y), half_texel, 1.0f - half_texel), 0.0f), 0.0f).r;
  float rSE2 = TEX_DEPTH.SampleLevel(SAMP_DEPTH, float3(clamp(uv + float2( step2.x,  step2.y), half_texel, 1.0f - half_texel), 0.0f), 0.0f).r;
  float revH  = AxisDepthReversal(rW2,  rW1,  r0, rE1,  rE2);
  float revV  = AxisDepthReversal(rN2,  rN1,  r0, rS1,  rS2);
  float revD1 = AxisDepthReversal(rNW2, rNW1, r0, rSE1, rSE2);
  float revD2 = AxisDepthReversal(rNE2, rNE1, r0, rSW1, rSW2);
  return max(max(revH, revV), max(revD1, revD2)) / d_scale;
}

float4 FinalizeOutput(float4 color) {
  float4 out_c = float4(color.rgb * xe_color_exp_bias.x, color.a);
  if ((xe_flags & MAJIN_FLAG_GAMMA0) != 0u) {
    out_c.rgb = LinearToPWLGamma(out_c.rgb);
  }
  return out_c;
}

float4 main(float4 xe_in_uv : TEXCOORD0, float4 xe_pos : SV_Position) : SV_Target0 {
  float2 uv = xe_in_uv.xy;
  uint packed = xe_majin_dof_packed;
  bool use_bokeh = (packed & 1u) != 0u;
  float dof_intensity = float(packed >> 1u) * 0.001f;

  float4 center_color = TEX_COLOR.SampleLevel(SAMP_COLOR, float3(uv, 0.0f), 0.0f);
  if (dof_intensity <= 0.001f) {
    return FinalizeOutput(center_color);
  }

  float raw_depth_c = TEX_DEPTH.SampleLevel(SAMP_DEPTH, float3(uv, 0.0f), 0.0f).r;
  float lin_z_c = LinearizeDepth(raw_depth_c);

  if (!use_bokeh) {
    float2 uv_half = uv - 0.5f * xe_c[0].xy;
#if MAJIN_DOF_PASS == 0
    float4 blur1 = TEX_BLUR1.SampleLevel(SAMP_BLUR1, float3(uv, 0.0f), 0.0f);
#else
    float4 blur1 = TEX_BLUR1.SampleLevel(SAMP_BLUR1, float3(uv_half, 0.0f), 0.0f);
#endif
    float4 blur2 = TEX_BLUR2.SampleLevel(SAMP_BLUR2, float3(uv_half, 0.0f), 0.0f);

    float coc_norm = abs(ComputeSignedCoC(lin_z_c, true));
    float coc = clamp(coc_norm * 2.0f * dof_intensity, 0.0f, 2.0f);
    float t0 = min(coc, 1.0f);
    float t1 = max(coc - 1.0f, 0.0f);
    float4 cA = lerp(center_color, blur1, t0);
    float4 cB = lerp(blur1, blur2, t1);
    float4 native_out = lerp(cA, cB, 0.5f * coc);
    return FinalizeOutput(native_out);
  }

  uint tex_w = 1u, tex_h = 1u, tex_els = 1u;
  TEX_COLOR.GetDimensions(tex_w, tex_h, tex_els);
  float2 tex_size = float2(max(tex_w, 1u), max(tex_h, 1u));
  float2 inv_tex_size = 1.0f / tex_size;
  float2 half_texel = 0.5f * inv_tex_size;

  float guest_step_px = max(floor(tex_size.y / 720.0f + 0.5f), 1.0f);
  float2 d_step = inv_tex_size * guest_step_px;

  float raw_n = TEX_DEPTH.SampleLevel(SAMP_DEPTH, float3(clamp(uv + float2(0.0f, -d_step.y), half_texel, 1.0f - half_texel), 0.0f), 0.0f).r;
  float raw_s = TEX_DEPTH.SampleLevel(SAMP_DEPTH, float3(clamp(uv + float2(0.0f,  d_step.y), half_texel, 1.0f - half_texel), 0.0f), 0.0f).r;
  float raw_e = TEX_DEPTH.SampleLevel(SAMP_DEPTH, float3(clamp(uv + float2( d_step.x, 0.0f), half_texel, 1.0f - half_texel), 0.0f), 0.0f).r;
  float raw_w = TEX_DEPTH.SampleLevel(SAMP_DEPTH, float3(clamp(uv + float2(-d_step.x, 0.0f), half_texel, 1.0f - half_texel), 0.0f), 0.0f).r;

  float d_scale = max(min(raw_depth_c, 1.0f - raw_depth_c), 0.0005f);
  float dither_osc = SampleCanopyDepthReversal(
      uv, d_step, half_texel, raw_depth_c, raw_n, raw_s, raw_e, raw_w, d_scale);
  float dither_conf = smoothstep(0.004f, 0.020f, dither_osc);

  float lin_z_n = LinearizeDepth(raw_n);
  float lin_z_s = LinearizeDepth(raw_s);
  float lin_z_e = LinearizeDepth(raw_e);
  float lin_z_w = LinearizeDepth(raw_w);

  float coc_c = ComputeSignedCoC(lin_z_c, false);
  float coc_n = ComputeSignedCoC(lin_z_n, false);
  float coc_s = ComputeSignedCoC(lin_z_s, false);
  float coc_e = ComputeSignedCoC(lin_z_e, false);
  float coc_w = ComputeSignedCoC(lin_z_w, false);

  float avg_coc = 0.2f * (coc_c + coc_n + coc_s + coc_e + coc_w);
  float min_near_coc = min(coc_c, min(min(coc_n, coc_s), min(coc_e, coc_w)));

  float effective_coc = lerp(coc_c, avg_coc, dither_conf);
  float center_abs_coc = abs(effective_coc);
  float near_bleed_coc = max(-min_near_coc, 0.0f);

  float max_radius_v = 0.0115f * dof_intensity;
  float gather_coc = max(center_abs_coc, near_bleed_coc);

  if (gather_coc < 0.004f && dither_conf <= 0.001f) {
    return FinalizeOutput(center_color);
  }

  float2 uv_radius_max = float2(max_radius_v * (tex_size.y / tex_size.x), max_radius_v);
  if (gather_coc < 0.35f) {
    float2 probe_r = uv_radius_max * 0.65f;
    float p_d0 = LinearizeDepth(TEX_DEPTH.SampleLevel(SAMP_DEPTH, float3(clamp(uv + float2( probe_r.x, 0.0f), half_texel, 1.0f - half_texel), 0.0f), 0.0f).r);
    float p_d1 = LinearizeDepth(TEX_DEPTH.SampleLevel(SAMP_DEPTH, float3(clamp(uv + float2(-probe_r.x, 0.0f), half_texel, 1.0f - half_texel), 0.0f), 0.0f).r);
    float p_d2 = LinearizeDepth(TEX_DEPTH.SampleLevel(SAMP_DEPTH, float3(clamp(uv + float2(0.0f,  probe_r.y), half_texel, 1.0f - half_texel), 0.0f), 0.0f).r);
    float p_d3 = LinearizeDepth(TEX_DEPTH.SampleLevel(SAMP_DEPTH, float3(clamp(uv + float2(0.0f, -probe_r.y), half_texel, 1.0f - half_texel), 0.0f), 0.0f).r);
    float p_near = max(max(-ComputeSignedCoC(p_d0, false), -ComputeSignedCoC(p_d1, false)),
                       max(-ComputeSignedCoC(p_d2, false), -ComputeSignedCoC(p_d3, false)));
    gather_coc = max(gather_coc, p_near);
  }

  if (gather_coc < 0.004f) {
    return FinalizeOutput(center_color);
  }

  float avg_z = 0.2f * (lin_z_c + lin_z_n + lin_z_s + lin_z_e + lin_z_w);
  float ref_z = lerp(lin_z_c, avg_z, dither_conf);

  float angle_offset = InterleavedGradientNoise(xe_pos.xy) * 6.2831853f;
  float2 disc_scale = uv_radius_max * gather_coc;

  float bayer_w = dither_conf * smoothstep(0.05f, 0.35f, gather_coc) * smoothstep(0.0f, 0.08f, dof_intensity);
  disc_scale = lerp(disc_scale, max(disc_scale, d_step * 3.25f), bayer_w);

  float3 clean_center_rgb = center_color.rgb;
  if (dither_conf > 0.001f) {
    float3 cN = TEX_COLOR.SampleLevel(SAMP_COLOR, float3(clamp(uv + float2(0.0f, -d_step.y), half_texel, 1.0f - half_texel), 0.0f), 0.0f).rgb;
    float3 cS = TEX_COLOR.SampleLevel(SAMP_COLOR, float3(clamp(uv + float2(0.0f,  d_step.y), half_texel, 1.0f - half_texel), 0.0f), 0.0f).rgb;
    float3 cE = TEX_COLOR.SampleLevel(SAMP_COLOR, float3(clamp(uv + float2( d_step.x, 0.0f), half_texel, 1.0f - half_texel), 0.0f), 0.0f).rgb;
    float3 cW = TEX_COLOR.SampleLevel(SAMP_COLOR, float3(clamp(uv + float2(-d_step.x, 0.0f), half_texel, 1.0f - half_texel), 0.0f), 0.0f).rgb;
    float3 card_avg = 0.25f * (cN + cS + cE + cW);
    clean_center_rgb = lerp(center_color.rgb, card_avg, dither_conf);
  }

  float luma_c = dot(center_color.rgb, kLumaWeights);
  float center_w = (1.0f - bayer_w) * (1.0f + 0.25f * smoothstep(0.55f, 1.8f, luma_c) * center_abs_coc);
  float3 accum_rgb = clean_center_rgb * center_w;
  float accum_w = center_w;
  float inner_r_t = 0.18f * bayer_w;

  [unroll]
  for (int i = 0; i < kBokehTaps; ++i) {
    float t = (float(i) + 0.5f) / float(kBokehTaps);
    float r = sqrt(lerp(inner_r_t, 1.0f, t));
    float theta = float(i) * kGoldenAngle + angle_offset;
    float s_sin, s_cos;
    sincos(theta, s_sin, s_cos);
    float2 dir = float2(s_cos, s_sin);
    float2 sample_uv = clamp(uv + dir * (r * disc_scale), half_texel, 1.0f - half_texel);

    float3 s_rgb = TEX_COLOR.SampleLevel(SAMP_BLUR1, float3(sample_uv, 0.0f), 0.0f).rgb;
    float s_raw_d = TEX_DEPTH.SampleLevel(SAMP_DEPTH, float3(sample_uv, 0.0f), 0.0f).r;
    float s_z = LinearizeDepth(s_raw_d);
    float s_signed_coc = ComputeSignedCoC(s_z, false);
    float s_abs_coc = abs(s_signed_coc);

    float w = 1.0f;
    if (s_z < ref_z * 0.96f) {
      if (s_signed_coc < -0.01f) {
        float reach = r * gather_coc;
        w = smoothstep(reach * 0.55f, reach * 1.05f + 0.04f, s_abs_coc);
      } else {
        float occluder_sharpness =
            saturate((center_abs_coc - s_abs_coc) / max(center_abs_coc, 0.08f));
        w = 1.0f - occluder_sharpness;
      }
    } else if (effective_coc < -0.02f) {
      w = lerp(0.65f, 1.0f, smoothstep(0.0f, 0.35f, s_abs_coc));
    }

    float s_luma = dot(s_rgb, kLumaWeights);
    float bokeh_boost = 1.0f + 0.35f * smoothstep(0.55f, 1.8f, s_luma) * s_abs_coc;
    w *= bokeh_boost;

    accum_rgb += s_rgb * w;
    accum_w += w;
  }

  float3 bokeh_rgb = accum_rgb / max(accum_w, 1e-4f);

  float blend_t = smoothstep(0.004f, 0.08f, gather_coc);
  float3 final_rgb = lerp(clean_center_rgb, bokeh_rgb, blend_t);
  return FinalizeOutput(float4(final_rgb, center_color.a));
}
)";

  return header + kBody;
}

}  // namespace rex::graphics::majin
