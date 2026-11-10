# SteelTally · 表格算量

钢结构构件表格算量工具，支持标准与非标构件的重量、面积计算及报表导出。

## 功能特性

- **构件录入**：支持六大类标准构件及非标几何，父子构件层级管理
- **计算引擎**：基于国标规格库与截面特性，面密度系数与人工覆盖
- **报表导出**：汇总表与分类明细，Excel (`.xlsx`) 与 PDF 格式
- **界面主题**：浅色/深色皮肤，内嵌图标与离线帮助
- **更新模块**：HTTPS 清单、SHA256 校验、RSA 签名验证、失败回滚

## 构建

### 动态 Qt 开发构建

```powershell
.\build-native.ps1
```

输出：`build\gjg_table_qty.exe`（需要运行时 Qt DLL）

### 静态单文件发行构建

```powershell
.\build-static.ps1 `
  -QtPrefix 'E:\SteelTallyToolchain\static-6.8.3-...\install' `
  -CMake 'path\to\cmake.exe'
```

输出：`release-static\gjg_table_qty.exe`（单文件，仅依赖 Windows 系统 DLL）

可选更新配置：
- `-UpdateUrl 'https://your-domain.com/updates/manifest.json'`
- `-UpdatePublicKeyFile 'path\to\public-key.b64'`

详见 `docs\在线更新开发说明.md`。

### 验证

```powershell
.\verify-static.ps1  # 单文件隔离测试
.\tests\test-update-signatures.ps1  # 签名验证
.\tests\test-update-transaction.ps1  # 替换与回滚
```

## 项目结构

```
table_qty/          应用源码 (C++17, Qt 6.8.3)
  src/              主程序、更新模块、工作区扩展
  assets/           图标、主题、数据、帮助页
tests/              PowerShell 测试脚本
docs/               开发说明与验收清单
toolchain/          静态 Qt 构建脚本与材料
distribution-materials/  许可、SBOM、重新链接材料
```

## 许可

应用代码未指定开源许可。静态构建链接 Qt 6.8.3（LGPLv3），发行时需提供来源、许可文本及重新链接材料，详见 `distribution-materials/README.md`。

## 贡献与支持

本项目当前为私有开发。更新服务器与签名密钥由发布者配置；默认构建显示"未配置更新源"。
