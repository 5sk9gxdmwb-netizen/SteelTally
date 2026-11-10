#pragma once
#include <QString>
#include <QStringList>
#include <QVector>

struct XlsxSheet {
    QString name;
    QVector<QStringList> rows;
    // 一张工作表可包含多段报表块，每段的标题行 / 表头行都登记在这里
    QVector<int> titleRows;   // 粗体 14
    QVector<int> headerRows;  // 粗体 11
};

bool writeXlsx(const QString& path, const QVector<XlsxSheet>& sheets, QString* err = nullptr);
