# SteelTally 第一版原生表格源码

来源：用户指定 new-chat-3/outputs/gjg_table_qty_qt。
当前副本已增加节点参数、企业数据、矩阵粘贴、原子保存和相关回归检查。
当前原生版本v1.0.2：六组34类实机分类、11类107项目录与新行默认值已核实。
原5类兼容类型保留至自定义；其余目录仍为明确标注的可编辑工程预设。
不要从旧 build 目录运行；该失败构建已归档。

从上级应用目录执行：

```powershell
.\build.ps1 -TableOnly
.\runtime\table\gjg_table_qty.exe
.\runtime\table\gjg_table_qty.exe --selftest --selftest-dir C:\Temp\table-check --data-root C:\Temp\table-check\data
```

Qt 6.8.3 / MinGW 13.1 / C++17。主构建脚本使用英文实体暂存目录。
源码在 src；主题、SVG 图标和参考数据在 assets；主题生成工具在 tools。
22 列输入、19 列构件汇总、14 张报表、3 张合并 Excel、PDF 和打印预览沿用选定版本。

本程序是用户提供的重写工程，不是厂商专有源码恢复。
参考资源的对外分发权限未在本轮验证，先用于内部对标。
旧来源 README 已备份在根“旧版可删/原生表格最终核对前-20261109”。
功能、计量边界、用户数据路径与验证方式见上级 README、CHANGELOG 及 docs。
最新说明：上级 `docs/GJG实机核对升级-v1.0.2.md`。
观察夹具位于 tests/fixtures；tools/verified_catalog.py 可生成头文件并对比二进制自检输出。
