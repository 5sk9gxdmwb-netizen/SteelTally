# 静态 Qt 实际构建 · 2026-11-10

工作目录：E:/SteelTallyToolchain/static-6.8.3-20261110-101215。
安装目标：该目录下 install。
阶段清单：E:/SteelTallyToolchain/static-build-record.json。

Qtbase configure 已实际退出 0。使用官方 CMake 3.31.8 + 本机 MinGW 13.1 的 mingw32-make，生成器 MinGW Makefiles。Ninja 官方文件下载失败（连接重置，重试超时），不再作为构建前提。

配置保留 Qt Network、Widgets、PrintSupport、Windows DirectWrite、FreeType/HarfBuzz、PNG/JPEG/GIF/ICO，以及 Windows Schannel。禁用 SQL/Testlib/DBus/ICU/OpenSSL。注意：虽然传入 FEATURE_opengl=OFF，配置摘要仍显示 Dynamic OpenGL=yes，不可照搬参数声称所有 OpenGL 路径均被关闭。最终运行与导入依赖检查为准。

配置摘要提示静态链接需显式导入插件，并警告缺少基础 cpp/winrt 支持。应用 CMake 已显式导入 Windows、offscreen、SVG 图像/图标插件，需等 SDK 生成后验证目标名称和实际链接。

实际 build-base 已启动，观察时日志约 10%，尚未安装 Qtbase 或编译 qtsvg。不得视为静态 SDK 已完成。

应用 build-static.ps1 已支持 MinGW Makefiles（默认使用现有 mingw32-make；保留 Ninja 参数兼容），语法检查通过。变更前备份位于“旧版可删/before-make-support-20261110-101413”。

构建后台任务结束后，先查看阶段清单和各日志；若全部六阶段退出 0，使用记录的 prefix、cmake 路径运行 build-static.ps1，再用 verify-static.ps1 对生成 EXE 检查依赖与完整自测。源、对象文件和链接记录须保留以便后续许可材料整理。不要重复启动另一套 Qt 编译。
