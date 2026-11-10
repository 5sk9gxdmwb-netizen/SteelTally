#pragma once
#include "models.h"
#include "sections.h"
#include <QStringList>
#include <QVector>
#include <QHash>
#include <cmath>
#include <limits>
#include "sectionlibrary.h"

struct RowQty {
    qint64 count = 0;
    double length = 0, unitWeight = 0, totalWeight = 0, unitArea = 0, totalArea = 0,
           fireArea = 0, primer = 0, mid = 0, top = 0;
};

inline void partUnit(const Part& p, double& uw, double& ua) {
    SectionProps sp = sectionProps(p.spec);
    double L = p.lengthMm / 1000.0;
    uw = sp.massPerM * L;
    ua = sp.surfacePerM * L;
    QJsonObject coefficients = p.calculation;
    if (coefficients.isEmpty()) coefficients = sectionlib::resolve(p.spec);
    if (!coefficients.isEmpty()) {
        bool matches = sectionlib::key(coefficients["spec"].toString()) == sectionlib::key(p.spec);
        for(auto alias:coefficients["aliases"].toArray())
            matches=matches || sectionlib::key(alias.toString())==sectionlib::key(p.spec);
        const bool plate = coefficients["unit"] == "kg/m2";
        const double width = coefficients["width_mm"].toDouble(0);
        if (!matches || !sectionlib::valid(coefficients) || (plate && width <= 0)) {
            uw = ua = std::numeric_limits<double>::quiet_NaN();
        } else {
            double factor = L * (plate ? width / 1000 : 1);
            uw = coefficients["mass"].toDouble() * factor;
            ua = coefficients["surface"].toDouble() * factor;
        }
    } else if (!p.spec.trimmed().isEmpty() && !sp.ok) {
        uw = ua = std::numeric_limits<double>::quiet_NaN();
    }
    if (p.measuredWeightKg >= 0) uw = p.measuredWeightKg;
    if (p.measuredAreaM2 >= 0) ua = p.measuredAreaM2;
}

inline QString calculationIssue(const Project& project) {
    for (const auto& e : project.elements) for (const auto& p : e.parts) {
        double w=0,a=0;partUnit(p,w,a);
        if (!std::isfinite(w) || !std::isfinite(a))
            return QStringLiteral("%1 / %2：规格、板宽或计算来源尚未补齐").arg(e.name,p.name);
    }
    return {};
}

inline RowQty rowCalc(const Element& e, const Part& p) {
    RowQty q;
    partUnit(p, q.unitWeight, q.unitArea);
    q.length = p.lengthMm;
    q.count = qint64(qMax(1, e.count)) * qMax(1, p.count);
    q.totalWeight = q.unitWeight * q.count;
    q.totalArea = q.unitArea * q.count;
    bool hasFire = !p.fireLimit.trimmed().isEmpty() || !p.fireGrade.trimmed().isEmpty() || !p.firePaint.trimmed().isEmpty();
    q.fireArea = hasFire ? q.totalArea : 0.0;
    q.primer = q.totalArea; q.mid = q.totalArea; q.top = q.totalArea;
    return q;
}

inline QStringList summaryColumns() {
    return { QStringLiteral("楼层"), QStringLiteral("名称"), QStringLiteral("零件名称"), QStringLiteral("材质"),
             QStringLiteral("规格"), QStringLiteral("防火时限"), QStringLiteral("防火等级"), QStringLiteral("除锈等级"),
             QStringLiteral("备注"), QStringLiteral("数量（根）"), QStringLiteral("长度（mm）"), QStringLiteral("单重（kg）"),
             QStringLiteral("总重（kg）"), QStringLiteral("单面积（m2）"), QStringLiteral("总面积（m2）"),
             QStringLiteral("防火面积（m2）"), QStringLiteral("底漆（m2）"), QStringLiteral("中间漆（m2）"), QStringLiteral("面漆（m2）") };
}

inline QVector<QStringList> summaryRows(const Project& proj) {
    struct Agg { qint64 count = 0; double length = 0, uw = 0, ua = 0, tw = 0, ta = 0, fa = 0, pr = 0, mid = 0, top = 0; QStringList key; };
    QVector<Agg> aggs;
    QHash<QString, int> idx;
    for (const auto& e : proj.elements) {
        for (const auto& p : e.parts) {
            QString k = e.floor + QChar(0x1f) + e.name + QChar(0x1f) + p.name + QChar(0x1f) + p.material + QChar(0x1f)
                        + p.spec + QChar(0x1f) + p.fireLimit + QChar(0x1f) + p.fireGrade + QChar(0x1f) + p.rustGrade
                        + QChar(0x1f) + p.remark + QChar(0x1f) + QString::number(p.lengthMm, 'g', 15)
                        + QChar(0x1f) + QString::number(p.measuredWeightKg, 'g', 15)
                        + QChar(0x1f) + QString::number(p.measuredAreaM2, 'g', 15);
            RowQty q = rowCalc(e, p);
            int i;
            if (!idx.contains(k)) {
                i = aggs.size(); idx.insert(k, i);
                Agg a; a.length = p.lengthMm; a.uw = q.unitWeight; a.ua = q.unitArea;
                a.key = { e.floor, e.name, p.name, p.material, p.spec, p.fireLimit, p.fireGrade, p.rustGrade, p.remark };
                aggs.push_back(a);
            } else i = idx.value(k);
            Agg& a = aggs[i];
            a.count += q.count; a.tw += q.totalWeight; a.ta += q.totalArea; a.fa += q.fireArea;
            a.pr += q.primer; a.mid += q.mid; a.top += q.top;
        }
    }
    QVector<QStringList> rows;
    for (const auto& a : aggs) {
        QStringList r = a.key;
        r << QString::number(a.count)
          << QString::number(a.length, 'f', 1)
          << QString::number(a.uw, 'f', 3)
          << QString::number(a.tw, 'f', 3)
          << QString::number(a.ua, 'f', 3)
          << QString::number(a.ta, 'f', 3)
          << QString::number(a.fa, 'f', 3)
          << QString::number(a.pr, 'f', 3)
          << QString::number(a.mid, 'f', 3)
          << QString::number(a.top, 'f', 3);
        rows.push_back(r);
    }
    return rows;
}

inline void totals(const Project& proj, double& totalWeight, double& totalArea) {
    totalWeight = 0; totalArea = 0;
    for (const auto& e : proj.elements)
        for (const auto& p : e.parts) {
            RowQty q = rowCalc(e, p);
            totalWeight += q.totalWeight; totalArea += q.totalArea;
        }
}
