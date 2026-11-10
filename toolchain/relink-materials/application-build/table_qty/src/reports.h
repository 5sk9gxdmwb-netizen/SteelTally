#pragma once
#include "models.h"
#include "calc.h"
#include "sections.h"
#include <QStringList>
#include <QVector>
#include <QMap>
#include <QHash>
#include <QRegularExpression>
#include <functional>

struct Report {
    int id = 0;
    QString group;
    QString title;
    QStringList headers;
    QVector<QStringList> rows;
};

inline bool partIsPlate(const Part& p) {
    const auto shape = p.measurement.value("shape").toString();
    return sectionProps(p.spec).kind == QStringLiteral("PL") ||
           p.calculation["unit"] == "kg/m2" || p.calculation["shape"].toInt(-1) == 5 ||
           shape == "rectangle" || shape == "triangle" || shape == "ring";
}
inline bool partIsBolt(const Element& e, const Part& p) {
    return e.typeName.contains(QStringLiteral("螺栓")) ||
           QRegularExpression(QStringLiteral("^M\\d+")).match(p.spec.toUpper()).hasMatch();
}
inline bool partIsCotter(const Element& e, const Part&) { return e.typeName.contains(QStringLiteral("栓钉")); }
inline bool partIsAnchor(const Element& e, const Part&) { return e.typeName.contains(QStringLiteral("锚栓")); }
inline QString craftOf(const Part& p) {
    if (p.measurement.value("method").toString() == "drawing_net_weight")
        return QStringLiteral("abnormal");
    QString s = p.spec.toUpper();
    if (s.startsWith(QStringLiteral("WH")) || p.spec.contains(QStringLiteral("焊"))) return QStringLiteral("welded");
    if (s.startsWith(QStringLiteral("BOX")) || s.startsWith(QStringLiteral("Z")) ||
        p.spec.contains(QStringLiteral("异形"))) return QStringLiteral("abnormal");
    return QStringLiteral("hot");
}

struct GroupAgg { QStringList key; qint64 count = 0; double length = 0, weight = 0, area = 0; };

inline QVector<GroupAgg> aggregate(const Project& proj,
                                   const std::function<bool(const Element&, const Part&)>& filter,
                                   const std::function<QStringList(const Element&, const Part&)>& keyFn) {
    QVector<GroupAgg> out;
    QHash<QString, int> idx;
    for (const auto& e : proj.elements) {
        for (const auto& p : e.parts) {
            if (!filter(e, p)) continue;
            QStringList key = keyFn(e, p);
            QString k = key.join(QChar(0x1f)) + QChar(0x1f)
                      + QString::number(p.lengthMm, 'g', 15) + QChar(0x1f)
                      + QString::number(p.measuredWeightKg, 'g', 15) + QChar(0x1f)
                      + QString::number(p.measuredAreaM2, 'g', 15) + QChar(0x1f)
                      + p.measurement.value("shape").toString();
            RowQty q = rowCalc(e, p);
            int i;
            if (!idx.contains(k)) {
                i = out.size(); idx.insert(k, i);
                GroupAgg g; g.key = key; g.length = p.lengthMm; out.push_back(g);
            } else i = idx.value(k);
            GroupAgg& g = out[i];
            g.count += q.count; g.weight += q.totalWeight; g.area += q.totalArea;
        }
    }
    return out;
}

inline QString f3(double v) { return std::isfinite(v) ? QString::number(v, 'f', 3) : QStringLiteral("待补充"); }
inline QString f1(double v) { return std::isfinite(v) ? QString::number(v, 'f', 1) : QStringLiteral("待补充"); }

inline QVector<Report> buildAllReports(const Project& proj) {
    QVector<Report> reports;

    // id0 清单汇总表
    {
        Report r; r.id = 0; r.group = QStringLiteral("做法汇总分析"); r.title = QStringLiteral("清单汇总表");
        r.headers = { QStringLiteral("序号"), QStringLiteral("编码"), QStringLiteral("项目名称"),
                      QStringLiteral("总重(kg)"), QStringLiteral("总面积(m²)"), QStringLiteral("总数量(套)") };
        QMap<QString, GroupAgg> byType; QStringList order;
        for (const auto& e : proj.elements) {
            if (!byType.contains(e.typeName)) { GroupAgg g; g.key = { e.typeName }; byType.insert(e.typeName, g); order << e.typeName; }
            GroupAgg& g = byType[e.typeName];
            g.count += e.count;
            for (const auto& p : e.parts) { RowQty q = rowCalc(e, p); g.weight += q.totalWeight; g.area += q.totalArea; }
        }
        int i = 1;
        for (const auto& t : order) {
            const GroupAgg& g = byType[t];
            r.rows.push_back({ QString::number(i), QStringLiteral("BQ%1").arg(i, 3, 10, QChar('0')),
                               t, f3(g.weight), f3(g.area), QString::number(g.count) });
            ++i;
        }
        reports.push_back(r);
    }

    // id1 定额汇总表
    {
        Report r; r.id = 1; r.group = QStringLiteral("做法汇总分析"); r.title = QStringLiteral("定额汇总表");
        r.headers = { QStringLiteral("序号"), QStringLiteral("编码"), QStringLiteral("项目名称"),
                      QStringLiteral("项目特征"), QStringLiteral("单位"), QStringLiteral("工程量") };
        QMap<QString, GroupAgg> byType; QMap<QString, QStringList> specs; QStringList order;
        for (const auto& e : proj.elements) {
            if (!byType.contains(e.typeName)) { GroupAgg g; g.key = { e.typeName }; byType.insert(e.typeName, g); order << e.typeName; }
            GroupAgg& g = byType[e.typeName];
            g.count += e.count;
            for (const auto& p : e.parts) {
                RowQty q = rowCalc(e, p); g.weight += q.totalWeight; g.area += q.totalArea;
                if (!specs[e.typeName].contains(p.spec)) specs[e.typeName] << p.spec;
            }
        }
        int i = 1;
        for (const auto& t : order) {
            const GroupAgg& g = byType[t];
            r.rows.push_back({ QString::number(i), QStringLiteral("DE%1").arg(i, 3, 10, QChar('0')), t,
                               specs[t].join(QStringLiteral("、")), QStringLiteral("kg"), f3(g.weight) });
            ++i;
        }
        reports.push_back(r);
    }

    // id2 构件汇总表
    {
        Report r; r.id = 2; r.group = QStringLiteral("构件汇总分析"); r.title = QStringLiteral("构件汇总表");
        r.headers = { QStringLiteral("构件类型"), QStringLiteral("数量"), QStringLiteral("总重(kg)"), QStringLiteral("总面积(m²)"),
                      QStringLiteral("防火面积(m²)"), QStringLiteral("底漆(m²)"), QStringLiteral("中间漆(m²)"),
                      QStringLiteral("面漆(m²)"), QStringLiteral("螺栓(总)"), QStringLiteral("栓钉(总)") };
        QMap<QString, GroupAgg> byType; QMap<QString, qint64> boltCnt, cotterCnt; QStringList order;
        for (const auto& e : proj.elements) {
            if (!byType.contains(e.typeName)) { GroupAgg g; g.key = { e.typeName }; byType.insert(e.typeName, g); order << e.typeName; }
            GroupAgg& g = byType[e.typeName];
            g.count += e.count;
            for (const auto& p : e.parts) {
                RowQty q = rowCalc(e, p);
                g.weight += q.totalWeight; g.area += q.totalArea;
                if (partIsBolt(e, p)) boltCnt[e.typeName] += q.count;
                if (partIsCotter(e, p)) cotterCnt[e.typeName] += q.count;
            }
        }
        for (const auto& t : order) {
            const GroupAgg& g = byType[t];
            // 防火/底漆/中间漆/面漆 以总面积近似（可按防火/涂装单独系数）
            r.rows.push_back({ t, QString::number(g.count), f3(g.weight), f3(g.area), f3(g.area), f3(g.area),
                               f3(g.area), f3(g.area), QString::number(boltCnt.value(t)), QString::number(cotterCnt.value(t)) });
        }
        reports.push_back(r);
    }

    // id3 三维算量构件汇总表（多 图元ID 列）
    {
        Report r; r.id = 3; r.group = QStringLiteral("构件汇总分析"); r.title = QStringLiteral("三维算量构件汇总表");
        r.headers = summaryColumns();
        r.headers.insert(3, QStringLiteral("图元ID"));
        for (const auto& row : summaryRows(proj)) { QStringList c = row; c.insert(3, QString()); r.rows.push_back(c); }
        reports.push_back(r);
    }

    // id4 表格算量构件汇总表
    {
        Report r; r.id = 4; r.group = QStringLiteral("构件汇总分析"); r.title = QStringLiteral("表格算量构件汇总表");
        r.headers = summaryColumns();
        r.rows = summaryRows(proj);
        reports.push_back(r);
    }

    // id5 三维算量构件计算书
    {
        Report r; r.id = 5; r.group = QStringLiteral("构件汇总分析"); r.title = QStringLiteral("三维算量构件计算书");
        r.headers = { QStringLiteral("序号"), QStringLiteral("零件名称"), QStringLiteral("材质"), QStringLiteral("规格"),
                      QStringLiteral("数量"), QStringLiteral("总数量"), QStringLiteral("长度(mm)"),
                      QStringLiteral("重量计算式文字说明"), QStringLiteral("重量计算式"), QStringLiteral("单重(kg)"),
                      QStringLiteral("总重(kg)"), QStringLiteral("单面积(m²)"), QStringLiteral("总面积(m²)"),
                      QStringLiteral("总底漆(m²)"), QStringLiteral("总中间漆(m²)"), QStringLiteral("总面漆(m²)"),
                      QStringLiteral("总防火面积(m²)") };
        int seq = 1;
        for (const auto& e : proj.elements) {
            for (const auto& p : e.parts) {
                RowQty q = rowCalc(e, p);
                double perM = p.lengthMm > 0 ? q.unitWeight / (p.lengthMm / 1000.0) : 0.0;
                QString formula = QStringLiteral("%1×%2").arg(f3(perM)).arg(f1(p.lengthMm / 1000.0));
                QString explain = QStringLiteral("线密度%1kg/m × 长度%2m").arg(f3(perM)).arg(f1(p.lengthMm / 1000.0));
                if (p.measuredWeightKg >= 0) {
                    formula = f3(p.measuredWeightKg);
                    explain = QStringLiteral("参数/图纸单件净重；依据：%1").arg(
                        p.measurement.value("source").toString(p.remark));
                }
                r.rows.push_back({ QString::number(seq++), p.name, p.material, p.spec,
                                   QString::number(p.count), QString::number(q.count), f1(p.lengthMm),
                                   explain, formula, f3(q.unitWeight), f3(q.totalWeight), f3(q.unitArea),
                                   f3(q.totalArea), f3(q.primer), f3(q.mid), f3(q.top), f3(q.fireArea) });
            }
        }
        reports.push_back(r);
    }

    // id6 构件材料明细表
    {
        Report r; r.id = 6; r.group = QStringLiteral("明细表"); r.title = QStringLiteral("构件材料明细表");
        r.headers = { QStringLiteral("构件类型"), QStringLiteral("截面类型"), QStringLiteral("构件名称"), QStringLiteral("零件名称"),
                      QStringLiteral("厚度"), QStringLiteral("材质"), QStringLiteral("规格"), QStringLiteral("长度(mm)"),
                      QStringLiteral("数量"), QStringLiteral("总数量"), QStringLiteral("单重(kg)"), QStringLiteral("总重(kg)"),
                      QStringLiteral("单面积(m2)"), QStringLiteral("总面积(m2)"), QStringLiteral("总底漆(m2)"),
                      QStringLiteral("总中间漆(m2)"), QStringLiteral("总面漆(m2)"), QStringLiteral("总防火面积(m2)"),
                      QStringLiteral("钢板备注") };
        for (const auto& e : proj.elements) {
            for (const auto& p : e.parts) {
                SectionProps sp = sectionProps(p.spec);
                RowQty q = rowCalc(e, p);
                QString thick;
                if (sp.kind == QStringLiteral("PL")) {
                    QRegularExpression re(QStringLiteral(R"(^PL(\d+(?:\.\d+)?)$)"));
                    auto m = re.match(p.spec.toUpper().remove(QChar(' ')));
                    thick = m.hasMatch() ? m.captured(1) : QString();
                }
                r.rows.push_back({ e.typeName, sp.kind, e.name, p.name, thick, p.material, p.spec,
                                   f1(p.lengthMm), QString::number(p.count), QString::number(q.count),
                                   f3(q.unitWeight), f3(q.totalWeight), f3(q.unitArea), f3(q.totalArea),
                                   f3(q.primer), f3(q.mid), f3(q.top), f3(q.fireArea), p.remark });
            }
        }
        reports.push_back(r);
    }

    // 型材/螺栓/栓钉/锚栓/钢板 明细表通用构造
    auto makeSectionReport = [&](int id, const QString& title, const QString& craft) {
        Report r; r.id = id; r.group = QStringLiteral("明细表"); r.title = title;
        r.headers = { QStringLiteral("规格"), QStringLiteral("材质"), QStringLiteral("数量"), QStringLiteral("长度(mm)"),
                      QStringLiteral("重量(kg)"), QStringLiteral("面积(m2)"), QStringLiteral("小计") };
        auto aggs = aggregate(proj,
            [&](const Element& e, const Part& p) {
                return !partIsPlate(p) && !partIsBolt(e, p) && !partIsCotter(e, p) && !partIsAnchor(e, p)
                       && craftOf(p) == craft;
            },
            [](const Element&, const Part& p) { return QStringList{ p.spec, p.material }; });
        double tw = 0, ta = 0;
        for (const auto& g : aggs) {
            r.rows.push_back({ g.key.value(0), g.key.value(1), QString::number(g.count), f1(g.length),
                               f3(g.weight), f3(g.area), QString() });
            tw += g.weight; ta += g.area;
        }
        r.rows.push_back({ QStringLiteral("小计"), QString(), QString(), QString(), f3(tw), f3(ta), QString() });
        return r;
    };
    reports.push_back(makeSectionReport(7, QStringLiteral("热轧型材表"), QStringLiteral("hot")));
    reports.push_back(makeSectionReport(8, QStringLiteral("焊接型材表"), QStringLiteral("welded")));
    reports.push_back(makeSectionReport(9, QStringLiteral("异形截面型材表"), QStringLiteral("abnormal")));

    // id10 螺栓汇总表
    {
        Report r; r.id = 10; r.group = QStringLiteral("明细表"); r.title = QStringLiteral("螺栓汇总表");
        r.headers = { QStringLiteral("规格"), QStringLiteral("长度(mm)"), QStringLiteral("螺栓等级"),
                      QStringLiteral("数量(套)"), QStringLiteral("小计") };
        auto aggs = aggregate(proj,
            [](const Element& e, const Part& p) { return partIsBolt(e, p); },
            [](const Element&, const Part& p) { return QStringList{ p.spec, f1(p.lengthMm), p.material }; });
        int total = 0;
        for (const auto& g : aggs) {
            r.rows.push_back({ g.key.value(0), g.key.value(1), g.key.value(2), QString::number(g.count), QString() });
            total += g.count;
        }
        r.rows.push_back({ QStringLiteral("小计"), QString(), QString(), QString::number(total), QString() });
        reports.push_back(r);
    }

    // id11 栓钉汇总表
    {
        Report r; r.id = 11; r.group = QStringLiteral("明细表"); r.title = QStringLiteral("栓钉汇总表");
        r.headers = { QStringLiteral("规格"), QStringLiteral("长度(mm)"), QStringLiteral("材质"),
                      QStringLiteral("数量(套)"), QStringLiteral("小计") };
        auto aggs = aggregate(proj,
            [](const Element& e, const Part& p) { return partIsCotter(e, p); },
            [](const Element&, const Part& p) { return QStringList{ p.spec, f1(p.lengthMm), p.material }; });
        int total = 0;
        for (const auto& g : aggs) {
            r.rows.push_back({ g.key.value(0), g.key.value(1), g.key.value(2), QString::number(g.count), QString() });
            total += g.count;
        }
        r.rows.push_back({ QStringLiteral("小计"), QString(), QString(), QString::number(total), QString() });
        reports.push_back(r);
    }

    // id12 锚栓汇总表
    {
        Report r; r.id = 12; r.group = QStringLiteral("明细表"); r.title = QStringLiteral("锚栓汇总表");
        r.headers = { QStringLiteral("分类"), QStringLiteral("规格"), QStringLiteral("长度(mm)"), QStringLiteral("材质"),
                      QStringLiteral("数量"), QStringLiteral("重量(kg)"), QStringLiteral("面积(m2)"), QStringLiteral("小计") };
        auto aggs = aggregate(proj,
            [](const Element& e, const Part& p) { return partIsAnchor(e, p); },
            [](const Element&, const Part& p) {
                QString cls = !p.remark.trimmed().isEmpty() ? p.remark.trimmed()
                            : (!p.name.trimmed().isEmpty() ? p.name.trimmed() : QStringLiteral("锚栓"));
                return QStringList{ cls, p.spec, f1(p.lengthMm), p.material };
            });
        double tw = 0, ta = 0;
        for (const auto& g : aggs) {
            r.rows.push_back({ g.key.value(0), g.key.value(1), g.key.value(2), g.key.value(3),
                               QString::number(g.count), f3(g.weight), f3(g.area), QString() });
            tw += g.weight; ta += g.area;
        }
        r.rows.push_back({ QStringLiteral("小计"), QString(), QString(), QString(), QString(), f3(tw), f3(ta), QString() });
        reports.push_back(r);
    }

    // id13 钢板汇总表
    {
        Report r; r.id = 13; r.group = QStringLiteral("明细表"); r.title = QStringLiteral("钢板汇总表");
        r.headers = { QStringLiteral("规格"), QStringLiteral("材质"), QStringLiteral("数量"), QStringLiteral("长度(mm)"),
                      QStringLiteral("重量(kg)"), QStringLiteral("面积(m2)"), QStringLiteral("小计") };
        auto aggs = aggregate(proj,
            [](const Element& e, const Part& p) { Q_UNUSED(e); return partIsPlate(p); },
            [](const Element&, const Part& p) {
                const auto label = p.measurement.isEmpty() ? p.spec : p.name + " / " + p.spec;
                return QStringList{ label, p.material };
            });
        double tw = 0, ta = 0;
        for (const auto& g : aggs) {
            r.rows.push_back({ g.key.value(0), g.key.value(1), QString::number(g.count), f1(g.length),
                               f3(g.weight), f3(g.area), QString() });
            tw += g.weight; ta += g.area;
        }
        r.rows.push_back({ QStringLiteral("小计"), QString(), QString(), QString(), f3(tw), f3(ta), QString() });
        reports.push_back(r);
    }

    return reports;
}
