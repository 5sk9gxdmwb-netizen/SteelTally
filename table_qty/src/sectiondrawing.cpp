#include "sectiondrawing.h"
#include "sectionlibrary.h"
#include <QtWidgets>
#include <cmath>

namespace {
using Path = QPainterPath;
QString num(double v) { return QString::number(v, 'g', 7); }
QString note(const QString& name, double v) { return name + QStringLiteral(" = ") + num(v); }

// I/H with independent flange widths (top bt, bottom bb) around a common web axis.
// Concave quarter arcs at the web roots add (1-pi/4)r² each; r must already fit.
Path ibeam(double h, double bt, double bb, double tw, double t1, double t2, double r) {
    const double W = qMax(bt, bb), cx = W / 2, xl = cx - tw / 2, xr = cx + tw / 2, y = h - t2;
    const double a0 = cx - bt / 2, a1 = cx + bt / 2, c0 = cx - bb / 2, c1 = cx + bb / 2;
    Path p;
    p.moveTo(a0, 0); p.lineTo(a1, 0); p.lineTo(a1, t1); p.lineTo(xr + r, t1);
    if (r > 0) p.arcTo(QRectF(xr, t1, 2 * r, 2 * r), 90, 90);
    p.lineTo(xr, y - r);
    if (r > 0) p.arcTo(QRectF(xr, y - 2 * r, 2 * r, 2 * r), 180, 90);
    p.lineTo(c1, y); p.lineTo(c1, h); p.lineTo(c0, h); p.lineTo(c0, y); p.lineTo(xl - r, y);
    if (r > 0) p.arcTo(QRectF(xl - 2 * r, y - 2 * r, 2 * r, 2 * r), 270, 90);
    p.lineTo(xl, t1 + r);
    if (r > 0) p.arcTo(QRectF(xl - 2 * r, t1, 2 * r, 2 * r), 0, 90);
    p.lineTo(a0, t1); p.closeSubpath(); return p;
}
Path tee(double h, double b, double tw, double tf, double r) {
    const double xl = (b - tw) / 2, xr = xl + tw;
    Path p; p.moveTo(0, 0); p.lineTo(b, 0); p.lineTo(b, tf); p.lineTo(xr + r, tf);
    if (r > 0) p.arcTo(QRectF(xr, tf, 2 * r, 2 * r), 90, 90);
    p.lineTo(xr, h); p.lineTo(xl, h); p.lineTo(xl, tf + r);
    if (r > 0) p.arcTo(QRectF(xl - 2 * r, tf, 2 * r, 2 * r), 0, 90);
    p.lineTo(0, tf); p.closeSubpath(); return p;
}
Path channel(double h, double b, double tw, double t1, double t2, double r) {
    const double y = h - t2;
    Path p; p.moveTo(0, 0); p.lineTo(b, 0); p.lineTo(b, t1); p.lineTo(tw + r, t1);
    if (r > 0) p.arcTo(QRectF(tw, t1, 2 * r, 2 * r), 90, 90);
    p.lineTo(tw, y - r);
    if (r > 0) p.arcTo(QRectF(tw, y - 2 * r, 2 * r, 2 * r), 180, 90);
    p.lineTo(b, y); p.lineTo(b, h); p.lineTo(0, h); p.closeSubpath(); return p;
}
// Vertical leg l1 (h), horizontal leg l2 (b); toe radii are not drawn.
Path angle(double h, double b, double t, double r) {
    Path p; p.moveTo(0, 0); p.lineTo(t, 0); p.lineTo(t, h - t - r);
    if (r > 0) p.arcTo(QRectF(t, h - t - 2 * r, 2 * r, 2 * r), 180, 90);
    p.lineTo(b, h - t); p.lineTo(b, h); p.lineTo(0, h); p.closeSubpath(); return p;
}
Path lipC(double h, double b, double c, double t) {
    Path p; p.moveTo(0, 0); p.lineTo(b, 0); p.lineTo(b, c); p.lineTo(b - t, c); p.lineTo(b - t, t);
    p.lineTo(t, t); p.lineTo(t, h - t); p.lineTo(b - t, h - t); p.lineTo(b - t, h - c);
    p.lineTo(b, h - c); p.lineTo(b, h); p.lineTo(0, h); p.closeSubpath(); return p;
}
// Flange length b is measured from the web's outer face; overall width is 2b-t.
Path lipZ(double h, double b, double c, double t) {
    const double x0 = b - t, W = 2 * b - t;
    Path p; p.moveTo(x0, 0); p.lineTo(W, 0); p.lineTo(W, c); p.lineTo(W - t, c); p.lineTo(W - t, t);
    p.lineTo(b, t); p.lineTo(b, h); p.lineTo(0, h); p.lineTo(0, h - c); p.lineTo(t, h - c);
    p.lineTo(t, h - t); p.lineTo(x0, h - t); p.closeSubpath(); return p;
}
Path ring(double d, double t) {
    Path p; p.setFillRule(Qt::OddEvenFill); p.addEllipse(QRectF(0, 0, d, d)); p.addEllipse(QRectF(t, t, d - 2 * t, d - 2 * t)); return p;
}
Path box(double h, double b, double tw, double tt, double tb, double r) {
    Path p; p.setFillRule(Qt::OddEvenFill); p.addRoundedRect(QRectF(0, 0, b, h), r, r, Qt::AbsoluteSize);
    const double ri = qMax(0.0, r - tw);
    p.addRoundedRect(QRectF(tw, tt, b - 2 * tw, h - tt - tb), ri, ri, Qt::AbsoluteSize); return p;
}
Path regular(int n, double acrossFlats, double startDeg) {
    const double R = acrossFlats / (2 * std::cos(kPi / n));
    Path p;
    for (int i = 0; i < n; ++i) {
        const double a = (startDeg + 360.0 * i / n) * kPi / 180;
        const QPointF q(R * std::cos(a), R * std::sin(a));
        i ? p.lineTo(q) : p.moveTo(q);
    }
    p.closeSubpath(); return p.translated(-p.boundingRect().topLeft());
}
} // namespace

SectionDrawing::SectionDrawing(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("sectionDrawing"));
    setMinimumSize(300, 300);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAccessibleName(QStringLiteral("截面尺寸图"));
}

// Custom shapes: the record was produced by sectionlib::formula, so its dimensions already fit.
static bool customGeo(const QJsonObject& d, SectionGeo& g) {
    const int type = d["shape"].toInt();
    const double h = d["h"].toDouble(), b = d["b"].toDouble(), tw = d["tw"].toDouble(), tf = d["tf"].toDouble();
    const double r = d["radius"].toDouble(), c = d["c"].toDouble();
    g.w = b; g.h = h;
    switch (type) {
    case 0: g.path = ibeam(h, b, b, tw, tf, tf, r);
        g.leaders = {{{b / 2, h / 2}, "tw"}, {{b, tf / 2}, "tf"}};
        g.notes = {note("tw", tw), note("tf", tf), note("r", r)}; break;
    case 1: g.path = ring(h, tw); g.w = g.h = h; g.wLabel = "D"; g.showH = false;
        g.leaders = {{{h - tw / 2, h / 2}, "t"}}; g.notes = {note("t", tw)}; break;
    case 2: g.path = box(h, b, tw, tw, tw, r); g.leaders = {{{b, h - tw / 2}, "t"}};
        g.notes = {note("t", tw), note("r", r)}; break;
    case 3: g.path.addEllipse(QRectF(0, 0, h, h)); g.w = g.h = h; g.wLabel = "D"; g.showH = false; break;
    case 4: g.path = angle(h, b, tw, 0); g.leaders = {{{b, h - tw / 2}, "t"}}; g.notes = {note("t", tw)}; break;
    case 5: g.path.addRect(QRectF(0, 0, h, tw)); g.w = h; g.h = tw; g.showH = false;
        g.leaders = {{{h, tw / 2}, "t"}}; g.notes = {note("t", tw)}; break;
    case 6: g.path = tee(h, b, tw, tf, r); g.leaders = {{{b / 2, h * 0.7}, "tw"}, {{b, tf / 2}, "tf"}};
        g.notes = {note("tw", tw), note("tf", tf), note("r", r)}; break;
    case 7: g.path = channel(h, b, tw, tf, tf, r); g.leaders = {{{tw / 2, h / 2}, "tw"}, {{b, tf / 2}, "tf"}};
        g.notes = {note("tw", tw), note("tf", tf), note("r", r)}; break;
    case 8: g.path = lipC(h, b, c, tw); g.leaders = {{{tw / 2, h / 2}, "t"}};
        g.notes = {note("t", tw), note("c", c)}; break;
    case 9: g.path = lipZ(h, b, c, tw); g.w = 2 * b - tw; g.leaders = {{{b - tw / 2, h / 2}, "t"}};
        g.notes = {note("t", tw), note("c", c), note("b", b)}; break;
    case 10: g.path.addRect(QRectF(0, 0, b, h)); break;
    case 11: g.path = regular(6, h, 0); g.w = g.path.boundingRect().width(); g.hLabel = "s"; g.showW = false; break;
    case 12: g.path = regular(8, h, 22.5); g.w = h; g.hLabel = "s"; g.showW = false; break;
    case 13: g.path = box(h, b, tw, tf, c, 0);
        g.leaders = {{{b, h - c / 2}, "c"}, {{b, tf / 2}, "tf"}, {{b - tw / 2, h / 2}, "tw"}};
        g.notes = {note("tw", tw), note("tf", tf), note("c", c)}; break;
    case 14: g.w = qMax(b, c); g.path = ibeam(h, b, c, tw, tf, tf, 0); g.wLabel = "Bmax";
        g.leaders = {{{g.w / 2, h / 2}, "tw"}, {{(g.w + b) / 2, tf / 2}, "tf"}};
        g.notes = {note("上翼缘 b", b), note("下翼缘 c", c), note("tw", tw), note("tf", tf)}; break;
    default: return false;
    }
    return true;
}

// Standard shapes: schematic drawings from explicit catalogue parameters only.
// Missing, non-positive or non-fitting required values leave the drawing unavailable.
static bool standardGeo(const QString& family, QHash<QString, double> v, SectionGeo& g) {
    auto has = [&](std::initializer_list<const char*> keys) {
        for (auto k : keys) if (!(v.value(QString::fromLatin1(k), -1) > 0)) return false;
        return true;
    };
    auto fit = [&](double r, double limit) { // a fillet radius that does not fit is not drawn
        if (r <= 0) return 0.0;
        if (r > limit) { g.notes << QStringLiteral("r 超出可绘范围，未绘出"); return 0.0; }
        return r;
    };
    const auto F = [&](const char* s) { return family == QString::fromUtf8(s); };
    const double r = v.value("r", 0);
    if (F("工字钢") || F("槽钢")) {
        if (!has({"h", "b", "d", "t"}) || v["h"] <= 2 * v["t"] || v["b"] <= v["d"]) return false;
        const double h = v["h"], b = v["b"], d = v["d"], t = v["t"];
        const bool isI = F("工字钢");
        const double rr = fit(r, isI ? qMin((b - d) / 2, (h - 2 * t) / 2) : qMin(b - d, (h - 2 * t) / 2));
        g.w = b; g.h = h;
        g.path = isI ? ibeam(h, b, b, d, t, t, rr) : channel(h, b, d, t, t, rr);
        g.leaders = {{{isI ? b / 2 : d / 2, h / 2}, "d"}, {{b, t / 2}, "t"}};
        g.notes << note("d", d) << note("t", t) << note("r", r) << QStringLiteral("翼缘内侧斜度、r1 未绘出");
    } else if (F("H型钢") || F("焊接H型钢")) {
        // Rolled rows use t1/t2; welded and older JSON rows use one tf for both flanges.
        const bool weld = F("焊接H型钢") || !v.contains("t1");
        const char* a = weld ? "tf" : "t1"; const char* z = weld ? "tf" : "t2";
        if (!has({"h", "b", "tw", a, z}) || v["h"] <= v[a] + v[z] || v["b"] <= v["tw"]) return false;
        const double rr = fit(r, qMin((v["b"] - v["tw"]) / 2, (v["h"] - v[a] - v[z]) / 2));
        g.w = v["b"]; g.h = v["h"]; g.path = ibeam(g.h, g.w, g.w, v["tw"], v[a], v[z], rr);
        g.leaders = {{{g.w / 2, g.h / 2}, "tw"}, {{g.w, v[a] / 2}, "tf"}};
        g.notes << note("tw", v["tw"]);
        if (weld) g.notes << note("tf", v["tf"]);
        else g.notes << note("t1", v["t1"]) << note("t2", v["t2"]) << note("r", r);
    } else if (F("T型钢")) {
        // Rolled rows: t1 web, t2 flange. Older JSON rows name them tw and tf.
        const char* wk = v.contains("t1") ? "t1" : "tw"; const char* fk = v.contains("t2") ? "t2" : "tf";
        if (!has({"h", "b", wk, fk}) || v[fk] >= v["h"] || v[wk] >= v["b"]) return false;
        const double rr = fit(r, qMin((v["b"] - v[wk]) / 2, v["h"] - v[fk]));
        g.w = v["b"]; g.h = v["h"]; g.path = tee(g.h, g.w, v[wk], v[fk], rr);
        g.leaders = {{{g.w / 2, g.h * 0.7}, "tw"}, {{g.w, v[fk] / 2}, "tf"}};
        g.notes << note("腹板", v[wk]) << note("翼缘", v[fk]) << note("r", r);
    } else if (F("角钢") || F("不等边角钢")) {
        const bool eq = F("角钢");
        const double l1 = eq ? v.value("l") : v.value("l1"), l2 = eq ? v.value("l") : v.value("l2"), t = v.value("t");
        if (!(l1 > 0 && l2 > 0 && t > 0) || qMin(l1, l2) <= t) return false;
        g.w = l2; g.h = l1; g.wLabel = eq ? "l" : "l2"; g.hLabel = eq ? "l" : "l1";
        g.path = angle(l1, l2, t, fit(r, qMin(l1, l2) - t));
        g.leaders = {{{l2, l1 - t / 2}, "t"}};
        g.notes << note("t", t) << note("r", r) << QStringLiteral("趾部圆角 r1 未绘出");
    } else if (F("圆钢") || F("圆管")) {
        // Native catalogue parameter d is a radius (D5.5 -> d=2.75).
        const bool tube = F("圆管");
        if (!has({"d"}) || (tube && (!has({"t"}) || v["t"] >= v["d"]))) return false;
        const double D = 2 * v["d"];
        g.w = g.h = D; g.wLabel = "D"; g.showH = false;
        if (tube) { g.path = ring(D, v["t"]); g.leaders = {{{D - v["t"] / 2, D / 2}, "t"}}; g.notes << note("t", v["t"]); }
        else g.path.addEllipse(QRectF(0, 0, D, D));
    } else if (F("C型钢") || F("Z型钢")) {
        const double h = v.value("h"), b = v.value("b"), c = v.value("c"), t = v.value("t");
        if (!has({"h", "b", "c", "t"}) || c <= t || h <= 2 * t || b <= 2 * t || 2 * c >= h) return false;
        const bool z = F("Z型钢");
        g.w = z ? 2 * b - t : b; g.h = h; g.path = z ? lipZ(h, b, c, t) : lipC(h, b, c, t);
        g.leaders = {{{z ? b - t / 2 : t / 2, h / 2}, "t"}};
        g.notes << note("t", t) << note("c", c) << note("b", b) << QStringLiteral("c 的量取口径以原标准为准");
    } else if (F("热轧扁钢") || F("方钢")) {
        if (!has({"b", "t"})) return false;
        g.w = v["b"]; g.h = v["t"]; g.path.addRect(QRectF(0, 0, g.w, g.h)); g.hLabel = "t";
        g.leaders = {{{g.w, g.h / 2}, "t"}};
    } else if (F("矩形管")) {
        // Native rows give b and d; older JSON rows give a (height) and b.
        const char* hk = v.contains("d") ? "d" : "a";
        if (!has({"b", hk, "t"}) || qMin(v["b"], v[hk]) <= 2 * v["t"]) return false;
        g.w = v["b"]; g.h = v[hk]; g.hLabel = hk; g.path = box(g.h, g.w, v["t"], v["t"], v["t"], 0);
        g.leaders = {{{g.w, g.h - v["t"] / 2}, "t"}};
        g.notes << note("t", v["t"]) << QStringLiteral("外圆角未提供，未绘出");
    } else if (F("焊接箱形")) {
        if (!has({"a", "b", "t"}) || qMin(v["a"], v["b"]) <= 2 * v["t"]) return false;
        double R = v.value("r", 0);
        if (R > 0 && (R < v["t"] || R > qMin(v["a"], v["b"]) / 2)) { g.notes << QStringLiteral("R 超出可绘范围，未绘出"); R = 0; }
        g.w = v["b"]; g.h = v["a"]; g.hLabel = "a"; g.path = box(g.h, g.w, v["t"], v["t"], v["t"], R);
        g.leaders = {{{g.w, g.h - v["t"] / 2}, "t"}}; g.notes << note("t", v["t"]) << note("R", R);
    } else if (F("钢板") || F("花纹钢板")) {
        // Edge view of a plate: the strip length is arbitrary and is not a catalogue dimension.
        if (!has({"t"})) return false;
        g.h = v["t"]; g.w = 16 * g.h; g.path.addRect(QRectF(0, 0, g.w, g.h)); g.showW = false; g.hLabel = "t";
        g.notes << QStringLiteral("板边示意；板宽不属于截面参数");
    } else return false;
    return true;
}

void SectionDrawing::setRecord(const QJsonObject& record, bool standardRecord) {
    dimensions = {}; geo = {}; standard = standardRecord; caption = record["spec"].toString();
    message = QStringLiteral("请选择有效截面尺寸");
    // Drawing needs only geometry; catalogue rows lacking a surface coefficient are still drawn.
    if (!record.isEmpty() && (standard ? !record["id"].toString().isEmpty() : sectionlib::valid(record))) {
        message = QStringLiteral("该标准条目未提供完整、自洽的截面几何参数，未绘制\n仍使用原库重量与表面积系数");
        if (!standard) { if (customGeo(record, geo)) dimensions = record; }
        else {
            // Only explicit source dimensions are used. Never infer geometry from mass.
            QHash<QString, double> values;
            const QRegularExpression expression(QStringLiteral("([A-Za-z0-9]+)\\s*=\\s*([0-9]+(?:\\.[0-9]+)?)"));
            auto matches = expression.globalMatch(record["parameters"].toString());
            while (matches.hasNext()) { const auto m = matches.next(); values[m.captured(1).toLower()] = m.captured(2).toDouble(); }
            const auto json = QJsonDocument::fromJson(record["parameters"].toString().toUtf8()).object();
            for (auto it = json.begin(); it != json.end(); ++it) if (it.value().isDouble()) values[it.key().toLower()] = it.value().toDouble();
            if (standardGeo(record["family"].toString(), values, geo))
                dimensions = {{"family", record["family"]}, {"shape", -1}, {"standard", true}};
        }
        if (dimensions.isEmpty()) geo = {};
    }
    setAccessibleDescription(dimensions.isEmpty() ? message : caption + QStringLiteral("，尺寸单位毫米"));
    update();
}

void SectionDrawing::paintEvent(QPaintEvent*) {
    QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
    const auto pal = palette(); const QColor ink = pal.color(QPalette::Text);
    const bool dark = ink.lightness() > 150;
    const QColor accent = dark ? QColor("#77b6eb") : QColor("#2976ae");
    p.fillRect(rect(), dark ? QColor("#242526") : QColor("#ffffff"));
    p.setPen(ink);
    QFont title = font(); title.setBold(true); p.setFont(title);
    p.drawText(QRectF(12, 10, width() - 24, 30), Qt::AlignCenter, caption.isEmpty() ? QStringLiteral("截面预览") : caption);
    p.setFont(font());
    if (dimensions.isEmpty()) {
        p.drawText(rect().adjusted(20, 48, -20, -30), Qt::AlignCenter | Qt::TextWordWrap, message); return;
    }
    const double gw = geo.w, gh = geo.h;
    const QRectF viewport(68, 82, qMax(30, width() - 152), qMax(30, height() - 190));
    const double scale = qMin(viewport.width() / gw, viewport.height() / gh);
    const QPointF origin(viewport.center().x() - gw * scale / 2, viewport.center().y() - gh * scale / 2);
    auto at = [&](double x, double y) { return origin + QPointF(x * scale, y * scale); };
    p.save(); p.translate(origin); p.scale(scale, scale);
    QPen outline(ink, 1.5); outline.setCosmetic(true); p.setPen(outline);
    QColor fill = accent; fill.setAlpha(65); p.setBrush(fill); p.drawPath(geo.path); p.restore();
    p.setPen(QPen(accent, 1, Qt::DashLine));
    p.drawLine(at(gw / 2, 0) - QPointF(0, 12), at(gw / 2, gh) + QPointF(0, 12));
    p.drawLine(at(0, gh / 2) - QPointF(12, 0), at(gw, gh / 2) + QPointF(12, 0));
    p.setPen(QPen(ink, 1));
    if (geo.showW) {
        const double y = origin.y() - 22; const QPointF a = at(0, 0), z = at(gw, 0);
        p.drawLine(a, QPointF(a.x(), y + 5)); p.drawLine(z, QPointF(z.x(), y + 5));
        p.drawLine(QPointF(a.x(), y), QPointF(z.x(), y));
        for (double x : {a.x(), z.x()}) p.drawLine(QPointF(x - 3, y + 3), QPointF(x + 3, y - 3));
        p.drawText(QRectF(a.x() - 20, y - 25, z.x() - a.x() + 40, 22), Qt::AlignCenter, geo.wLabel + QStringLiteral(" = ") + num(gw));
    }
    if (geo.showH) {
        const double x = origin.x() - 24;
        p.drawLine(at(0, 0), QPointF(x - 5, origin.y())); p.drawLine(at(0, gh), QPointF(x - 5, origin.y() + gh * scale));
        p.drawLine(QPointF(x, origin.y()), QPointF(x, origin.y() + gh * scale));
        p.save(); p.translate(x - 8, origin.y() + gh * scale / 2); p.rotate(-90);
        p.drawText(QRectF(-80, -22, 160, 22), Qt::AlignCenter, geo.hLabel + QStringLiteral(" = ") + num(gh)); p.restore();
    }
    // Thickness leaders remain readable even when true-scale walls are subpixel.
    const double lx = origin.x() + gw * scale + 8;
    double ly = origin.y() + 18;
    for (const auto& l : geo.leaders) {
        p.drawLine(at(l.anchor.x(), l.anchor.y()), QPointF(lx, ly)); p.drawLine(QPointF(lx, ly), QPointF(lx + 12, ly));
        p.drawText(QRectF(lx + 15, ly - 11, 40, 22), Qt::AlignLeft | Qt::AlignVCenter, l.text);
        ly += 24;
    }
    p.drawText(QRectF(12, height() - 92, width() - 24, 34), Qt::AlignCenter | Qt::TextWordWrap, geo.notes.join(QStringLiteral("    ")));
    p.setPen(pal.color(QPalette::PlaceholderText));
    p.drawText(QRectF(12, height() - 58, width() - 24, 48), Qt::AlignCenter | Qt::TextWordWrap,
        standard ? QStringLiteral("尺寸单位 mm；标准截面几何示意\n不依据图形重算原库系数") : QStringLiteral("尺寸单位 mm；等比例预览\n表面积口径以计算说明为准"));
}
