#pragma once
#include <QtWidgets>

// Progress reports completed initialization stages, not a simulated percentage.
class StartupSplash : public QSplashScreen {
public:
    StartupSplash() : QSplashScreen(QPixmap(620, 300), Qt::WindowStaysOnTopHint) {
        setObjectName(QStringLiteral("startupSplash"));
        setWindowIcon(QIcon(QStringLiteral(":/assets/icons/light/product/App/logo.ico")));
        setStage(8, QStringLiteral("正在初始化界面…"));
    }
    void setStage(int value, const QString& message) {
        progress = qBound(0, value, 100); status = message;
        QPixmap image(620, 300); image.fill(QColor("#edf2f6"));
        QPainter p(&image); p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(0, 0, 12, 300, QColor("#2976ae"));
        QIcon(QStringLiteral(":/assets/icons/light/product/App/logo.ico")).paint(&p, 42, 46, 64, 64);
        QFont title = QApplication::font(); title.setPointSize(23); title.setBold(true); p.setFont(title);
        p.setPen(QColor("#20394b")); p.drawText(QRect(126, 46, 470, 48), Qt::AlignVCenter, QStringLiteral("SteelTally 表格算量"));
        QFont body = QApplication::font(); body.setPointSize(11); p.setFont(body);
        p.setPen(QColor("#536a7a")); p.drawText(128, 117, QStringLiteral("钢结构型材查询 · 截面计算 · 表格算量"));
        p.drawText(QRect(44, 197, 532, 26), Qt::AlignLeft | Qt::AlignVCenter, status);
        p.fillRect(44, 237, 532, 5, QColor("#cfdae2"));
        p.fillRect(44, 237, 532 * progress / 100, 5, QColor("#2976ae"));
        body.setPointSize(9); p.setFont(body);
        p.drawText(QRect(44, 251, 532, 22), Qt::AlignRight, QStringLiteral("%1%").arg(progress));
        body.setPointSize(9); p.setFont(body);
        p.drawText(QRect(44, 263, 532, 22), Qt::AlignRight, QStringLiteral("版本 %1").arg(QCoreApplication::applicationVersion()));
        p.end(); setPixmap(image);
    }
private:
    int progress = 0;
    QString status;
};
