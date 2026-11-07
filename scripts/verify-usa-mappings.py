"""Verify all 32 analysis-overrides, timing hooks, and save hooks in USA (0.0.0.2)."""
import json
import re
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PRIV = ROOT / "prototype-usa" / "private"
REF_PROTO = ROOT / "prototype"
REPORTS = ROOT / "reports"

text = (PRIV / "loaded-text.bin").read_bytes()
pdata = (PRIV / "loaded-pdata.bin").read_bytes()
rdata = (PRIV / "loaded-rdata.bin").read_bytes()
TEXT_BASE = 0x82150000
RDATA_BASE = 0x82000600

pdata_starts = set()
for off in range(0, len(pdata), 8):
    begin, _ = struct.unpack_from(">II", pdata, off)
    if begin:
        pdata_starts.add(begin)


def disasm_words(va, count=5):
    off = va - TEXT_BASE
    return [f"{w:08X}" for w in struct.unpack_from(f">{count}I", text, off)]


ov_txt = (REF_PROTO / "analysis-overrides.toml").read_text()
ov_addrs = [int(x, 16) for x in re.findall(r"^0x([0-9A-Fa-f]{8})", ov_txt, re.M)]

print(f"=== Verifying {len(ov_addrs)} PAL analysis-overrides.toml at USA = PAL - 0x70 ===")
ov_results = []
for p in ov_addrs:
    u = p - 0x70
    prev_w = disasm_words(u - 4, 1)[0]
    words = disasm_words(u, 5)
    # Check if prev_w is a function terminator (blr=4E800020, bctr=4E800420, b=48xxxxxx, or 00000000 padding)
    is_boundary = (
        prev_w in ("4E800020", "4E800420", "00000000")
        or (int(prev_w, 16) >> 26 == 18 and (int(prev_w, 16) & 3) == 0)
    )
    print(
        f"  PAL 0x{p:08X} -> USA 0x{u:08X} | boundary={str(is_boundary):5s} (prev={prev_w}) | insns={' '.join(words)}"
    )
    ov_results.append(
        {
            "pal": f"0x{p:08X}",
            "usa": f"0x{u:08X}",
            "boundary_ok": is_boundary,
            "prev_opcode": prev_w,
            "opcodes": words,
        }
    )

print("\n=== Finding exact split point between delta=0 and delta=-0x70 ===")
# Check where in 0x821B2C18 .. 0x821F506C the 0x70 shift happens
# Let's check SceneTitleTitle (PAL 0x821CC390) and save caller (PAL 0x821B9DF4)
probes = [
    ("timing: sub_82162238", 0x82162238, 0),
    ("timing: caller 0x821616EC", 0x821616EC, 0),
    ("save: caller 0x821B9DF4 (d=0)", 0x821B9DF4, 0),
    ("save: caller 0x821B9DF4 (d=-0x70)", 0x821B9DF4, -0x70),
    ("timing: sub_821CC390 (d=0)", 0x821CC390, 0),
    ("timing: sub_821CC390 (d=-0x70)", 0x821CC390, -0x70),
    ("save: ContentCreateEx 0x8267AFE0", 0x8267AFE0, -0x70),
    ("save: ContentClose 0x8267B070", 0x8267B070, -0x70),
    ("save: ContentSetThumbnail 0x8267B078", 0x8267B078, -0x70),
    ("save: GetLastError 0x8267B190", 0x8267B190, -0x70),
    ("save: GetOverlappedResult 0x8267B510", 0x8267B510, -0x70),
    ("save: WriteFile 0x8267B6C8", 0x8267B6C8, -0x70),
    ("save: SetLastNtError 0x8267D3E0", 0x8267D3E0, -0x70),
]
for label, p, d in probes:
    u = p + d
    in_p = u in pdata_starts
    w = disasm_words(u, 4)
    print(f"  {label:36s} -> USA 0x{u:08X} | in_pdata={str(in_p):5s} | {' '.join(w)}")

print("\n=== Checking SceneTitleTitle vtable in USA .text ===")
for va in range(0x821CB000, 0x821CD000, 4):
    off = va - TEXT_BASE
    w = struct.unpack_from(">I", text, off)[0]
    if (w >> 26) == 15 and (w & 0xFFFE) == 0x820E:
        print(f"  At USA 0x{va:08X}: {' '.join(disasm_words(va, 6))}")

