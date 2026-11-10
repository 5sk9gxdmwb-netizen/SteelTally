# 静态 Qt 准备进度 · 2026-11-10

用户已批准四个精确官方文件地址，下载实际启动。

已下载并解压：

- qtbase-everywhere-src-6.8.3.zip：74,010,483 字节，SHA256 992bf7766e214a341ef793eb3665fb784787d2fd666955f5f507f4c6f1f770dd。
- qtsvg-everywhere-src-6.8.3.zip：2,651,981 字节，SHA256 90643c1ba05245abedc2bcd9d4f745568dc27a1b37dd3ba861fd19d8ac6a7c46。

下载目录：E:/SteelTallyToolchain/official-20261110-094827。清单 manifest.json 随各下载完成后更新。Qtbase HTTP 入口响应提供的 SHA-256 Digest 与上述本地哈希一致。qtsvg 目前仅记录本地哈希，尚未另行核对发布方校验表。

CMake 下载中，观察时已约完成一半；Ninja 在串行下载队列下一项。未重复启动下载。源码 LICENSES 中 46 个文件及根部许可规则已复制到 toolchain/source-licenses，用于后续许可材料整理；不代表许可交付审核已完成。

已准备外部构建脚本，位于原桌面工作目录 build-qt-toolchain.py，和下载物分离。默认静态 Release、静态 C/C++ runtime，4 路并行，不构建 tests/examples；保留 Widgets、PrintSupport、平台插件和 SVG，禁用不需要的 OpenGL/Vulkan、SQL、DBus、ICU、OpenSSL。实际参数有效性及功能取舍需配置输出确认，不能凭脚本判断构建完成。

下载全部结束后：仅解压 CMake/Ninja（Qt 两份已解压，避免重复运行早期全量解压脚本），启动 build-qt-toolchain.py。脚本记录 E:/SteelTallyToolchain/static-build-record.json；阶段日志放新建 static-6.8.3-时间戳目录，实际成功后再运行 E:/表格算量/build-static.ps1 和 verify-static.ps1。
