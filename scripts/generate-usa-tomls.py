"""Generate and byte-verify all 4 USA .toml configuration files and majin_manifest.toml."""
import json
import re
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PRIV = ROOT / "prototype-usa" / "private"
REF_PROTO = ROOT / "prototype"
OUT_PROTO = ROOT / "prototype-usa"
REPORTS = ROOT / "reports"

text = (PRIV / "loaded-text.bin").read_bytes()
TEXT_BASE = 0x82150000

SPLIT_VA = 0x821C0000  # Exact transition region between delta=0 and delta=-0x70


def pal_to_usa(pal_va: int) -> int:
    return pal_va if pal_va < SPLIT_VA else (pal_va - 0x70)


def read_words(va: int, n: int = 4):
    off = va - TEXT_BASE
    return struct.unpack_from(f">{n}I", text, off)


# 1. Translate and verify dispatch-thunks.toml
pal_dt_lines = (REF_PROTO / "dispatch-thunks.toml").read_text(encoding="utf-8").splitlines()
usa_dt_lines = []
dt_verified = 0
for line in pal_dt_lines:
    m = re.match(r"^0x([0-9A-Fa-f]{8})\s*=\s*\{\s*size\s*=\s*(\d+)\s*\}", line)
    if m:
        p_va = int(m.group(1), 16)
        sz = int(m.group(2))
        u_va = pal_to_usa(p_va)
        a, b, c, d = read_words(u_va, 4)
        assert c == 0x7D6903A6 and d == 0x4E800420, f"Invalid thunk at USA 0x{u_va:08X}"
        usa_dt_lines.append(f"0x{u_va:08X} = {{ size = {sz} }}")
        dt_verified += 1
    else:
        usa_dt_lines.append(line)

(OUT_PROTO / "dispatch-thunks.toml").write_text("\n".join(usa_dt_lines) + "\n", encoding="utf-8")

# 2. Translate and verify dispatch-variants.toml
pal_dv_lines = (REF_PROTO / "dispatch-variants.toml").read_text(encoding="utf-8").splitlines()
usa_dv_lines = []
dv_verified = 0
for line in pal_dv_lines:
    m = re.match(r"^0x([0-9A-Fa-f]{8})\s*=\s*\{\s*name\s*=\s*\"sub_[0-9A-Fa-f]{8}\"\s*\}", line)
    if m:
        p_va = int(m.group(1), 16)
        u_va = pal_to_usa(p_va)
        a, b, c, d = read_words(u_va, 4)
        assert d == 0x4E800420, f"Invalid variant thunk at USA 0x{u_va:08X}"
        usa_dv_lines.append(f'0x{u_va:08X} = {{ name = "sub_{u_va:08X}" }}')
        dv_verified += 1
    else:
        usa_dv_lines.append(line)

(OUT_PROTO / "dispatch-variants.toml").write_text("\n".join(usa_dv_lines) + "\n", encoding="utf-8")

# 3. Translate and verify member-dispatch-thunks.toml
pal_mt_lines = (REF_PROTO / "member-dispatch-thunks.toml").read_text(encoding="utf-8").splitlines()
usa_mt_lines = []
mt_verified = 0
for line in pal_mt_lines:
    m = re.match(r"^0x([0-9A-Fa-f]{8})\s*=\s*\{\s*name\s*=\s*\"sub_[0-9A-Fa-f]{8}\"\s*\}", line)
    if m:
        p_va = int(m.group(1), 16)
        u_va = pal_to_usa(p_va)
        a, b, c, d, e = read_words(u_va, 5)
        assert e == 0x4E800420, f"Invalid member thunk at USA 0x{u_va:08X}"
        usa_mt_lines.append(f'0x{u_va:08X} = {{ name = "sub_{u_va:08X}" }}')
        mt_verified += 1
    else:
        usa_mt_lines.append(line)

(OUT_PROTO / "member-dispatch-thunks.toml").write_text("\n".join(usa_mt_lines) + "\n", encoding="utf-8")

# 4. Translate and verify analysis-overrides.toml
pal_ao_lines = (REF_PROTO / "analysis-overrides.toml").read_text(encoding="utf-8").splitlines()
usa_ao_lines = []
ao_verified = 0
for line in pal_ao_lines:
    m = re.match(r"^0x([0-9A-Fa-f]{8})\s*=\s*\{\s*name\s*=\s*\"sub_[0-9A-Fa-f]{8}\"\s*\}", line)
    if m:
        p_va = int(m.group(1), 16)
        u_va = pal_to_usa(p_va)
        w = read_words(u_va, 2)
        assert w[0] != 0, f"Zero opcode at USA 0x{u_va:08X}"
        usa_ao_lines.append(f'0x{u_va:08X} = {{ name = "sub_{u_va:08X}" }}')
        ao_verified += 1
    else:
        usa_ao_lines.append(line)

(OUT_PROTO / "analysis-overrides.toml").write_text("\n".join(usa_ao_lines) + "\n", encoding="utf-8")

# 5. Create majin_manifest.toml in prototype-usa/
manifest = """# Majin and the Forsaken Kingdom (USA 0.0.0.2) ReXGlue Manifest

[project]
name = "majin"
sdk_version = "0.10.0"
game_root = "../game-data-usa"

[entrypoint]
file_path = "../game-data-usa/default.xex"
out_directory_path = "generated/default"
includes = ["analysis-overrides.toml", "dispatch-thunks.toml", "member-dispatch-thunks.toml", "dispatch-variants.toml"]
"""
(OUT_PROTO / "majin_manifest.toml").write_text(manifest, encoding="utf-8")

print(
    json.dumps(
        {
            "dispatch_thunks_verified": dt_verified,
            "dispatch_variants_verified": dv_verified,
            "member_thunks_verified": mt_verified,
            "analysis_overrides_verified": ao_verified,
            "total_verified": dt_verified + dv_verified + mt_verified + ao_verified,
        },
        indent=2,
    )
)
