#include <QApplication>
#include <QLabel>
#include <QTextBrowser>
#include <QMenuBar>
#include <QMenu>
#include <QFileInfo>
#include <QFile>
#include <QJsonDocument>
#include <QScrollBar>
#include <QDir>
#include <QDialog>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTreeWidget>
#include <QPrintPreviewDialog>
#include <QTimer>
#include <QFontDatabase>
#include <cstdio>
#include "mainwindow.h"
#include "updater.h"
#include "tableinputpanel.h"
#include "reporter.h"
#include "workspaceextras.h"
#include "partcatalog.h"
#include "calculationprobe.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    // Make Chinese text render consistently in both desktop and offscreen checks.
    if (QFile::exists(QStringLiteral("C:/Windows/Fonts/msyh.ttc")))
        QFontDatabase::addApplicationFont(QStringLiteral("C:/Windows/Fonts/msyh.ttc"));
    app.setFont(QFont(QStringLiteral("Microsoft YaHei"),9));
    app.setApplicationName(QStringLiteral("GJGTableQty"));
    app.setOrganizationName(QStringLiteral("SteelTally"));
    app.setApplicationVersion(QStringLiteral("1.0.3"));
    const int updaterMode = updater::commandMode(app.arguments());
    if (updaterMode >= 0) return updaterMode;

    if (app.arguments().contains(QStringLiteral("--section-selftest"))) {
        extras::loadLibraries(QStringLiteral(":/assets"));
        Project p;TableInputPanel panel(&p,nullptr,false);QString error;
        const bool ok=panel.runCalculationChecks(&error);
        std::printf("section-selftest: %s %s\n",ok?"ok":"FAILED",error.toUtf8().constData());
        return ok?0:1;
    }

    MainWindow w;
    const int probeArgument = app.arguments().indexOf(QStringLiteral("--calculation-probe"));
    if (probeArgument >= 0) {
        if (probeArgument + 2 >= app.arguments().size()) {
            std::fprintf(stderr, "usage: --calculation-probe input.json output.json\n"); return 2;
        }
        return runCalculationProbe(app.arguments()[probeArgument + 1], app.arguments()[probeArgument + 2]);
    }
    if (app.arguments().contains(QStringLiteral("--selftest")) ||
        app.arguments().contains(QStringLiteral("--demo"))) w.loadSample();

    if (app.arguments().contains(QStringLiteral("--selftest"))) {
        QTimer::singleShot(400, &app, [&]() {
            const int outArgument = app.arguments().indexOf(QStringLiteral("--selftest-dir"));
            const QString work = outArgument >= 0 && outArgument + 1 < app.arguments().size()
                ? QDir(app.arguments()[outArgument + 1]).absolutePath()
                : QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("selftest-output"));
            if (!QDir().mkpath(work)) {
                std::fprintf(stderr, "selftest: cannot create output directory\n"); app.exit(2); return;
            }
            std::printf("selftest: summary rows=%d reports=%d\n", w.summaryRowCount(), w.reportCount());
            QString extraError;
            const bool extraOk = extras::runChecks(&extraError);
            std::printf("extensions: %s %s\n", extraOk ? "ok" : "FAILED", extraError.toUtf8().constData());
            QString inputError;
            const bool inputOk = w.runInputChecks(&inputError);
            std::printf("input: %s categories=%d partTypes=%d%s%s\n",
                        inputOk ? "ok" : "FAILED", w.inputCategoryCount(), w.inputPartTypeCount(),
                        inputOk ? "" : " error=", inputOk ? "" : inputError.toUtf8().constData());
            QJsonObject auditedCatalogues;
            for (auto it = partcatalog::verifiedCatalogue().cbegin();
                 it != partcatalog::verifiedCatalogue().cend(); ++it) {
                QJsonArray groups;
                int partCount = 0;
                for (const auto& group : partcatalog::groups(it.key())) {
                    groups.append(QJsonObject{{QStringLiteral("name"), group.first},
                        {QStringLiteral("parts"), QJsonArray::fromStringList(group.second)}});
                    partCount += group.second.size();
                }
                auditedCatalogues[it.key()] = QJsonObject{
                    {QStringLiteral("prefix"), partcatalog::prefix(it.key())},
                    {QStringLiteral("fireLimit"), partcatalog::blank({}, it.key()).fireLimit},
                    {QStringLiteral("partCount"), partCount}, {QStringLiteral("groups"), groups}};
            }
            Project cataloguePreviewProject;
            TableInputPanel cataloguePreview(&cataloguePreviewProject, nullptr, false);
            auto* categoryTree = cataloguePreview.findChild<QTreeWidget*>(QStringLiteral("categoryTree"));
            QJsonArray categoryDump;
            for (int i = 0; categoryTree && i < categoryTree->topLevelItemCount(); ++i) {
                auto* root = categoryTree->topLevelItem(i);
                QStringList children;
                for (int n = 0; n < root->childCount(); ++n) children << root->child(n)->text(0);
                categoryDump.append(QJsonObject{{QStringLiteral("name"), root->text(0)},
                    {QStringLiteral("types"), QJsonArray::fromStringList(children)}});
            }
            QFile catalogueFile(work + QStringLiteral("/verified_catalog.json"));
            const bool catalogueSaved = catalogueFile.open(QIODevice::WriteOnly) &&
                catalogueFile.write(QJsonDocument(QJsonObject{
                    {QStringLiteral("version"), app.applicationVersion()},
                    {QStringLiteral("catalogs"), auditedCatalogues},
                    {QStringLiteral("categories"), categoryDump}}).toJson()) > 0;
            std::printf("catalogue: verified=%d dump=%d version=%s\n", auditedCatalogues.size(),
                int(catalogueSaved), app.applicationVersion().toUtf8().constData());
            std::printf("assets dir = %s\n", w.assetsDir().toUtf8().constData());

            const bool lightOk = w.setTheme(QStringLiteral("SilveryWhite"));
            const int lightBytes = w.themeBytes();
            const bool darkOk = w.setTheme(QStringLiteral("CoolDark"));
            const int darkBytes = w.themeBytes();
            w.setTheme(QStringLiteral("SilveryWhite"));
            std::printf("theme: light=%d(%d B) dark=%d(%d B) icons=%d current=%s\n",
                        int(lightOk), lightBytes, int(darkOk), darkBytes, w.iconActionCount(),
                        w.themeName().toUtf8().constData());
            std::printf("theme: icons loaded %d / %d\n", w.validIconCount(), w.iconActionCount());
            auto* panel = w.findChild<TableInputPanel*>();
            const int panelIcons = panel ? panel->validIconCount() : 0;
            std::printf("input: icons loaded %d / %d\n", panelIcons, panel ? panel->iconCount() : 0);

            const char* specs[] = { "H400x200x8x13", "BOX400x400x12", "ROD20",
                                    "CZ200x70x20x2.5", "TH400-300x200x8x12", "FB100x10" };
            for (const char* s : specs) {
                SectionProps p = sectionProps(QString::fromUtf8(s));
                std::printf("  %-22s %.3f kg/m  %.4f m2/m\n", s, p.massPerM, p.surfacePerM);
            }

            const QString xlsxPath = work + QStringLiteral("/selftest_export.xlsx");
            const bool okXlsx = w.exportExcelTo(xlsxPath);
            std::printf("xlsx: %s sheets=%d size=%lld -> %s\n",
                        okXlsx ? "ok" : "FAILED", w.exportSheetCount(),
                        static_cast<long long>(QFileInfo(xlsxPath).size()),
                        xlsxPath.toUtf8().constData());

            const QString pdfPath = work + QStringLiteral("/selftest_reports.pdf");
            const bool okPdf = w.exportPdfTo(pdfPath);
            std::printf("pdf: %s size=%lld -> %s\n",
                        okPdf ? "ok" : "FAILED",
                        static_cast<long long>(QFileInfo(pdfPath).size()),
                        pdfPath.toUtf8().constData());

            bool previewObserved = false;
            QTimer::singleShot(350, &w, [&]() {
                if (auto* preview = qobject_cast<QPrintPreviewDialog*>(QApplication::activeModalWidget())) {
                    previewObserved = true;
                    preview->accept();
                } else if (auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget())) dialog->reject();
            });
            Report previewReport;
            previewReport.title = QStringLiteral("打印预览验证");
            previewReport.headers = {QStringLiteral("项目"), QStringLiteral("数量")};
            previewReport.rows = {{QStringLiteral("验证零件"), QStringLiteral("1")}};
            QString previewError;
            const bool previewOk = reporter::previewReports(&w, {previewReport}, &previewError) && previewObserved;
            std::printf("preview: %s dialog opened and closed; no printer job submitted\n", previewOk ? "ok" : "FAILED");

            w.resize(1620, 860);
            w.show();
            QCoreApplication::processEvents();
            const QString shotLight = work + QStringLiteral("/ui_silverywhite.png");
            const bool shot1 = w.grab().save(shotLight);
            w.setTheme(QStringLiteral("CoolDark"));
            QCoreApplication::processEvents();
            const bool shot2 = w.grab().save(work + QStringLiteral("/ui_cooldark.png"));
            w.setTheme(QStringLiteral("SilveryWhite"));
            bool dialogShot = false;
            if (panel) {
                QScopedPointer<QDialog> dialog(panel->createColumnDialog());
                dialog->show();
                QCoreApplication::processEvents();
                dialogShot = dialog->grab().save(work + QStringLiteral("/ui_column_settings.png"));
                dialog->reject();
            }
            std::printf("shots: light=%d dark=%d -> %s\n", int(shot1), int(shot2),
                        shotLight.toUtf8().constData());
            std::printf("shots: column-settings=%d\n", int(dialogShot));
            Project referenceProject;
            TableInputPanel referencePanel(&referenceProject, nullptr, false);
            referencePanel.prepareReferencePreview();
            referencePanel.setTheme(w.assetsDir(), false);
            referencePanel.resize(1620, 700); referencePanel.show();
            QCoreApplication::processEvents();
            referencePanel.findChild<QTableWidget*>(QStringLiteral("partsTable"))->horizontalScrollBar()->setValue(0);
            const bool referenceShot = referencePanel.grab().save(work + QStringLiteral("/ui_reference_layout.png"));
            QScopedPointer<QDialog> partDialog(referencePanel.createPartDialog());
            partDialog->show(); QCoreApplication::processEvents();
            const bool catalogShot = partDialog->grab().save(work + QStringLiteral("/ui_part_catalog.png"));
            partDialog->reject();
            QFile referenceFile(work + QStringLiteral("/ui_reference_project.json"));
            const bool referenceSaved = referenceFile.open(QIODevice::WriteOnly) &&
                referenceFile.write(QJsonDocument(referenceProject.toJson()).toJson()) > 0;
            std::printf("shots: reference-layout=%d part-catalog=%d project=%d\n",
                        int(referenceShot), int(catalogShot), int(referenceSaved));

            bool verifiedShots = categoryTree && categoryDump.size() == 6;
            cataloguePreview.setTheme(w.assetsDir(), false);
            cataloguePreview.resize(1620, 700);
            cataloguePreview.show();
            for (auto it = partcatalog::verifiedCatalogue().cbegin();
                 categoryTree && it != partcatalog::verifiedCatalogue().cend(); ++it) {
                const auto items = categoryTree->findItems(it.key(), Qt::MatchExactly | Qt::MatchRecursive);
                if (items.isEmpty()) { verifiedShots = false; continue; }
                categoryTree->setCurrentItem(items.first());
                cataloguePreview.createElement();
                QCoreApplication::processEvents();
                if (it.key() == QStringLiteral("钢梁"))
                    verifiedShots = cataloguePreview.grab().save(work + QStringLiteral("/ui_verified_beam.png")) && verifiedShots;
                QScopedPointer<QDialog> dialog(cataloguePreview.createPartDialog());
                dialog->show();
                QCoreApplication::processEvents();
                verifiedShots = dialog->grab().save(
                    work + QStringLiteral("/ui_verified_catalog_%1.png").arg(it.key())) && verifiedShots;
                dialog->reject();
            }
            std::printf("shots: verified-catalogues=%d all=%d\n", auditedCatalogues.size(), int(verifiedShots));

            bool helpOk = true;
            for (const QString page : {QStringLiteral("usage"), QStringLiteral("calculation"),
                                       QStringLiteral("changelog"), QStringLiteral("about")}) {
                QFile help(QStringLiteral(":/assets/help/%1.html").arg(page));
                helpOk = help.open(QIODevice::ReadOnly) && help.size() > 100 && helpOk;
            }
            auto* updateLabel = w.findChild<QLabel*>(QStringLiteral("updateStatus"));
            helpOk = helpOk && updateLabel && updateLabel->text().contains(QStringLiteral("未配置更新源")) &&
                w.windowTitle().contains(app.applicationVersion());
            std::printf("help: embedded-pages=4 status=%s\n", helpOk ? "ok" : "FAILED");
            bool helpDialogObserved = false;
            auto* helpMenu = w.menuBar()->actions().last()->menu();
            QTimer::singleShot(100, &w, [&]() {
                if (auto* dialog = w.findChild<QDialog*>(QStringLiteral("offlineHelpDialog"))) {
                    const auto browsers = dialog->findChildren<QTextBrowser*>();
                    helpDialogObserved = browsers.size() == 4;
                    for (auto* browser : browsers) helpDialogObserved = !browser->toPlainText().isEmpty() && helpDialogObserved;
                    dialog->grab().save(work + QStringLiteral("/ui_help.png"));
                    dialog->accept();
                }
            });
            helpMenu->actions().first()->trigger();
            helpOk = helpOk && helpDialogObserved;
            std::printf("help: dialog=%d\n", int(helpDialogObserved));
            std::fflush(stdout);
            app.exit(helpOk && extraOk && inputOk && catalogueSaved && verifiedShots && lightOk && darkOk && okXlsx && okPdf && previewOk && shot1 && shot2 && dialogShot && referenceShot && catalogShot && referenceSaved &&
                     w.validIconCount() == w.iconActionCount() && panel && panelIcons == panel->iconCount() ? 0 : 1);
        });
    } else {
        w.show();
        updater::acknowledgeHealth(app.arguments());
    }
    return app.exec();
}
