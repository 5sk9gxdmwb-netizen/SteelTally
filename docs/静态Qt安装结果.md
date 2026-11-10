# 静态 Qt 安装结果 · 2026-11-10

静态 Qt 6.8.3 已实际编译、安装，SVG 模块亦已实际编译安装。

安装目录：E:/SteelTallyToolchain/static-6.8.3-20261110-101215/install。
构建阶段清单：E:/SteelTallyToolchain/static-build-record.json。

最终成功阶段退出码均为 0：rebuild-base-serial、install-base、configure-svg、build-svg、install-svg。早期失败记录保留：build-base=2（OpenGL/Dynamic OpenGL 配置冲突），rebuild-base=2（并行 Make 头文件同步占用）。修正后使用单路编译完成。

安装目录 mkspecs/qconfig.pri 明确显示 QT_CONFIG += static ... static_runtime release，禁用 shared，Qt 6.8.3 / GCC 13.1。实际工具为 CMake 3.31.8 和 MinGW Makefiles，未使用未下载成功的 Ninja。

应用单文件构建已启动，尚需实际链接、PE 导入检查、离屏及 Windows 平台完整自测。静态 Qt 安装成功不等于应用单文件已交付。

许可文件位于 toolchain/source-licenses，原始源码、Qt 构建目录及对象文件均保留。分发前仍需整理对应第三方声明与可重新链接材料，当前未作许可合规完成认证。
