#pragma once
#include <QWidget>
#include <QMap>
#include <QSet>
#include <QVector>
#include <QToolButton>
#include <QPointer>
#include "models.h"

class QComboBox;
class QTreeWidget;
class QLabel;
class QTableWidget;
class QDialog;
class SectionCalculator;

class TableInputPanel : public QWidget {
    Q_OBJECT
public:
    enum Column { ElementType, ElementName, PartType, PartName, Code, Material, Spec,
                  Formula, Length, Count, UnitWeight, TotalWeight, UnitArea, TotalArea,
                  FireLimit, FirePaint, Remark, Rust, Primer, Mid, Top, Quota, ColumnCount };
    explicit TableInputPanel(Project* project, QWidget* parent = nullptr, bool persist = true);
    void reload();
    bool applyMatrix(const QString& text, int startRow, int startColumn, QString* error, bool commit = true);
    void pasteClipboard();
    void setTheme(const QString& assets, bool dark);
    int currentElementIndex() const { return selectedElement; }
    int currentPartIndex() const;
    int categoryCount() const;
    int partTypeCount() const { return partTypes().size(); }
    int validIconCount() const;
    int iconCount() const { return iconButtons.size(); }
    void setColumnVisible(const QString& name, bool on);
    bool columnVisible(const QString& name) const;
    QDialog* createColumnDialog();
    QDialog* createPartDialog();
    void prepareReferencePreview();
    void createElement();
    void addPart();
    void editCurrent();
    void deleteSelection();
    void showColumnSettings();
    bool runInteractionChecks(QString* error);
    bool runCalculationChecks(QString* error);

signals:
    void dataChanged();
    void statusMessage(const QString& message);

private:
    void buildUi();
    void rebuildCategories();
    void refreshElements();
    void refreshRows();
    void cellChanged(int row, int column);
    void addCategoryType();
    void copyElement();
    void deleteElement();
    void renameElement();
    void deleteParts();
    void movePart(int delta);
    void copyFloor();
    void selectElement(int index);
    void syncTreeSelection();
    void appendParts(const QStringList& names);
    int rowFor(int element, int part = -1) const;
    void saveColumns();
    void syncCalculator();
    void commitCalculation(QJsonObject record, double length, int count, QString material, bool clearManual);
    QString categoryFor(const Element& el) const;
    QStringList partTypes() const;
    QStringList partTypesFor(const QString& type) const;
    QToolButton* button(const QString& text, const QString& icon, const char* name);

    Project* project;
    bool persist;
    bool updating = false;
    struct RowRef { int element = -1; int part = -1; };
    QVector<RowRef> rowRefs;
    QMap<int, int> elementRows;
    QVector<int> visibleElements;
    QSet<int> collapsedElements;
    int selectedElement = -1;
    QString selectedCategory = QStringLiteral("主构件");
    QString selectedType = QStringLiteral("钢柱");
    QComboBox* floorCombo = nullptr;
    QTreeWidget* categories = nullptr;
    QTreeWidget* elements = nullptr;
    QLabel* footer = nullptr;
    QTableWidget* table = nullptr;
    SectionCalculator* calculator = nullptr;
    QPointer<QWidget> toolWindows[5];   // calctools::Kind 对应的独立工具窗口
    QJsonObject lastCalculationBefore, lastCalculationAfter;
    QString lastCalculationPart;
    QMap<QString, bool> visibility;
    QList<QPair<QToolButton*, QString>> iconButtons;
};
