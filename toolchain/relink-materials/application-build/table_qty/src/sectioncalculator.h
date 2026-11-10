#pragma once
#include <QtWidgets>
#include "models.h"

class SectionCalculator : public QWidget {
    Q_OBJECT
public:
    explicit SectionCalculator(QWidget* parent=nullptr);
    void bind(const Part* part, int elementCount=1);
    QJsonObject selectedRecord() const;
signals:
    void commitRequested(QJsonObject record, double lengthMm, int count, QString material, bool clearManual);
private:
    void filter();
    void updatePreview();
    void submit();
    bool loading=false, bound=false;
    bool manualWeight=false, manualArea=false;
    double lockedWeight=0,lockedArea=0;
    int parentCount=1;
    QJsonObject current;
    QString boundId;
    QComboBox *mode,*family,*standard,*material,*shape;
    QLineEdit* search;
    QListWidget* list;
    QWidget* standardPage;
    QWidget* customPage;
    QDoubleSpinBox *length,*width,*h,*b,*tw,*tf,*radius;
    QSpinBox* count;
    QCheckBox *live,*clearManual;
    QLabel *preview,*target,*details;
    QPushButton* apply;
};
