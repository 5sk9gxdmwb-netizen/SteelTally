#pragma once
#include <QFile>
#include <QJsonDocument>
#include <QJsonParseError>
#include <cstdio>
#include "calc.h"

// Isolated, read-only calculation endpoint for differential regression.
// Does not load, save or mutate a user's project or any reference-software file.
inline int runCalculationProbe(const QString& inputPath, const QString& outputPath) {
    QFile input(inputPath);
    if (!input.open(QIODevice::ReadOnly)) {
        std::fprintf(stderr, "calculation-probe: cannot read input\n"); return 2;
    }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(input.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject() ||
        !document.object().value(QStringLiteral("cases")).isArray()) {
        std::fprintf(stderr, "calculation-probe: invalid case document\n"); return 2;
    }
    QJsonArray results;
    for (const auto& value : document.object().value(QStringLiteral("cases")).toArray()) {
        const auto sample = value.toObject();
        if (!sample.value(QStringLiteral("id")).isString() ||
            !sample.value(QStringLiteral("part")).isObject()) {
            std::fprintf(stderr, "calculation-probe: invalid case\n"); return 2;
        }
        const auto part = Part::fromJson(sample.value(QStringLiteral("part")).toObject());
        const auto element = Element::fromJson(sample.value(QStringLiteral("element")).toObject());
        const auto section = sectionProps(part.spec);
        const auto qty = rowCalc(element, part);
        results.append(QJsonObject{
            {QStringLiteral("id"), sample.value(QStringLiteral("id"))},
            {QStringLiteral("sectionRecognized"), section.ok},
            {QStringLiteral("sectionKind"), section.kind},
            {QStringLiteral("massPerM"), section.massPerM},
            {QStringLiteral("surfacePerM"), section.surfacePerM},
            {QStringLiteral("count"), double(qty.count)},
            {QStringLiteral("lengthMm"), qty.length},
            {QStringLiteral("unitWeight"), qty.unitWeight},
            {QStringLiteral("totalWeight"), qty.totalWeight},
            {QStringLiteral("unitArea"), qty.unitArea},
            {QStringLiteral("totalArea"), qty.totalArea},
            {QStringLiteral("fireArea"), qty.fireArea},
            {QStringLiteral("primer"), qty.primer},
            {QStringLiteral("mid"), qty.mid},
            {QStringLiteral("top"), qty.top}});
    }
    QFile output(outputPath);
    if (!output.open(QIODevice::WriteOnly) ||
        output.write(QJsonDocument(QJsonObject{
            {QStringLiteral("version"), QCoreApplication::applicationVersion()},
            {QStringLiteral("schema"), QStringLiteral("steeltally.calculation-probe/1")},
            {QStringLiteral("cases"), results}}).toJson()) <= 0) {
        std::fprintf(stderr, "calculation-probe: cannot write output\n"); return 2;
    }
    std::printf("calculation-probe: %d cases\n", int(results.size()));
    return 0;
}
