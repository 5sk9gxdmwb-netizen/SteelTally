"""Compare native results to individually evidenced GJG UI/exports, never to invented outputs."""
import argparse
import json
import math
import os
import subprocess
from pathlib import Path


def read(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def compare(oracle, actual, evidence_root):
    assert oracle["schema"] == "steeltally.gjg-calculation-oracle/1"
    cases = oracle["cases"]
    assert cases, "No observed cases; an empty oracle is not a passing test"
    ids = [sample["id"] for sample in cases]
    assert len(ids) == len(set(ids)), "Duplicate oracle IDs"
    actual_ids = [sample["id"] for sample in actual["cases"]]
    assert len(actual_ids) == len(set(actual_ids)), "Duplicate native IDs"
    assert set(ids) == set(actual_ids), "Native/oracle case coverage differs"
    results = {sample["id"]: sample for sample in actual["cases"]}
    report = []
    for sample in cases:
        assert sample["source"] == "GJG2026 live UI", sample["id"]
        assert sample["expected"], "No observed output: " + sample["id"]
        assert sample["evidence"] and all(
            (evidence_root / path).is_file() for path in sample["evidence"]
        ), "Missing reference evidence: " + sample["id"]
        actual_case = results[sample["id"]]
        differences = {}
        for metric, expected in sample["expected"].items():
            tolerance = sample["tolerance"].get(metric, 0)
            value = actual_case[metric]
            assert isinstance(expected, (float, int)) and math.isfinite(expected)
            assert isinstance(value, (float, int)) and math.isfinite(value)
            assert tolerance >= 0
            delta = value - expected
            differences[metric] = {
                "expected": expected, "actual": value, "delta": delta,
                "tolerance": tolerance, "ok": abs(delta) <= tolerance + 1e-12,
            }
        report.append({"id": sample["id"], "description": sample["description"],
                       "metrics": differences,
                       "ok": all(metric["ok"] for metric in differences.values())})
    return {"nativeVersion": actual["version"], "cases": report,
            "passed": sum(case["ok"] for case in report), "total": len(report),
            "ok": all(case["ok"] for case in report)}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("oracle", type=Path)
    parser.add_argument("--native", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--evidence-root", type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    native_path = args.native.resolve()
    env = os.environ.copy()
    for name in ("QT_PLUGIN_PATH", "QT_QPA_PLATFORM_PLUGIN_PATH", "QML2_IMPORT_PATH"):
        env.pop(name, None)
    env["QT_QPA_PLATFORM_PLUGIN_PATH"] = str(native_path.parent / "plugins")
    actual_path = (args.output / "native-results.json").resolve()
    process = subprocess.run(
        [str(native_path), "--calculation-probe", str(args.oracle.resolve()), str(actual_path)],
        cwd=native_path.parent, env=env, capture_output=True, timeout=30,
    )
    (args.output / "native-probe.log").write_bytes(process.stdout + process.stderr)
    assert process.returncode == 0, f"Native probe exited {process.returncode}"
    report = compare(read(args.oracle), read(actual_path), args.evidence_root.resolve())
    (args.output / "comparison.json").write_text(
        json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8"
    )
    lines = [
        "# GJG同输入算量差异", "",
        f"原生版本：{report['nativeVersion']}；通过：{report['passed']}/{report['total']}。",
        "这里只证明所列观察案例及输出精度，不代表整套GJG算法一致。", "",
        "| 案例 | 指标 | GJG观察值 | 本程序值 | 差值 | 容差 | 结果 |",
        "|---|---|---:|---:|---:|---:|---|",
    ]
    for case in report["cases"]:
        for name, result in case["metrics"].items():
            lines.append(
                f"| {case['id']} | {name} | {result['expected']:.9g} | "
                f"{result['actual']:.9g} | {result['delta']:.9g} | "
                f"{result['tolerance']:.9g} | {'通过' if result['ok'] else '差异'} |"
            )
    (args.output / "算量差异.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"Native {report['nativeVersion']}: {report['passed']}/{report['total']} cases match observed GJG outputs")
    return 0 if report["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
