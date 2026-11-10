"""Merge the two existing project libraries; do not inspect any external executable."""
import json
import re
import unicodedata
from pathlib import Path

ROOT = Path(__file__).resolve().parent
DATA = ROOT / "table_qty/assets/data"
INPUT = ROOT / "backup-original/data"
native = json.loads((INPUT / "sections.json").read_text(encoding="utf-8-sig"))
old = json.loads((INPUT / "steeltally_catalog.json").read_text(encoding="utf-8-sig"))
groups = {r["number"]: r for r in native["categorys"]}
families = {
    "HN": "H型钢", "HM": "H型钢", "HW": "H型钢", "I": "工字钢", "CH": "槽钢",
    "LEQ": "角钢", "LNE": "不等边角钢", "PIPE": "圆管", "ROD": "圆钢",
    "BOX": "矩形管", "RECT": "矩形管", "CFC": "C型钢", "CFZ": "Z型钢",
    "T": "T型钢", "PL": "钢板", "CHK": "花纹钢板", "GRATE": "格栅板",
    "FB": "热轧扁钢", "SS": "方钢", "WH": "焊接H型钢", "WB": "焊接箱形", "RAIL": "钢轨",
}

def norm(text):
    return unicodedata.normalize("NFKC", text).upper().replace("×", "X").replace("*", "X").replace(" ", "")

def standard_key(text, fallback):
    m = re.search(r"GB/?T\s*(\d+)[—－-](\d{2,4})", text, re.I)
    return f"GB/T {m[1]}-{m[2]}" if m else fallback

def signature(row):
    s = norm(row["spec"])
    family = row["family"]
    nums = re.findall(r"\d+(?:\.\d+)?", s)
    nums = "X".join(f"{float(v):g}" for v in nums)
    # Prefixes differ between the old HN/HM/HW and the native RH naming.
    if family in {"H型钢", "T型钢", "角钢", "不等边角钢", "矩形管", "圆管", "圆钢", "C型钢", "Z型钢"}:
        s = nums
    # Equal dimensions in different standards/editions remain distinct.
    return family, row["standard"], s

def aliases(family, spec, dimensions=None):
    values = [spec]
    s = norm(spec)
    if family == "H型钢":
        values.append(re.sub(r"^(?:HN|HM|HW|RH)", "H", s))
    elif family == "圆管":
        values.append(re.sub(r"^(?:D|Φ|PHI)", "PIPE", s))
    elif family == "圆钢":
        values.append(re.sub(r"^(?:D|Φ|PHI)", "ROD", s))
    elif family == "矩形管":
        values.append(s.replace("□", "BOX", 1))
    elif family in {"C型钢", "Z型钢"}:
        values.append(re.sub(r"^C", "CZ", s) if family == "C型钢" else re.sub(r"^Z", "ZZ", s))
    elif family == "热轧扁钢" and dimensions:
        values.append(f"FB{dimensions['b']:g}X{dimensions['t']:g}")
    return sorted(set(values))

result = {}
merges = []
for o in native["sections"]:
    group = groups[o["category"]]
    ver = o["version"]
    raw_source = next((s for y, s in zip(group["standards"], group["sources"]) if y == ver), "")
    std = standard_key(raw_source, f"原库计算规则/{group['description']}/{ver}")
    plate = "钢板" in group["description"] or group["description"] == "格栅板"
    r = dict(id=f"native:{o['category']}:{ver}:{o['number']}", spec=o["norm"],
             family=group["description"], standard=std, mass=o.get("WPM", o.get("Weight", -1)), surface=o.get("APM", -1),
             unit="kg/m2" if plate else "kg/m", surface_basis="沿用原型钢库 APM 口径",
             source=raw_source or "原有表格型钢库", parameters=o.get("parameters", ""),
             version=ver, default=ver == group["default"],
             aliases=aliases(group["description"], o["norm"]))
    k = signature(r)
    if k in result:
        prior = result[k]
        merges.append(dict(key=k, retained=prior, removed=r, reason="same native identity"))
        prior["aliases"] = sorted(set(prior["aliases"] + r["aliases"]))
    else:
        result[k] = r

for o in old["items"]:
    m, a = o["mass"], o["surface"] or {}
    r = dict(id="old:" + o["id"], spec=o["name"], family=families.get(o["family"], o["label"]),
             standard=standard_key(o["standard"], o["standard"]), mass=m["value"],
             surface=a.get("value") if a.get("value") is not None else -1,
             unit=m["unit"], surface_basis=a.get("basis") or "not_defined",
             source=m.get("source_ref") or "SteelTally 旧型材库",
             parameters=json.dumps(o["dimensions"], ensure_ascii=False, separators=(",", ":")),
             catalog_version=old["catalog_version"], default=False,
             aliases=aliases(families.get(o["family"], o["label"]), o["name"], o["dimensions"]))
    k = signature(r)
    if k in result:
        prior = result[k]
        # Keep the native edition when the old library disagrees.
        # The old catalog only fills an otherwise unavailable surface coefficient.
        keep_old = prior["surface"] < 0 and r["surface"] >= 0
        kept, removed = (r, prior) if keep_old else (prior, r)
        kept["aliases"] = sorted(set(prior["aliases"] + r["aliases"]))
        kept["default"] = prior["default"]
        merges.append(dict(key=k, retained=kept.copy(), removed=removed,
                           values_differ=abs(kept["mass"] - removed["mass"]) > 1e-9 or abs(kept["surface"] - removed["surface"]) > 1e-9,
                           reason="same family + standard edition + specification"))
        result[k] = kept
    else:
        result[k] = r

rows = list(result.values())
assert len({signature(r) for r in rows}) == len(rows)
merged = dict(schema="steeltally.sections/1", version="1.0.3", records=rows,
              input_count=len(native["sections"]) + len(old["items"]), merged_count=len(merges))
(DATA / "unified_sections.json").write_text(json.dumps(merged, ensure_ascii=False, indent=2), encoding="utf-8")
(ROOT / "catalog-merge-report.json").write_text(json.dumps(dict(
    input_count=merged["input_count"], retained_count=len(rows), removed_count=len(merges),
    conflicts=sum(bool(v.get("values_differ")) for v in merges),
    rules="same family, standard edition, specification; native library wins; old library fills missing data; preserve aliases",
    merges=merges), ensure_ascii=False, indent=2), encoding="utf-8")
print(f"input={merged['input_count']} retained={len(rows)} merged={len(merges)} "
      f"conflicts={sum(bool(v.get('values_differ')) for v in merges)}")
