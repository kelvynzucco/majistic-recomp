"""Static CPU & Vtable scanner for Majin and the Forsaken Kingdom (USA 0.0.0.2)."""
import json
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PRIV = ROOT / "prototype-usa" / "private"
OUT_PROTOTYPE = ROOT / "prototype-usa"
REPORTS = ROOT / "reports"

pdata = (PRIV / "loaded-pdata.bin").read_bytes()
text = (PRIV / "loaded-text.bin").read_bytes()
rdata = (PRIV / "loaded-rdata.bin").read_bytes()

TEXT_BASE = 0x82150000
TEXT_END = TEXT_BASE + len(text)
RDATA_BASE = 0x82000600

# 1. Parse .pdata function intervals
pdata_starts = set()
pdata_intervals = []
for off in range(0, len(pdata), 8):
    begin, info = struct.unpack_from(">II", pdata, off)
    if begin == 0:
        continue
    func_len = ((info >> 8) & 0x3FFFFF) * 4
    pdata_starts.add(begin)
    pdata_intervals.append((begin, begin + func_len))

pdata_intervals.sort()


def find_enclosing_pdata(addr: int):
    # Binary search in sorted intervals
    lo, hi = 0, len(pdata_intervals) - 1
    while lo <= hi:
        mid = (lo + hi) // 2
        b, e = pdata_intervals[mid]
        if addr < b:
            hi = mid - 1
        elif addr >= e:
            lo = mid + 1
        else:
            return (b, e)
    return None


# 2. Scan direct bl targets in .text
bl_targets = set()
for off in range(0, len(text), 4):
    insn = struct.unpack_from(">I", text, off)[0]
    if (insn >> 26) == 18 and (insn & 3) == 1:  # bl (AA=0, LK=1)
        li = insn & 0x03FFFFFC
        if li & 0x02000000:
            li -= 0x04000000
        target = (TEXT_BASE + off + li) & 0xFFFFFFFF
        if TEXT_BASE <= target < TEXT_END:
            bl_targets.add(target)

known_seed = pdata_starts | bl_targets | {0x8267C090}

# 3. Layer 1: Exact 4-instruction virtual dispatch thunks (lwz r12/r11,0(r3); lwz r11,slot(r12/r11); mtctr r11; bctr)
layer1_all = []
layer1_missing = []
for off in range(0, len(text) - 15, 4):
    a, b, c, d = struct.unpack_from(">4I", text, off)
    valid_loads = (a == 0x81830000 and (b >> 16) == 0x816C) or (
        a == 0x81630000 and (b >> 16) == 0x816B
    )
    slot = b & 0xFFFF
    if valid_loads and slot % 4 == 0 and slot < 0x8000 and c == 0x7D6903A6 and d == 0x4E800420:
        addr = TEXT_BASE + off
        enclosing = find_enclosing_pdata(addr)
        in_seed = addr in known_seed
        entry = {
            "address": f"0x{addr:08X}",
            "addr_int": addr,
            "slot_offset": slot,
            "in_seed": in_seed,
            "enclosing_pdata": f"0x{enclosing[0]:08X}..0x{enclosing[1]:08X}" if enclosing else None,
        }
        layer1_all.append(entry)
        if not in_seed and enclosing is None:
            layer1_missing.append(entry)

layer1_addrs = {x["addr_int"] for x in layer1_all}

# 4. Layer 2: Volatile r9-r12 variants (excluding Layer 1 and internal .pdata cases)
layer2_all = []
layer2_missing = []
layer2_internal_deferred = []
for off in range(0, len(text) - 15, 4):
    a, b, c, d = struct.unpack_from(">4I", text, off)
    table, target = (a >> 21) & 31, (b >> 21) & 31
    if table not in range(9, 13) or target not in range(9, 13):
        continue
    if a != (0x80030000 | (table << 21)):
        continue
    if (b >> 16) != (0x8000 | (target << 5) | table):
        continue
    slot = b & 0xFFFF
    if slot >= 0x8000 or slot % 4 != 0:
        continue
    if c != (0x7C0903A6 | (target << 21)) or d != 0x4E800420:
        continue
    addr = TEXT_BASE + off
    if addr in layer1_addrs:
        continue
    enclosing = find_enclosing_pdata(addr)
    in_seed = addr in known_seed
    entry = {
        "address": f"0x{addr:08X}",
        "addr_int": addr,
        "table_reg": table,
        "target_reg": target,
        "slot_offset": slot,
        "in_seed": in_seed,
        "enclosing_pdata": f"0x{enclosing[0]:08X}..0x{enclosing[1]:08X}" if enclosing else None,
    }
    layer2_all.append(entry)
    if enclosing is not None and enclosing[0] != addr:
        layer2_internal_deferred.append(entry)
    elif not in_seed:
        layer2_missing.append(entry)

# 5. Layer 3: 5-instruction member-object dispatch thunks
layer3_all = []
layer3_missing = []
for off in range(0, len(text) - 23, 4):
    a, b, c, d, e, padding = struct.unpack_from(">6I", text, off)
    if (a >> 16) != 0x8063 or b != 0x81630000:
        continue
    if not (((c >> 16) == 0x814B and d == 0x7D4903A6) or ((c >> 16) == 0x816B and d == 0x7D6903A6)):
        continue
    member, slot = a & 0xFFFF, c & 0xFFFF
    if member >= 0x8000 or member % 4 != 0 or slot >= 0x8000 or slot % 4 != 0:
        continue
    if e != 0x4E800420 or padding != 0:
        continue
    addr = TEXT_BASE + off
    enclosing = find_enclosing_pdata(addr)
    in_seed = addr in known_seed
    entry = {
        "address": f"0x{addr:08X}",
        "addr_int": addr,
        "member_offset": member,
        "slot_offset": slot,
        "in_seed": in_seed,
        "enclosing_pdata": f"0x{enclosing[0]:08X}..0x{enclosing[1]:08X}" if enclosing else None,
    }
    layer3_all.append(entry)
    if not in_seed and enclosing is None:
        layer3_missing.append(entry)

summary = {
    "pdata_function_count": len(pdata_starts),
    "direct_bl_targets": len(bl_targets),
    "combined_seed_count": len(known_seed),
    "layer1_standard_thunks_total": len(layer1_all),
    "layer1_missing_from_seed": len(layer1_missing),
    "layer2_variants_total": len(layer2_all),
    "layer2_missing_from_seed": len(layer2_missing),
    "layer2_internal_deferred": layer2_internal_deferred,
    "layer3_member_thunks_total": len(layer3_all),
    "layer3_entries": layer3_all,
}

(REPORTS / "usa-thunk-scan-summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
print(json.dumps(summary, indent=2))
