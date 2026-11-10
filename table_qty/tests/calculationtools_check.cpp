// 独立自检驱动：只编译 calculationtools.cpp，不链接主程序。
// 用法：calculationtools_check [输出目录]   （建议 QT_QPA_PLATFORM=offscreen）
#include <QApplication>
#include <cstdio>
#include "../src/calculationtools.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QString msg;
    const bool ok = runCalculationToolChecks(&msg, argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString());
    std::printf("calculation-tools-check: %s %s\n", ok ? "ok" : "FAILED", msg.toUtf8().constData());
    std::fflush(stdout);
    return ok ? 0 : 1;
}
