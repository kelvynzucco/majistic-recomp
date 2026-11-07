"""Map exact address deltas between PAL (0.0.0.3) and USA (0.0.0.2) across .text."""
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
TEXT_BASE = 0x82150000

pdata_starts = set()
for off in range(0, len(pdata), 8):
    begin, _ = struct.unpack_from(">II", pdata, off)
    if begin:
        pdata_starts.add(begin)

# Collect all 4-insn thunks (Layer 1 + Layer 2) in USA
usa_thunks = {}
for off in range(0, len(text) - 15, 4):
    a, b, c, d = struct.unpack_from(">4I", text, off)
    table, target = (a >> 21) & 31, (b >> 21) & 31
    if table in range(9, 13) and target in range(9, 13):
        if a == (0x80030000 | (table << 21)) and (b >> 16) == (0x8000 | (target << 5) | table):
            slot = b & 0xFFFF
            if slot < 0x8000 and slot % 4 == 0 and c == (0x7C0903A6 | (target << 21)) and d == 0x4E800420:
                usa_thunks[TEXT_BASE + off] = (a, b, c, d)

pal_thunks_txt = (REF_PROTO / "dispatch-thunks.toml").read_text()
pal_vars_txt = (REF_PROTO / "dispatch-variants.toml").read_text()
pal_addrs = sorted(
    [int(x, 16) for x in re.findall(r"^0x([0-9A-Fa-f]{8})", pal_thunks_txt, re.M)]
    + [int(x, 16) for x in re.findall(r"^0x([0-9A-Fa-f]{8})", pal_vars_txt, re.M)]
)

print(f"Total PAL thunks + variants to probe: {len(pal_addrs)}")
transitions = []
last_d = object()
matched = 0
for p in pal_addrs:
    found_d = None
    for d in range(0, -0x200, -4):
        if (p + d) in usa_thunks:
            found_d = d
            break
    if found_d is None:
        for d in range(4, 0x200, 4):
            if (p + d) in usa_thunks:
                found_d = d
                break
    if found_d is not None:
        matched += 1
    if found_d != last_d:
        u_str = f"0x{p + found_d:08X}" if found_d is not None else "NONE"
        d_str = f"{found_d:+d} (0x{found_d:+X})" if found_d is not None else "NONE"
        print(f"  PAL 0x{p:08X} -> USA {u_str} | delta = {d_str}")
        transitions.append((p, found_d))
        last_d = found_d

print(f"Matched {matched}/{len(pal_addrs)} PAL thunks!")
