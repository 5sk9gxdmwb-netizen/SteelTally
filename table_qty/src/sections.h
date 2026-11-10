#pragma once
#include <QString>
#include <QRegularExpression>
#include <QHash>
#include <functional>

static constexpr double kPi = 3.14159265358979323846;

struct SectionProps { bool ok = false; QString kind; double massPerM = 0.0; double surfacePerM = 0.0; };
inline std::function<SectionProps(const QString&)>& catalogSectionResolver() {
    static std::function<SectionProps(const QString&)> resolver;return resolver;
}

// 标准截面表覆盖：注册后优先于公式计算（用于接入 GB/T 11263/706 等精确理论重量）
inline QHash<QString, SectionProps>& sectionOverrideTable() {
    static QHash<QString, SectionProps> table;
    return table;
}
inline void registerSection(const QString& spec, double massPerM, double surfacePerM, const QString& kind = QStringLiteral("TABLE")) {
    QString key = spec.trimmed().toUpper().replace(QStringLiteral("×"),"X").replace("*","X")
        .replace(QStringLiteral("Φ"),"D").replace(QStringLiteral("φ"),"D").remove(QChar(' '));
    SectionProps p; p.ok = true; p.kind = kind; p.massPerM = massPerM; p.surfacePerM = surfacePerM;
    sectionOverrideTable().insert(key, p);
}

inline SectionProps sectionProps(const QString& specIn) {
    QString s = specIn.trimmed().toUpper().replace(QStringLiteral("×"), QStringLiteral("X"))
                    .replace(QStringLiteral("*"), QStringLiteral("X"))
                    .replace(QStringLiteral("Φ"), QStringLiteral("D"))
                    .replace(QStringLiteral("φ"), QStringLiteral("D"))
                    .remove(QChar(' '));
    if (catalogSectionResolver()) {
        const auto known = catalogSectionResolver()(specIn);
        if (known.ok) return known;
    }
    if (sectionOverrideTable().contains(s)) return sectionOverrideTable().value(s);
    SectionProps r;
    QRegularExpression re;
    // H 型变截面: TH400-300X200X8X12 或 H400-300X200X8X12
    re.setPattern(QStringLiteral(R"(^(?:TH|H)(\d+(?:\.\d+)?)-(\d+(?:\.\d+)?)X(\d+(?:\.\d+)?)X(\d+(?:\.\d+)?)X(\d+(?:\.\d+)?)$)"));
    auto m = re.match(s);
    if (m.hasMatch()) {
        double h1 = m.captured(1).toDouble(), h2 = m.captured(2).toDouble(), b = m.captured(3).toDouble(),
               tw = m.captured(4).toDouble(), tf = m.captured(5).toDouble();
        double h = (h1 + h2) / 2.0;
        double area = 2 * b * tf + (h - 2 * tf) * tw;
        if (!(tw > 0 && tf > 0 && b > tw && h > 2 * tf)) return r;  // 尺寸无效：不给出重量
        r.ok = true; r.kind = QStringLiteral("TH"); r.massPerM = area * 1e-6 * 7850.0; r.surfacePerM = (2 * h + 4 * b) * 1e-3;
        return r;
    }
    // H / I / C 型钢: H400X200X8X13
    re.setPattern(QStringLiteral(R"(^(H|I|C)(\d+(?:\.\d+)?)X(\d+(?:\.\d+)?)X(\d+(?:\.\d+)?)(?:X(\d+(?:\.\d+)?))?$)"));
    m = re.match(s);
    if (m.hasMatch()) {
        QString k = m.captured(1);
        double h = m.captured(2).toDouble(), b = m.captured(3).toDouble(), tw = m.captured(4).toDouble();
        double tf = m.captured(5).isEmpty() ? tw : m.captured(5).toDouble();
        double area = 2 * b * tf + (h - 2 * tf) * tw;
        if (!(tw > 0 && tf > 0 && b > tw && h > 2 * tf)) return r;
        r.ok = true; r.kind = k; r.massPerM = area * 1e-6 * 7850.0; r.surfacePerM = (2 * h + 4 * b) * 1e-3;
        return r;
    }
    // 角钢: L75X5
    re.setPattern(QStringLiteral(R"(^L(\d+(?:\.\d+)?)X(\d+(?:\.\d+)?)$)"));
    m = re.match(s);
    if (m.hasMatch()) {
        double b = m.captured(1).toDouble(), t = m.captured(2).toDouble();
        double area = t * (2 * b - t);
        if (!(t > 0 && b > t)) return r;
        r.ok = true; r.kind = QStringLiteral("L"); r.massPerM = area * 1e-6 * 7850.0; r.surfacePerM = 2 * (2 * b - t) * 1e-3;
        return r;
    }
    // 圆管: PIPE114X4 / D114X4 / Q114X4
    re.setPattern(QStringLiteral(R"(^(?:PIPE|D|Q)(\d+(?:\.\d+)?)X(\d+(?:\.\d+)?)$)"));
    m = re.match(s);
    if (m.hasMatch()) {
        double D = m.captured(1).toDouble(), t = m.captured(2).toDouble();
        if (!(t > 0 && D > 2 * t)) return r;
        double area = kPi / 4.0 * (D * D - (D - 2 * t) * (D - 2 * t));
        r.ok = true; r.kind = QStringLiteral("PIPE"); r.massPerM = area * 1e-6 * 7850.0; r.surfacePerM = kPi * D * 1e-3;
        return r;
    }
    // 方矩管: SHS100X100X5
    re.setPattern(QStringLiteral(R"(^(?:SHS|RHS|F)(\d+(?:\.\d+)?)X(\d+(?:\.\d+)?)X(\d+(?:\.\d+)?)$)"));
    m = re.match(s);
    if (m.hasMatch()) {
        double a = m.captured(1).toDouble(), b = m.captured(2).toDouble(), t = m.captured(3).toDouble();
        double area = a * b - (a - 2 * t) * (b - 2 * t);
        if (!(t > 0 && 2 * t < qMin(a, b))) return r;
        r.ok = true; r.kind = QStringLiteral("SHS"); r.massPerM = area * 1e-6 * 7850.0; r.surfacePerM = 2 * (a + b) * 1e-3;
        return r;
    }
    // 箱型: BOX400X400X12
    re.setPattern(QStringLiteral(R"(^BOX(\d+(?:\.\d+)?)X(\d+(?:\.\d+)?)X(\d+(?:\.\d+)?)$)"));
    m = re.match(s);
    if (m.hasMatch()) {
        double a = m.captured(1).toDouble(), b = m.captured(2).toDouble(), t = m.captured(3).toDouble();
        double area = a * b - (a - 2 * t) * (b - 2 * t);
        if (!(t > 0 && 2 * t < qMin(a, b))) return r;
        r.ok = true; r.kind = QStringLiteral("BOX"); r.massPerM = area * 1e-6 * 7850.0; r.surfacePerM = 2 * (a + b) * 1e-3;
        return r;
    }
    // 圆钢: ROD20
    re.setPattern(QStringLiteral(R"(^ROD(\d+(?:\.\d+)?)$)"));
    m = re.match(s);
    if (m.hasMatch()) {
        double d = m.captured(1).toDouble();
        if (!(d > 0)) return r;
        double area = kPi / 4.0 * d * d;
        r.ok = true; r.kind = QStringLiteral("ROD"); r.massPerM = area * 1e-6 * 7850.0; r.surfacePerM = kPi * d * 1e-3;
        return r;
    }
    // C 型檩条: CZ200X70X20X2.5 (h,b,卷边,厚)
    re.setPattern(QStringLiteral(R"(^(CZ|ZZ)(\d+(?:\.\d+)?)X(\d+(?:\.\d+)?)X(\d+(?:\.\d+)?)X(\d+(?:\.\d+)?)$)"));
    m = re.match(s);
    if (m.hasMatch()) {
        double h = m.captured(2).toDouble(), b = m.captured(3).toDouble(),
               lip = m.captured(4).toDouble(), t = m.captured(5).toDouble();
        if (!(t > 0 && h > 2 * t && b > 2 * t)) return r;
        double dev = h + 2 * b + 2 * lip - 4 * t;
        r.ok = true; r.kind = m.captured(1); r.massPerM = dev * t * 1e-6 * 7850.0;
        r.surfacePerM = (2 * (h + b) + 2 * lip) * 1e-3;
        return r;
    }
    // 扁钢: FB100X10
    re.setPattern(QStringLiteral(R"(^FB(\d+(?:\.\d+)?)X(\d+(?:\.\d+)?)$)"));
    m = re.match(s);
    if (m.hasMatch()) {
        double b = m.captured(1).toDouble(), t = m.captured(2).toDouble();
        if (!(b > 0 && t > 0)) return r;
        r.ok = true; r.kind = QStringLiteral("FB"); r.massPerM = b * t * 1e-6 * 7850.0; r.surfacePerM = 2 * (b + t) * 1e-3;
        return r;
    }
    // 钢板: PL10
    re.setPattern(QStringLiteral(R"(^PL(\d+(?:\.\d+)?)$)"));
    m = re.match(s);
    if (m.hasMatch()) {
        double t = m.captured(1).toDouble();
        r.ok = true; r.kind = QStringLiteral("PL"); r.massPerM = 7.85 * t; r.surfacePerM = 1.0;
        return r;
    }
    return r;
}
