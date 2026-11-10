#include "reporter.h"

#include <QDate>
#include <QDialog>
#include <QFileInfo>
#include <QFontMetricsF>
#include <QMarginsF>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPrintDialog>
#include <QPrintPreviewDialog>
#include <QPrinter>
#include <QRegularExpression>
#include <QWidget>

#include <algorithm>
#include <cmath>

namespace reporter {
namespace {

struct Palette {
    QColor ink    = QColor(0x1A, 0x1A, 0x1A);
    QColor sub    = QColor(0x59, 0x59, 0x59);
    QColor rule   = QColor(0x8C, 0x8C, 0x8C);
    QColor grid   = QColor(0xC6, 0xC6, 0xC6);
    QColor edge   = QColor(0x6E, 0x6E, 0x6E);
    QColor headBg = QColor(0xE8, 0xEC, 0xF1);
    QColor accent = QColor(0x1F, 0x5C, 0xA8);
    QColor alt    = QColor(0xF7, 0xF9, 0xFB);
};

struct Fonts {
    QFont title;   // 首页大标题
    QFont subtitle;// 续页小标题
    QFont meta;    // 分组 / 日期
    QFont head;    // 列头
    QFont cell;    // 数据
    QFont foot;    // 页脚
};

struct Layout {
    QVector<double> w;   // 每列宽（设备像素）
    double headH = 0;    // 表头行高
    double rowH = 0;     // 数据行高
    int cols = 0;
};

struct Chunk { int from; int to; }; // 数据行区间 [from, to)

const QRegularExpression& numRe() {
    static const QRegularExpression re(
        QStringLiteral("^\\s*[-+]?\\d[\\d,]*(?:\\.\\d+)?\\s*%?\\s*$"));
    return re;
}

bool isNumeric(const QString& s) { return numRe().match(s).hasMatch(); }

Fonts makeFonts(double S) {
    const QString fam = QStringLiteral("Microsoft YaHei");
    Fonts f;
    auto mk = [&](double pt, bool bold) {
        QFont fnt(fam);
        fnt.setPixelSize(std::max(6, int(std::lround(pt * S))));
        fnt.setBold(bold);
        fnt.setStyleStrategy(QFont::PreferAntialias);
        return fnt;
    };
    f.title    = mk(16.5, true);
    f.subtitle = mk(10.5, true);
    f.meta     = mk(8.8, false);
    f.head     = mk(8.6, true);
    f.cell     = mk(8.6, false);
    f.foot     = mk(7.6, false);
    return f;
}

int columnCount(const Report& r) {
    int n = r.headers.size();
    for (const auto& row : r.rows) n = std::max(n, int(row.size()));
    return std::max(1, n);
}

Layout layoutTable(const Report& r, double availW, double S, const Fonts& f, QPaintDevice* dev) {
    Layout L;
    L.cols = columnCount(r);
    L.rowH = 16.0 * S;
    const double padX = 4.0 * S;
    const double minW = 26.0 * S;
    const double maxW = 190.0 * S;

    QFontMetricsF fmHead(f.head, dev);
    QFontMetricsF fmCell(f.cell, dev);

    QVector<double> need(L.cols, minW);
    for (int c = 0; c < L.cols; ++c) {
        double w = 0;
        if (c < r.headers.size()) w = fmHead.horizontalAdvance(r.headers[c]);
        for (const auto& row : r.rows)
            if (c < row.size()) w = std::max(w, fmCell.horizontalAdvance(row[c]));
        need[c] = std::clamp(w + 2 * padX + 4 * S, minW, maxW);
    }

    // 宽度分配：够宽按比例放大，不够宽按比例压缩并保底
    double sum = 0;
    for (double v : need) sum += v;
    QVector<double> out(L.cols, 0);
    if (sum <= availW && sum > 0) {
        for (int c = 0; c < L.cols; ++c) out[c] = need[c] * availW / sum;
    } else {
        out = need;
        QVector<bool> locked(L.cols, false);
        for (int iter = 0; iter < 8; ++iter) {
            double fixedSum = 0, freeNeed = 0;
            for (int c = 0; c < L.cols; ++c) {
                if (locked[c]) fixedSum += out[c];
                else freeNeed += need[c];
            }
            const double target = availW - fixedSum;
            if (freeNeed <= 0 || target <= 0) break;
            bool changed = false;
            for (int c = 0; c < L.cols; ++c)
                if (!locked[c]) out[c] = need[c] * target / freeNeed;
            for (int c = 0; c < L.cols; ++c) {
                if (!locked[c] && out[c] < minW) { out[c] = minW; locked[c] = true; changed = true; }
            }
            if (!changed) break;
        }
    }
    double total = 0;
    for (double v : out) total += v;
    if (total > 0 && total > availW) {
        for (double& v : out) v *= availW / total;
    }
    L.w = out;

    // 表头行高（允许换行，最多两行）
    const double innerW = (L.cols > 0 ? L.w[0] : availW) - 2 * padX;
    double h = 0;
    for (int c = 0; c < L.cols; ++c) {
        const double cw = std::max(8.0, L.w[c] - 2 * padX);
        const QString t = (c < r.headers.size()) ? r.headers[c] : QString();
        QRectF br = fmHead.boundingRect(QRectF(0, 0, cw, 1e6),
                                        Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignVCenter, t);
        h = std::max(h, br.height() + 7.0 * S);
    }
    Q_UNUSED(innerW);
    L.headH = std::max(18.5 * S, h);
    return L;
}

void drawCellText(QPainter& p, const QRectF& rect, const QString& text, const QFont& font,
                  const QColor& color, Qt::Alignment align, bool elideRight) {
    if (rect.width() <= 1 || text.isEmpty()) return;
    const double padX = 4.0;
    QFontMetricsF fm(font, p.device());
    QString t = elideRight ? fm.elidedText(text, Qt::ElideRight, std::max(4.0, rect.width() - 2 * padX))
                           : text;
    p.setFont(font);
    p.setPen(color);
    p.drawText(rect.adjusted(padX, 0, -padX, 0),
               align | Qt::AlignVCenter | (elideRight ? Qt::TextSingleLine : Qt::TextWordWrap), t);
}

void drawTitleBlock(QPainter& p, const QRectF& page, double S, const Fonts& f, const Palette& c,
                    const Report& r, bool firstPage, int index, int total) {
    const double mx = 24.0 * S;
    const double top = 22.0 * S;
    const double w = page.width() - 2 * mx;

    if (firstPage) {
        QRectF rt(mx, top, w, 22.0 * S);
        p.setFont(f.title);
        p.setPen(c.ink);
        p.drawText(rt, Qt::AlignHCenter | Qt::AlignVCenter, r.title);

        QString meta = QStringLiteral("%1  ·  报表 %2 / %3  ·  %4")
                           .arg(r.group)
                           .arg(index + 1)
                           .arg(total)
                           .arg(QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd")));
        p.setFont(f.meta);
        p.setPen(c.sub);
        p.drawText(QRectF(mx, top + 22.0 * S, w, 12.0 * S),
                   Qt::AlignHCenter | Qt::AlignVCenter, meta);

        const double y = top + 36.0 * S;
        p.setPen(QPen(c.accent, 1.5 * S));
        p.drawLine(QPointF(mx, y), QPointF(mx + w, y));
        p.setPen(QPen(c.rule, 0.5 * S));
        p.drawLine(QPointF(mx, y + 1.6 * S), QPointF(mx + w, y + 1.6 * S));
    } else {
        QRectF rt(mx, top, w, 14.0 * S);
        p.setFont(f.subtitle);
        p.setPen(c.ink);
        p.drawText(rt, Qt::AlignLeft | Qt::AlignVCenter,
                   QStringLiteral("%1（续）").arg(r.title));

        p.setFont(f.meta);
        p.setPen(c.sub);
        p.drawText(QRectF(mx, top, w, 14.0 * S), Qt::AlignRight | Qt::AlignVCenter,
                   QStringLiteral("第 %1 页").arg(index + 1));

        const double y = top + 15.0 * S;
        p.setPen(QPen(c.rule, 0.6 * S));
        p.drawLine(QPointF(mx, y), QPointF(mx + w, y));
    }
}

void drawColumnHeader(QPainter& p, const QRectF& page, double S, const Fonts& f, const Palette& c,
                      const Report& r, const Layout& L, double y) {
    const double mx = 24.0 * S;
    double x = mx;
    p.setPen(QPen(c.edge, 0.7 * S));
    p.setBrush(c.headBg);
    p.drawRect(QRectF(mx, y, page.width() - 2 * mx, L.headH));
    p.setBrush(Qt::NoBrush);

    for (int col = 0; col < L.cols; ++col) {
        const QRectF cell(x, y, L.w[col], L.headH);
        const QString t = (col < r.headers.size()) ? r.headers[col] : QString();
        drawCellText(p, cell, t, f.head, c.ink, Qt::AlignHCenter, false);
        if (col > 0) {
            p.setPen(QPen(c.grid, 0.5 * S));
            p.drawLine(QPointF(x, y), QPointF(x, y + L.headH));
        }
        x += L.w[col];
    }
    p.setPen(QPen(c.edge, 0.7 * S));
    p.drawRect(QRectF(mx, y, page.width() - 2 * mx, L.headH));
}

void drawDataRow(QPainter& p, double S, const Palette& c, const Fonts& f, const Layout& L,
                 double mx, double tableW, double y, const QStringList& row, int visualRow) {
    if (visualRow % 2 == 1) {
        p.fillRect(QRectF(mx, y, tableW, L.rowH), c.alt);
    }
    double x = mx;
    for (int col = 0; col < L.cols; ++col) {
        const QRectF cell(x, y, L.w[col], L.rowH);
        const QString t = (col < row.size()) ? row[col] : QString();
        const Qt::Alignment al = (col > 0 && isNumeric(t)) ? Qt::AlignRight : Qt::AlignLeft;
        drawCellText(p, cell, t, f.cell, c.ink, al, true);
        x += L.w[col];
    }
    p.setPen(QPen(c.grid, 0.45 * S));
    double gx = mx;
    for (int col = 1; col < L.cols; ++col) {
        gx += L.w[col - 1];
        p.drawLine(QPointF(gx, y), QPointF(gx, y + L.rowH));
    }
    p.drawLine(QPointF(mx, y + L.rowH), QPointF(x, y + L.rowH));
}

void drawFooter(QPainter& p, const QRectF& page, double S, const Fonts& f, const Palette& c,
                const Report& r, int pageNo, int pageTotal) {
    const double mx = 24.0 * S;
    const double y = page.height() - 18.0 * S;
    p.setPen(QPen(c.rule, 0.5 * S));
    p.drawLine(QPointF(mx, y), QPointF(page.width() - mx, y));

    p.setFont(f.foot);
    p.setPen(c.sub);
    p.drawText(QRectF(mx, y + 2.0 * S, page.width() * 0.55, 10.0 * S),
               Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("GJG 表格算量 · %1").arg(r.title));
    p.drawText(QRectF(page.width() * 0.35, y + 2.0 * S, page.width() * 0.65 - mx, 10.0 * S),
               Qt::AlignRight | Qt::AlignVCenter,
               QStringLiteral("第 %1 页 / 共 %2 页").arg(pageNo).arg(pageTotal));
}

} // namespace

bool renderToPrinter(QPrinter* printer, const QVector<Report>& reports, QString* err) {
    if (!printer) { if (err) *err = QStringLiteral("打印机不可用"); return false; }
    if (reports.isEmpty()) { if (err) *err = QStringLiteral("报表为空"); return false; }

    QPainter p;
    if (!p.begin(printer)) {
        if (err) *err = QStringLiteral("无法在打印设备上开始绘制");
        return false;
    }
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);

    const double S = std::max(1.0, printer->resolution() / 72.0); // 1pt -> S 设备像素
    const QRectF page = printer->pageLayout().paintRectPixels(printer->resolution());
    if (page.width() <= 0 || page.height() <= 0) {
        p.end();
        if (err) *err = QStringLiteral("页面尺寸无效");
        return false;
    }

    const Fonts fonts = makeFonts(S);
    const Palette pal;
    const double mx = 24.0 * S;
    const double top = 22.0 * S;
    const double bottom = page.height() - 26.0 * S;
    const double availW = page.width() - 2 * mx;
    const double headFirst = 40.0 * S; // 标题块高度
    const double headNext = 18.0 * S;  // 续页标题高度

    // ---- 第一遍：排版 + 分页规划 ----
    struct Plan {
        int report = 0;
        Layout layout;
        QVector<Chunk> chunks;
    };
    QVector<Plan> plans;
    plans.reserve(reports.size());
    int totalPages = 0;
    for (int ri = 0; ri < reports.size(); ++ri) {
        const Report& r = reports[ri];
        Plan plan;
        plan.report = ri;
        plan.layout = layoutTable(r, availW, S, fonts, printer);
        const Layout& L = plan.layout;
        const int n = r.rows.size();
        int i = 0;
        bool first = true;
        do {
            const double y0 = top + (first ? headFirst : headNext) + L.headH + 3.0 * S;
            const int from = i;
            double y = y0;
            while (i < n && y + L.rowH <= bottom) { y += L.rowH; ++i; }
            if (i == from && i < n) ++i; // 行高异常时兜底，避免死循环
            plan.chunks.append(Chunk{ from, i });
            first = false;
        } while (i < n);
        totalPages += plan.chunks.size();
        plans.append(plan);
    }

    // ---- 第二遍：绘制 ----
    int pageNo = 0;
    bool started = false;
    for (const Plan& plan : plans) {
        const Report& r = reports[plan.report];
        const Layout& L = plan.layout;
        for (int ci = 0; ci < plan.chunks.size(); ++ci) {
            if (started) printer->newPage();
            started = true;
            ++pageNo;

            drawTitleBlock(p, page, S, fonts, pal, r, ci == 0, plan.report, reports.size());
            const double hy = top + (ci == 0 ? headFirst : headNext) + 2.0 * S;
            drawColumnHeader(p, page, S, fonts, pal, r, L, hy);

            double y = hy + L.headH;
            const Chunk& ch = plan.chunks[ci];
            for (int row = ch.from; row < ch.to; ++row) {
                drawDataRow(p, S, pal, fonts, L, mx, availW, y, r.rows[row], row - ch.from);
                y += L.rowH;
            }
            // 表格外框
            p.setPen(QPen(pal.edge, 0.7 * S));
            p.setBrush(Qt::NoBrush);
            p.drawRect(QRectF(mx, hy, availW, L.headH + (ch.to - ch.from) * L.rowH));

            drawFooter(p, page, S, fonts, pal, r, pageNo, totalPages);
        }
    }
    p.end();
    return true;
}

bool exportPdf(const QString& path, const QVector<Report>& reports, QString* err) {
    if (path.isEmpty()) { if (err) *err = QStringLiteral("路径为空"); return false; }
    QPrinter pr(QPrinter::HighResolution);
    pr.setOutputFormat(QPrinter::PdfFormat);
    pr.setOutputFileName(path);
    pr.setPageSize(QPageSize(QPageSize::A4));
    pr.setPageOrientation(QPageLayout::Landscape);
    pr.setPageMargins(QMarginsF(0, 0, 0, 0), QPageLayout::Point);
    pr.setFullPage(true);
    pr.setResolution(600);
    pr.setColorMode(QPrinter::Color);
    if (!renderToPrinter(&pr, reports, err)) return false;

    QFileInfo fi(path);
    if (!fi.exists() || fi.size() < 2048) {
        if (err) *err = QStringLiteral("PDF 文件未生成或体积异常");
        return false;
    }
    return true;
}

bool printReports(QWidget* parent, const QVector<Report>& reports, QString* err) {
    QPrinter pr(QPrinter::HighResolution);
    pr.setPageSize(QPageSize(QPageSize::A4));
    pr.setPageOrientation(QPageLayout::Landscape);
    pr.setPageMargins(QMarginsF(0, 0, 0, 0), QPageLayout::Point);
    pr.setFullPage(true);
    pr.setDocName(QStringLiteral("GJG 表格算量报表"));

    QPrintDialog dlg(&pr, parent);
    dlg.setWindowTitle(QStringLiteral("打印 — GJG 表格算量"));
    if (dlg.exec() != QDialog::Accepted) return false;
    return renderToPrinter(&pr, reports, err);
}

bool previewReports(QWidget* parent, const QVector<Report>& reports, QString* err) {
    QPrinter pr(QPrinter::HighResolution);
    pr.setPageSize(QPageSize(QPageSize::A4));
    pr.setPageOrientation(QPageLayout::Landscape);
    pr.setPageMargins(QMarginsF(0, 0, 0, 0), QPageLayout::Point);
    pr.setFullPage(true);
    pr.setDocName(QStringLiteral("GJG 表格算量报表"));

    QPrintPreviewDialog dlg(&pr, parent);
    dlg.setWindowTitle(QStringLiteral("打印预览 — GJG 表格算量"));
    QObject::connect(&dlg, &QPrintPreviewDialog::paintRequested, &dlg,
                     [&](QPrinter* dev) {
                         QString e;
                         if (!renderToPrinter(dev, reports, &e) && err) *err = e;
                     });
    dlg.exec();
    return true;
}

} // namespace reporter
