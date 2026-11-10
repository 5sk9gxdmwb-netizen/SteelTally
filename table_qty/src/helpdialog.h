#pragma once
#include <QtWidgets>

inline void showTableHelp(QWidget* parent, int initialTab = 0) {
    QDialog dialog(parent);
    dialog.setObjectName(QStringLiteral("offlineHelpDialog"));
    dialog.setWindowTitle(QStringLiteral("表格算量 · 帮助"));
    dialog.resize(800, 600);
    QVBoxLayout layout(&dialog);
    QTabWidget tabs;
    const QStringList names{QStringLiteral("使用说明"), QStringLiteral("计算依据"),
                            QStringLiteral("更新日志"), QStringLiteral("关于")};
    const QStringList pages{"usage", "calculation", "changelog", "about"};
    for (int i = 0; i < pages.size(); ++i) {
        auto* browser = new QTextBrowser(&tabs);
        browser->setOpenExternalLinks(false);
        browser->setSource(QUrl(QStringLiteral("qrc:/assets/help/%1.html").arg(pages[i])));
        tabs.addTab(browser, names[i]);
    }
    tabs.setCurrentIndex(initialTab);
    layout.addWidget(&tabs);
    QDialogButtonBox buttons(QDialogButtonBox::Close);
    QObject::connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout.addWidget(&buttons);
    dialog.exec();
}
