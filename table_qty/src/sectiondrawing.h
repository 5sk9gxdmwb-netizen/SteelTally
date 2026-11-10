#pragma once
#include <QWidget>
#include <QJsonObject>
#include <QPainterPath>
#include <QStringList>

struct SectionLeader { QPointF anchor; QString text; };
struct SectionGeo {
    QPainterPath path; double w = 0, h = 0;
    QString wLabel = QStringLiteral("b"), hLabel = QStringLiteral("h");
    bool showW = true, showH = true;
    QStringList notes; QVector<SectionLeader> leaders;
};

// Drawing only: all numerical coefficients remain owned by sectionlib.
class SectionDrawing : public QWidget {
public:
    explicit SectionDrawing(QWidget* parent = nullptr);
    void setRecord(const QJsonObject& record, bool standardRecord);
    QJsonObject geometry() const { return dimensions; }
protected:
    void paintEvent(QPaintEvent*) override;
private:
    QJsonObject dimensions;
    QString caption, message;
    bool standard = false;
    SectionGeo geo;
};
