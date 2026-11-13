#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <map>
#include <set>
#include <fstream>
#include <filesystem>
#include <cstring>
#include <cstdint>

#define XXH_INLINE_ALL
#include "xxhash.h"

namespace fs = std::filesystem;

#pragma pack(push, 1)
struct XshHeader {
  uint32_t magic;           // 0x48534558 ('XESH')
  uint32_t version_swapped; // 0x19122020 for BE 0x20201219
};

struct XshRecordHeader {
  uint64_t ucode_data_hash;
  uint32_t countType;       // ucode_dword_count : 31, type : 1 (bit 31)
};

enum class PipelineGeometryShader : uint32_t {
  kNone,
  kPointList,
  kRectangleList,
  kQuadList,
};

enum class PipelinePrimitiveTopology : uint32_t {
  kPointList,
  kLineList,
  kLineStrip,
  kTriangleList,
  kTriangleStrip,
  kTriangleFan,
  kLineListWithAdjacency,
  kPatchList,
};

enum class PipelinePolygonMode : uint32_t {
  kFill,
  kLine,
  kPoint,
};

enum class PipelineBlendFactor : uint32_t {
  kZero,
  kOne,
  kSrcColor,
  kOneMinusSrcColor,
  kDstColor,
  kOneMinusDstColor,
  kSrcAlpha,
  kOneMinusSrcAlpha,
  kDstAlpha,
  kOneMinusDstAlpha,
  kConstantColor,
  kOneMinusConstantColor,
  kConstantAlpha,
  kOneMinusConstantAlpha,
  kSrcAlphaSaturate,
};

struct PipelineRenderTarget {
  PipelineBlendFactor src_color_blend_factor : 4;  // 4
  PipelineBlendFactor dst_color_blend_factor : 4;  // 8
  uint32_t color_blend_op : 3;                     // 11 (BlendOp)
  PipelineBlendFactor src_alpha_blend_factor : 4;  // 15
  PipelineBlendFactor dst_alpha_blend_factor : 4;  // 19
  uint32_t alpha_blend_op : 3;                     // 22 (BlendOp)
  uint32_t color_write_mask : 4;                   // 26
  uint32_t padding : 6;                            // 32
};

struct PipelineDescription {
  uint64_t vertex_shader_hash;                     // 8
  uint64_t vertex_shader_modification;             // 8
  uint64_t pixel_shader_hash;                      // 8
  uint64_t pixel_shader_modification;              // 8
  uint32_t render_pass_key;                        // 4

  // Word 0 (32 bits)
  PipelineGeometryShader geometry_shader : 2;      // 2
  PipelinePrimitiveTopology primitive_topology : 3; // 5
  uint32_t primitive_restart : 1;                  // 6
  uint32_t tessellation_mode : 2;                  // 8
  uint32_t depth_clamp_enable : 1;                 // 9
  PipelinePolygonMode polygon_mode : 2;            // 11
  uint32_t cull_front : 1;                         // 12
  uint32_t cull_back : 1;                          // 13
  uint32_t front_face_clockwise : 1;               // 14
  uint32_t rasterizer_discard : 1;                 // 15
  uint32_t depth_write_enable : 1;                 // 16
  uint32_t depth_compare_op : 3;                   // 19 (CompareFunction)
  uint32_t stencil_test_enable : 1;                // 20
  uint32_t stencil_front_fail_op : 3;              // 23 (StencilOp)
  uint32_t stencil_front_pass_op : 3;              // 26 (StencilOp)
  uint32_t stencil_front_depth_fail_op : 3;        // 29 (StencilOp)
  uint32_t stencil_front_compare_op : 3;           // 32 (CompareFunction)

  // Word 1 (32 bits)
  uint32_t stencil_back_fail_op : 3;               // 3 (StencilOp)
  uint32_t stencil_back_pass_op : 3;               // 6 (StencilOp)
  uint32_t stencil_back_depth_fail_op : 3;         // 9 (StencilOp)
  uint32_t stencil_back_compare_op : 3;            // 12 (CompareFunction)
  uint32_t sample_rate_shading : 1;                // 13
  uint32_t padding0 : 19;                          // 32

  PipelineRenderTarget render_targets[4];          // 16 bytes

  uint64_t GetHash() const {
    return XXH3_64bits(this, sizeof(*this));
  }
};
static_assert(sizeof(PipelineDescription) == 60, "PipelineDescription must be 60 bytes");

struct PipelineStoredDescription {
  uint64_t description_hash;
  PipelineDescription description;
};
static_assert(sizeof(PipelineStoredDescription) == 68, "PipelineStoredDescription must be 68 bytes");
#pragma pack(pop)

struct GuestShaderRecord {
  uint64_t hash = 0;
  uint32_t countType = 0;
  std::vector<uint32_t> ucode;
};

static PipelineDescription NormalizeEDS(const PipelineDescription& src) {
  PipelineDescription desc = src;

  // In RexGlue with Vulkan EDS 1, 2, and 3:
  // Word 0: Only non-dynamic bits are preserved (mask 0x47DF)
  uint32_t* w0_ptr = reinterpret_cast<uint32_t*>(reinterpret_cast<char*>(&desc) + 36);
  *w0_ptr &= 0x47DF;

  // Word 1: Stencil and dynamic state bits are cleared to 0
  uint32_t* w1_ptr = reinterpret_cast<uint32_t*>(reinterpret_cast<char*>(&desc) + 40);
  *w1_ptr = 0;

  // Render targets: All 4 attachments are cleared to 0 under EDS3 dynamic blend/color mask
  std::memset(desc.render_targets, 0, sizeof(desc.render_targets));

  return desc;
}

int main(int argc, char** argv) {
  std::cout << "=========================================================\n";
  std::cout << "  Majin Recomp - Shader Cache Consolidation / Merger    \n";
  std::cout << "=========================================================\n";

  fs::path rel_xsh = "game-data/runtime/cache/vulkan-vulkan/release/shaders/shareable/4E4D0816.xsh.bak";
  fs::path rel_xpso = "game-data/runtime/cache/vulkan-vulkan/release/shaders/shareable/4E4D0816.fbo.vk.xpso.bak";
  fs::path exp_xsh = "game-data/runtime/cold_driver_exp/cache/shaders/shareable/4E4D0816.xsh";
  fs::path exp_xpso = "game-data/runtime/cold_driver_exp/cache/shaders/shareable/4E4D0816.fbo.vk.xpso";
  fs::path out_xsh = "game-data/runtime/cache/vulkan-vulkan/release/shaders/shareable/4E4D0816.xsh";
  fs::path out_xpso = "game-data/runtime/cache/vulkan-vulkan/release/shaders/shareable/4E4D0816.fbo.vk.xpso";

  // 1. Load and merge .xsh files
  std::map<uint64_t, GuestShaderRecord> merged_shaders;

  auto load_xsh = [&](const fs::path& p, const std::string& label) {
    std::ifstream f(p, std::ios::binary);
    if (!f) {
      std::cerr << "Failed to open " << label << " at " << p << "\n";
      return;
    }
    XshHeader hdr;
    f.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
    if (hdr.magic != 0x48534558) {
      std::cerr << "Invalid XSH magic in " << p << "\n";
      return;
    }
    size_t count = 0;
    while (f) {
      XshRecordHeader r;
      f.read(reinterpret_cast<char*>(&r), sizeof(r));
      if (!f) break;
      uint32_t dwords = r.countType & 0x7FFFFFFF;
      GuestShaderRecord s;
      s.hash = r.ucode_data_hash;
      s.countType = r.countType;
      s.ucode.resize(dwords);
      f.read(reinterpret_cast<char*>(s.ucode.data()), dwords * sizeof(uint32_t));
      if (!f) break;
      if (merged_shaders.find(s.hash) == merged_shaders.end()) {
        merged_shaders[s.hash] = std::move(s);
      }
      count++;
    }
    std::cout << "[" << label << "] Loaded " << count << " shaders from " << p.filename() << "\n";
  };

  load_xsh(rel_xsh, "Release XSH");
  load_xsh(exp_xsh, "Cold Exp XSH");
  std::cout << "[Merged XSH] Total unique guest shaders: " << merged_shaders.size() << "\n\n";

  // 2. Load and merge .xpso files
  std::map<std::string, PipelineStoredDescription> merged_pipelines;

  // Load Release XPSO (already EDS 0x20260301)
  {
    std::ifstream f(rel_xpso, std::ios::binary);
    if (f) {
      f.seekg(20, std::ios::beg);
      size_t count = 0;
      size_t verified = 0;
      while (f) {
        PipelineStoredDescription rec;
        f.read(reinterpret_cast<char*>(&rec), sizeof(rec));
        if (!f) break;
        uint64_t computed = rec.description.GetHash();
        if (computed == rec.description_hash) {
          verified++;
        }
        std::string desc_key(reinterpret_cast<const char*>(&rec.description), sizeof(rec.description));
        merged_pipelines[desc_key] = rec;
        count++;
      }
      std::cout << "[Release XPSO] Loaded " << count << " pipelines (Verified: " << verified << ")\n";
    }
  }

  // Load Cold Exp XPSO (pre-EDS 0x20260228 -> normalize with EDS)
  {
    std::ifstream f(exp_xpso, std::ios::binary);
    if (f) {
      f.seekg(20, std::ios::beg);
      size_t raw_count = 0;
      size_t added_count = 0;
      size_t matched_count = 0;
      while (f) {
        PipelineStoredDescription raw_rec;
        f.read(reinterpret_cast<char*>(&raw_rec), sizeof(raw_rec));
        if (!f) break;
        raw_count++;

        PipelineDescription norm = NormalizeEDS(raw_rec.description);
        std::string desc_key(reinterpret_cast<const char*>(&norm), sizeof(norm));

        if (merged_pipelines.find(desc_key) == merged_pipelines.end()) {
          PipelineStoredDescription new_rec;
          new_rec.description = norm;
          new_rec.description_hash = norm.GetHash();
          merged_pipelines[desc_key] = new_rec;
          added_count++;
        } else {
          matched_count++;
        }
      }
      std::cout << "[Cold Exp XPSO] Loaded " << raw_count << " raw pipelines -> Matched existing: " << matched_count << ", Added new: " << added_count << "\n";
    }
  }

  std::cout << "[Merged XPSO] Total unique post-EDS pipelines: " << merged_pipelines.size() << "\n\n";

  // 3. Write merged .xsh
  {
    std::ofstream out(out_xsh, std::ios::binary | std::ios::trunc);
    if (!out) {
      std::cerr << "FATAL: Could not open " << out_xsh << " for writing\n";
      return 1;
    }
    XshHeader hdr;
    hdr.magic = 0x48534558;
    hdr.version_swapped = 0x19122020; // 0x20201219 in BE
    out.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));

    for (const auto& [hash, s] : merged_shaders) {
      XshRecordHeader r;
      r.ucode_data_hash = s.hash;
      r.countType = s.countType;
      out.write(reinterpret_cast<const char*>(&r), sizeof(r));
      out.write(reinterpret_cast<const char*>(s.ucode.data()), s.ucode.size() * sizeof(uint32_t));
    }
    std::cout << "[Output] Written merged XSH: " << out_xsh << " (" << fs::file_size(out_xsh) << " bytes)\n";
  }

  // 4. Write merged .xpso
  {
    std::ofstream out(out_xpso, std::ios::binary | std::ios::trunc);
    if (!out) {
      std::cerr << "FATAL: Could not open " << out_xpso << " for writing\n";
      return 1;
    }
    // 20-byte Header:
    // uint32_t magic = 0x53504558 ('XPES') LE
    // uint32_t api = 0 (Vulkan) LE
    // uint32_t format = 2 BE (0x02000000 in memory)
    // uint32_t desc_version = 0x20260301 BE (0x01032620 in memory)
    // uint32_t mod_version = 0x20261018 BE (0x18102620 in memory)
    uint8_t hdr[20];
    *reinterpret_cast<uint32_t*>(&hdr[0]) = 0x53504558;
    *reinterpret_cast<uint32_t*>(&hdr[4]) = 0;
    // Big endian values:
    hdr[8] = 0; hdr[9] = 0; hdr[10] = 0; hdr[11] = 2; // format 2
    hdr[12] = 0x20; hdr[13] = 0x26; hdr[14] = 0x03; hdr[15] = 0x01; // descVer 0x20260301
    hdr[16] = 0x20; hdr[17] = 0x26; hdr[18] = 0x10; hdr[19] = 0x18; // modVer 0x20261018
    out.write(reinterpret_cast<const char*>(hdr), sizeof(hdr));

    for (const auto& [desc_key, rec] : merged_pipelines) {
      out.write(reinterpret_cast<const char*>(&rec), sizeof(rec));
    }
    std::cout << "[Output] Written merged XPSO: " << out_xpso << " (" << fs::file_size(out_xpso) << " bytes)\n";
  }

  std::cout << "\nSUCCESS: Shader cache merge completed successfully!\n";
  return 0;
}
