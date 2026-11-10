#pragma once
#include <QString>
#include <QStringList>
#include <QList>
#include <QPair>
#include <QMap>
#include "models.h"

// Verified entries are transcribed from the normal GJG2026 UI in a separate
// audit project. All remaining entries are editable engineering presets.
// Catalogue names and defaults do not imply verified geometry or algorithms.
namespace partcatalog {
using Group = QPair<QString, QStringList>;
#include "verifiedpartcatalog.h"
inline QList<Group> groups(const QString& type) {
    const auto found = verifiedCatalogue().constFind(type);
    if (found != verifiedCatalogue().cend()) return found->groups;
    const Group fasteners{QStringLiteral("其它"), {QStringLiteral("螺栓"), QStringLiteral("栓钉"),
        QStringLiteral("钢筋连接器"), QStringLiteral("地脚锚栓")}};
    if (type == QStringLiteral("钢柱") || type == QStringLiteral("格构柱")) {
        QList<Group> out = {
            {QStringLiteral("柱身"), {QStringLiteral("柱身型钢"), QStringLiteral("柱身钢板"),
                QStringLiteral("柱身加劲肋"), QStringLiteral("柱身隔板"), QStringLiteral("环形肋板"), QStringLiteral("纵向肋板")}},
            {QStringLiteral("牛腿"), {QStringLiteral("牛腿上翼缘板"), QStringLiteral("牛腿腹板"),
                QStringLiteral("牛腿下翼缘板"), QStringLiteral("牛腿加劲肋"), QStringLiteral("牛腿加强板")}},
            {QStringLiteral("柱顶"), {QStringLiteral("柱顶盖板"), QStringLiteral("柱顶加劲肋")}},
            {QStringLiteral("柱脚"), {QStringLiteral("柱脚底板"), QStringLiteral("柱脚加劲肋"),
                QStringLiteral("地脚锚栓盖板"), QStringLiteral("柱脚托座板"), QStringLiteral("抗剪键")}},
            {QStringLiteral("连接"), {QStringLiteral("支撑连接板"), QStringLiteral("系杆连接板"),
                QStringLiteral("檩条托板"), QStringLiteral("檩条加劲板"), QStringLiteral("隅撑连接板"), QStringLiteral("柱梁连接板")}},
            fasteners};
        if (type == QStringLiteral("格构柱"))
            out.insert(1, {QStringLiteral("格构补充"), {QStringLiteral("缀条"), QStringLiteral("缀板")}});
        return out;
    }
    if (type.contains(QStringLiteral("梁")))
        return {{QStringLiteral("梁身"), {QStringLiteral("梁身型钢"), QStringLiteral("梁身钢板"),
                    QStringLiteral("梁身加劲肋"), QStringLiteral("梁身腹板"), QStringLiteral("梁身上翼缘板"),
                    QStringLiteral("梁身下翼缘板"), QStringLiteral("梁身隔板")}},
                {QStringLiteral("连接"), {QStringLiteral("连接板"), QStringLiteral("端板"),
                    QStringLiteral("支撑连接板"), QStringLiteral("檩条托板"), QStringLiteral("檩条加劲板"),
                    QStringLiteral("隅撑连接板"), QStringLiteral("柱梁连接板")}}, fasteners};
    if (type == QStringLiteral("螺栓") || type == QStringLiteral("栓钉") || type == QStringLiteral("锚栓"))
        return {{QStringLiteral("紧固件"), {type, QStringLiteral("螺母"), QStringLiteral("垫圈"), QStringLiteral("锚板")}}};
    if (type == QStringLiteral("钢板") || type == QStringLiteral("型材屋面"))
        return {{QStringLiteral("面板"), {type, QStringLiteral("收边板"), QStringLiteral("连接板")}}, fasteners};
    if (type == QStringLiteral("钢楼梯") || type == QStringLiteral("钢爬梯") || type == QStringLiteral("钢平台"))
        return {{QStringLiteral("主体"), {QStringLiteral("主体型钢"), QStringLiteral("平台梁"),
                    QStringLiteral("梯梁"), QStringLiteral("踏步板"), QStringLiteral("平台板")}},
                {QStringLiteral("附属"), {QStringLiteral("栏杆"), QStringLiteral("扶手"),
                    QStringLiteral("加劲肋"), QStringLiteral("连接板"), QStringLiteral("底板")}}, fasteners};
    return {{QStringLiteral("主体"), {type + QStringLiteral("型钢"), type + QStringLiteral("钢板")}},
            {QStringLiteral("连接"), {QStringLiteral("连接板"), QStringLiteral("端板"),
                QStringLiteral("加劲肋"), QStringLiteral("节点板")}}, fasteners};
}
inline QStringList names(const QString& type) {
    QStringList out;
    for (const auto& group : groups(type)) out << group.second;
    out.removeDuplicates();
    return out;
}
inline bool verified(const QString& type) { return verifiedCatalogue().contains(type); }
inline QString fireLimit(const QString& componentType) {
    return verifiedCatalogue().value(componentType).fireLimit;
}
inline Part blank(const QString& type = {}, const QString& componentType = {}) {
    Part p;
    p.partType = type; p.name = type; p.material = QStringLiteral("Q355B");
    p.spec.clear(); p.lengthFormula.clear(); p.lengthMm = 0; p.count = 1;
    p.fireLimit = fireLimit(componentType);
    return p;
}
inline QString prefix(const QString& type) {
    const auto found = verifiedCatalogue().constFind(type);
    if (found != verifiedCatalogue().cend()) return found->prefix;
    static const QMap<QString, QString> map = {
        {QStringLiteral("钢柱"), QStringLiteral("GZ")}, {QStringLiteral("钢梁"), QStringLiteral("GL")},
        {QStringLiteral("吊车梁"), QStringLiteral("DCL")}, {QStringLiteral("系杆"), QStringLiteral("XG")},
        {QStringLiteral("檩条"), QStringLiteral("LT")}, {QStringLiteral("隅撑"), QStringLiteral("YC")},
        {QStringLiteral("拉条"), QStringLiteral("LTA")}, {QStringLiteral("天沟"), QStringLiteral("TG")},
        {QStringLiteral("格构柱"), QStringLiteral("GGZ")}, {QStringLiteral("钢平台"), QStringLiteral("GPT")},
        {QStringLiteral("钢楼梯"), QStringLiteral("GLT")}, {QStringLiteral("钢爬梯"), QStringLiteral("GPTI")},
        {QStringLiteral("栏杆"), QStringLiteral("LG")}, {QStringLiteral("门框"), QStringLiteral("MK")},
        {QStringLiteral("窗框"), QStringLiteral("CK")}};
    return map.value(type, type.isEmpty() ? QStringLiteral("GJ") : type);
}
}
