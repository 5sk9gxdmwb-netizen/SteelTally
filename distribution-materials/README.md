# 静态发行材料索引

本目录保留静态 Qt 发行所需的来源与声明材料。程序可以单 EXE 分发运行；适用许可要求的源码、声明和重新链接材料应随发行提供或以对应许可允许的方式提供，不能因程序单文件而省略。

- qt-source：本次实际使用的 Qtbase / Qtsvg 6.8.3 官方源码 ZIP，原样保留。
- licenses：源码随附的许可文本与规则。
- sbom：静态 SDK 安装生成的 Qtbase/Qtsvg SPDX 文件。
- attribution-originals：源码各处 qt_attribution.json 原文件。
- third-party-attributions.json：59 项来源声明的整合索引。它是源码声明的全集候选，包含本次未链接组件，不应宣称59项均已链接。原文件个别含未转义控制字符，索引生成读取时允许该字符，未修改原声明。

应用对象文件、staging 源码及链接命令已保存于 toolchain/relink-materials/application-build；随着更新模块变动，需在最终成功构建后重新归档对应版本，不要以旧对象文件代表新 EXE。

Qt 完整静态构建树与安装暂留 E:/SteelTallyToolchain/static-6.8.3-20261110-101215。下载哈希清单见 toolchain/download-manifest.json，阶段清单见该构建根目录 static-build-record.json。

重新构建流程：使用同版本 MinGW 13.1 和 CMake 3.31.8，按 Qt 成功配置及修正记录构建静态 Qtbase，再构建 Qtsvg；运行 build-static.ps1 指向新静态 prefix。脚本会在新 ASCII staging 中复制本程序源码，把资源物理名转换为 ASCII，保留原 qrc 别名。修改 Qt 后重新配置应用，应使新 Qt 的导入目标和静态插件被实际采用。对象级重新链接命令位于保存的 CMakeFiles/gjg_table_qty.dir/link.txt 及响应文件中，绝对路径需对应调整。最终要验证修改 Qt 后可重新链接，而非仅保存材料。

用户未指定商业 Qt 授权，本轮保留开源许可材料，不代表已经完成法律审核。需确认静态链接适用的 LGPLv3 条件、修改/重新链接权利、工具链运行库例外及实际链接第三方的版权声明。当前无应用最终许可文本，不应擅自替用户授予整套应用开源许可或编造版权所有人。
