#pragma once
#include <QString>
#include <QStringList>
#include <QVector>
#include <QJsonObject>
#include <QJsonArray>
#include <QMap>
#include <QUuid>

inline QString normalizedMaterial(QString s) {
    s = s.trimmed().toUpper();
    if (s.startsWith(QStringLiteral("Q345"))) s.replace(0, 4, QStringLiteral("Q355"));
    return s;
}
inline QStringList materialChoices() {
    return {"Q235B","Q355B","Q355C","Q355D","Q390B","Q420B","Q235","Q355"};
}

struct Part {
    QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString name = QStringLiteral("零件");
    QString material = QStringLiteral("Q235B");
    QString spec = QStringLiteral("H200x100x6x8");
    double lengthMm = 1000.0;
    int count = 1;
    QString fireLimit, fireGrade, rustGrade, remark;
    QString partType, code, lengthFormula, firePaint, quotaItem;
    QString primerPaint, midPaint, topPaint;
    double measuredWeightKg = -1, measuredAreaM2 = -1;
    QJsonObject measurement;
    QJsonObject calculation;

    QJsonObject toJson() const {
        QJsonObject o;
        o["id"] = id; o["calculation"] = calculation;
        o["measuredWeightKg"] = measuredWeightKg; o["measuredAreaM2"] = measuredAreaM2;
        o["measurement"] = measurement;
        o["name"] = name; o["material"] = normalizedMaterial(material); o["spec"] = spec;
        o["lengthMm"] = lengthMm; o["count"] = count;
        o["fireLimit"] = fireLimit; o["fireGrade"] = fireGrade;
        o["rustGrade"] = rustGrade; o["remark"] = remark;
        o["partType"] = partType; o["code"] = code; o["lengthFormula"] = lengthFormula;
        o["firePaint"] = firePaint; o["quotaItem"] = quotaItem;
        o["primerPaint"] = primerPaint; o["midPaint"] = midPaint; o["topPaint"] = topPaint;
        return o;
    }
    static Part fromJson(const QJsonObject& o) {
        Part p;
        p.id = o["id"].toString(p.id);
        if (p.id.isEmpty()) p.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        p.calculation = o["calculation"].toObject();
        p.measuredWeightKg = o["measuredWeightKg"].toDouble(-1);
        p.measuredAreaM2 = o["measuredAreaM2"].toDouble(-1);
        p.measurement = o["measurement"].toObject();
        p.name = o["name"].toString(p.name);
        p.material = normalizedMaterial(o["material"].toString(p.material));
        p.spec = o["spec"].toString(p.spec);
        p.lengthMm = o["lengthMm"].toDouble(p.lengthMm);
        p.count = o["count"].toInt(p.count);
        p.fireLimit = o["fireLimit"].toString();
        p.fireGrade = o["fireGrade"].toString();
        p.rustGrade = o["rustGrade"].toString();
        p.remark = o["remark"].toString();
        p.partType = o["partType"].toString(); p.code = o["code"].toString();
        p.lengthFormula = o["lengthFormula"].toString(); p.firePaint = o["firePaint"].toString();
        p.quotaItem = o["quotaItem"].toString(); p.primerPaint = o["primerPaint"].toString();
        p.midPaint = o["midPaint"].toString(); p.topPaint = o["topPaint"].toString();
        return p;
    }
};

struct Element {
    QString typeName = QStringLiteral("钢柱");
    QString name = QStringLiteral("构件");
    QString floor = QStringLiteral("首层");
    int count = 1;
    QVector<Part> parts;
    QString category;

    QJsonObject toJson() const {
        QJsonObject o;
        o["typeName"] = typeName; o["name"] = name; o["floor"] = floor; o["count"] = count;
        o["category"] = category;
        QJsonArray arr; for (const auto& p : parts) arr.append(p.toJson()); o["parts"] = arr;
        return o;
    }
    static Element fromJson(const QJsonObject& o) {
        Element e;
        e.typeName = o["typeName"].toString(e.typeName);
        e.name = o["name"].toString(e.name);
        e.floor = o["floor"].toString(e.floor);
        e.count = o["count"].toInt(e.count);
        e.category = o["category"].toString();
        for (const auto& v : o["parts"].toArray()) e.parts.push_back(Part::fromJson(v.toObject()));
        return e;
    }
};

struct Project {
    QString name = QStringLiteral("未命名工程");
    QString summaryDate;
    QStringList floors{ QStringLiteral("首层") };
    QVector<Element> elements;
    QMap<QString, QStringList> customTypes;

    QJsonObject toJson() const {
        QJsonObject o;
        o["name"] = name; o["summaryDate"] = summaryDate;
        QJsonArray f; for (const auto& s : floors) f.append(s); o["floors"] = f;
        QJsonArray arr; for (const auto& e : elements) arr.append(e.toJson()); o["elements"] = arr;
        QJsonObject types;
        for (auto it = customTypes.cbegin(); it != customTypes.cend(); ++it)
            types[it.key()] = QJsonArray::fromStringList(it.value());
        o["customTypes"] = types;
        return o;
    }
    static Project fromJson(const QJsonObject& o) {
        Project p;
        p.name = o["name"].toString(p.name);
        p.summaryDate = o["summaryDate"].toString();
        p.floors.clear();
        for (const auto& v : o["floors"].toArray()) p.floors << v.toString();
        if (p.floors.isEmpty()) p.floors << QStringLiteral("首层");
        for (const auto& v : o["elements"].toArray()) p.elements.push_back(Element::fromJson(v.toObject()));
        const auto types = o["customTypes"].toObject();
        for (auto it = types.begin(); it != types.end(); ++it)
            for (const auto& v : it.value().toArray()) p.customTypes[it.key()] << v.toString();
        return p;
    }
};
