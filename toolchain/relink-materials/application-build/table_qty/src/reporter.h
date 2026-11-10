#pragma once
#include <QString>
#include <QVector>
#include "reports.h"

class QPrinter;
class QWidget;

// 报表渲染 / 打印 / PDF 输出。
// 版式：A4 横向，表头跨页重复，首行标题块，页脚含报表名与「第 n 页 / 共 m 页」。
namespace reporter {

// 把一组报表渲染到已配置好的 printer 上；每张报表另起一页。
bool renderToPrinter(QPrinter* printer, const QVector<Report>& reports, QString* err = nullptr);

// 导出为单个 PDF 文件（多报表，各自起新页）。
bool exportPdf(const QString& path, const QVector<Report>& reports, QString* err = nullptr);

// 弹出打印对话框并打印。
bool printReports(QWidget* parent, const QVector<Report>& reports, QString* err = nullptr);

// 弹出打印预览（可再打印）。
bool previewReports(QWidget* parent, const QVector<Report>& reports, QString* err = nullptr);

} // namespace reporter
