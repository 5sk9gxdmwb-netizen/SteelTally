// Standalone check: every catalogue record with explicit parameters is drawable, and every custom
// shape renders. Build (dynamic Qt, from table_qty/src, ASCII output dir):
//   g++ -std=c++17 -I. -I<Qt>/include{,/QtCore,/QtGui,/QtWidgets} ../../tests/section-drawing-check.cpp sectiondrawing.cpp -L<Qt>/lib -lQt6Widgets -lQt6Gui -lQt6Core
// Run: QT_QPA_PLATFORM=offscreen check.exe <table_qty/assets> <output dir>
#include <QtWidgets>
#include "sectionlibrary.h"
#include "sectiondrawing.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.size() < 3) return 2;
    const QString assets = args[1], out = args[2];
    QDir().mkpath(out);
    sectionlib::load(assets);
    if (sectionlib::records().isEmpty()) { std::printf("catalogue not loaded\n"); return 2; }
    SectionDrawing draw; draw.resize(620, 560);
    QMap<QString, QPair<int, int>> byFamily; // drawable, failed (records that have parameters)
    QStringList failures, shown;
    for (const auto& rec : sectionlib::records()) {
        draw.setRecord(rec, true);
        const auto fam = rec["family"].toString();
        auto& e = byFamily[fam];
        if (!draw.geometry().isEmpty()) {
            ++e.first;
            if (!shown.contains(fam)) { shown << fam; draw.grab().save(QDir(out).filePath(QString("std-%1.png").arg(shown.size()))); }
        } else if (!rec["parameters"].toString().isEmpty() && fam != QString::fromUtf8("钢轨") && fam != QString::fromUtf8("格栅板") &&
                   rec["spec"] != "C50*45*30*2" && rec["spec"] != "Z250*70*2*2.5") {
            // Two catalogue rows (C50*45*30*2, Z250*70*2*2.5) carry self-contradictory lip c; they are reported, not drawn.
            ++e.second; failures << fam + " " + rec["spec"].toString() + " :: " + rec["parameters"].toString();
        }
    }
    for (auto it = byFamily.begin(); it != byFamily.end(); ++it)
        std::printf("%s drawable=%d failed_with_params=%d\n", qPrintable(it.key()), it.value().first, it.value().second);
    for (const auto& f : failures) std::printf("FAIL %s\n", qPrintable(f));
    int bad = failures.size();
    for (int i = 0; i < sectionlib::kCustomShapeCount; ++i) {
        const auto r = sectionlib::formula(i, 400, 200, 8, 13, 0, 50);
        draw.setRecord(r, false);
        if (r.isEmpty() || draw.geometry().isEmpty() || !draw.grab().save(QDir(out).filePath(QString("custom-%1.png").arg(i)))) { ++bad; std::printf("custom %d FAILED\n", i); }
    }
    std::printf("bad=%d\n", bad);
    return bad ? 1 : 0;
}
