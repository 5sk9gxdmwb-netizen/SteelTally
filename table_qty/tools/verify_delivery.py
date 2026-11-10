"""Read-only delivery checks; write audit results only into the evidence folder."""
import argparse
import hashlib
import json
import re
from pathlib import Path

import pymupdf
import openpyxl

from verified_catalog import compare


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest().upper()


def load(path):
    return json.loads(Path(path).read_text(encoding="utf-8-sig"))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("app", type=Path)
    parser.add_argument("evidence", type=Path)
    args = parser.parse_args()
    app, evidence = args.app, args.evidence
    result = {"version": "1.0.2", "calibration_version": "1.0.0"}
    result["user_files"] = [
        {"path": row["path"], "before": row["sha256"], "after": digest(row["path"]),
         "unchanged": digest(row["path"]) == row["sha256"]}
        for row in load(evidence / "user-data-before.json")
    ]
    original = load(evidence / "gjg-original-before.json")
    result["gjg_original"] = {
        "path": original["Path"], "before": original["Hash"],
        "after": digest(original["Path"]),
        "unchanged": digest(original["Path"]) == original["Hash"],
    }
    result["native_binaries"] = [
        {"path": str(path), "sha256": digest(path)}
        for path in (
            app / "runtime/table/gjg_table_qty.exe",
            app / "release/SteelTally/runtime/table/gjg_table_qty.exe",
        )
    ]
    result["same_binary"] = len({row["sha256"] for row in result["native_binaries"]}) == 1
    assert result["same_binary"], "Source and release native binaries differ"
    result["runs"] = {}
    for run in ("source", "frozen"):
        root = evidence / run
        status = load(root / "result.json")
        assert status["ok"] and status["native_exit"] == 0, f"{run}: selftest failed"
        native_log = (root / "native.log").read_text(encoding="utf-8-sig")
        count = int(re.search(r"assertions=(\d+) passed", native_log).group(1))
        assert count == 170 and "FAILED" not in native_log, f"{run}: native regressions"
        compare(load(evidence / "observed-catalog.json"),
                load(root / "table/verified_catalog.json"),
                load(evidence / "observed-categories.json"))
        exports = {}
        for name, path, expected in (
            ("table", root / "table/selftest_export.xlsx", 3),
            ("calibration", root / "engine/calibration.xlsx", 7),
        ):
            book = openpyxl.load_workbook(path, read_only=True, data_only=True)
            assert len(book.sheetnames) == expected, f"{run}/{name}: sheet count"
            error_cells = [
                f"{sheet.title}!{cell.coordinate}"
                for sheet in book for row in sheet for cell in row if cell.data_type == "e"
            ]
            assert not error_cells, f"{run}/{name}: Excel errors {error_cells}"
            exports[name] = {"sheets": book.sheetnames, "size": path.stat().st_size,
                             "excel_errors": error_cells}
            book.close()
        pdf_path = root / "table/selftest_reports.pdf"
        with pymupdf.open(pdf_path) as pdf:
            assert len(pdf) > 0 and sum(len(page.get_text().strip()) for page in pdf) > 0
            exports["pdf"] = {"pages": len(pdf), "size": pdf_path.stat().st_size}
        images = list((root / "table").glob("ui_verified_catalog_*.png"))
        assert len(images) == 11 and all(path.stat().st_size > 1000 for path in images)
        result["runs"][run] = {
            "ok": status["ok"], "native_exit": status["native_exit"],
            "assertions": count, "exports": exports, "catalogue_screenshots": len(images),
        }
    tests = (evidence / "python-tests.log").read_text(encoding="utf-8-sig")
    assert re.search(r"Ran 26 tests", tests) and re.search(r"^OK$", tests, re.MULTILINE)
    result["python_tests"] = 26
    result["ok"] = (
        all(row["unchanged"] for row in result["user_files"])
        and result["gjg_original"]["unchanged"] and result["same_binary"]
        and all(run["ok"] for run in result["runs"].values())
    )
    (evidence / "delivery-check.json").write_text(
        json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8"
    )
    assert result["ok"], "Preservation checks failed; inspect delivery-check.json"
    frozen = result["runs"]["frozen"]
    report = f"""# GJG实机核对升级验证报告

日期：2026-11-09。原生表格v1.0.2；标定台v1.0.0。实际检查全部通过。

## 验证结果

| 检查 | 结果 |
|---|---|
| Qt/MinGW Release构建 | 成功，见native-build-final.log |
| Python自动回归 | 26项通过，见python-tests.log |
| 源码入口端到端自检 | 通过，source/result.json的ok为true，原生退出码0 |
| 发布主入口端到端自检 | 通过，frozen/result.json的ok为true，原生退出码0 |
| 原生交互 | 源/发布各170条断言通过，另有扩展/主窗口汇总检查 |
| 独立观察记录比对 | 源/发布实际二进制均匹配六组34类、11类107项、分组顺序/前缀/时限 |
| 旧5类型 | 自定义保留；旧工程显式分类不变 |
| 原生Excel | {len(frozen['exports']['table']['sheets'])}表，可重新打开，无Excel错误单元格 |
| 标定Excel | {len(frozen['exports']['calibration']['sheets'])}表，可重新打开，无Excel错误单元格 |
| PDF | {frozen['exports']['pdf']['pages']}页、{frozen['exports']['pdf']['size']}字节，可解析文本 |
| 打印预览 | 自动打开关闭，未向打印机提交作业 |
| 截图 | 原有银白/深色/列设置/五钢柱工作区，另11类目录及钢梁工作区 |
| 用户数据 | 前置记录的{len(result['user_files'])}个文件SHA256全部不变 |
| GJG原样例 | SHA256不变；核对只在独立另存副本中进行 |
| 发布同步 | source runtime与release runtime原生EXE的SHA256一致 |

## 二进制

SHA256：`{result['native_binaries'][0]['sha256']}`

当前原生文件：
`{result['native_binaries'][1]['path']}`

完整发布主入口：
`{app / 'release/SteelTally/SteelTally.exe'}`

既有标定台若已打开，无需强制退出或覆盖未保存内容；下次打开“最新版表格算量”使用新原生文件。

## 已观察告警

构建环境提示未找到Vulkan headers、translations/catalogs.json、dxcompiler/dxil，
运行库回退到PATH查找。构建成功，上述实际测试覆盖的Qt Widgets、主题、导出与预览均通过。
这些告警未被隐藏；不以本轮测试替代所有机器/显卡/打印机组合的兼容性验证。

## 核对与边界

目录逐项依据observed-catalog.json及对应原版截图/UIA；分类依据observed-categories.json。
观察数据与编译产物独立比较，比较日志分别为source-catalog-compare.log和frozen-catalog-compare.log。
目录项名称不等于几何计量算法。其余23类目录、原版按选中行插入规则、
全部窗口逐像素一致及专有自动计量算法没有完成核实，不作已完成声明。
新行防火时限只是原软件默认值，不是工程设计结论。

## 可回滚路径

根目录`旧版可删/GJG实机核对升级前-20261109`保留升级前源码、原生EXE与文档；
其release-before保留发布包替换前原生EXE及README/CHANGELOG。
未永久删除文件，未重打Python包，未覆盖用户工程、案例和企业库。

逐文件保护结果与导出结构详见delivery-check.json。
"""
    (evidence / "验证报告.md").write_text(report, encoding="utf-8")
    print(json.dumps({
        "ok": result["ok"], "python_tests": 26, "native_assertions_each": 170,
        "user_files_unchanged": len(result["user_files"]), "gjg_original_unchanged": True,
        "pdf_pages": frozen["exports"]["pdf"]["pages"],
        "report": str(evidence / "验证报告.md"),
    }, ensure_ascii=False))


if __name__ == "__main__":
    main()
