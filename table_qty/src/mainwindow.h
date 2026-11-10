#pragma once
#include <QMainWindow>
#include <QIcon>
#include <QPair>
#include <QVector>
#include "models.h"
#include "reports.h"

class QTreeView;
class QStandardItemModel;
class QTableView;
class QTabWidget;
class QLabel;
class QComboBox;
class QAction;
class TableInputPanel;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

    void loadSample();
    int summaryRowCount() const;
    int reportCount() const { return reports.size(); }

    // Excel：固定输出 3 张合并工作表（做法汇总分析 / 构件汇总分析 / 明细表）
    bool exportExcelTo(const QString& path);
    int exportSheetCount() const { return 3; }

    // PDF：全部报表合并导出为单个文件
    bool exportPdfTo(const QString& path);

    // 皮肤：SilveryWhite / CoolDark
    bool setTheme(const QString& name);
    QString themeName() const { return currentTheme; }
    QString assetsDir() const { return assetsRoot; }
    int iconActionCount() const { return iconActions.size(); }
    int validIconCount() const;
    int themeBytes() const { return lastThemeBytes; }
    int currentReportCount() const { return reports.size(); }
    int inputCategoryCount() const;
    int inputPartTypeCount() const;
    bool runInputChecks(QString* error);

private:
    void buildUi();
    void buildToolBar(class QToolBar* tb);
    void refreshTree();
    void refreshInput();
    void refreshSummary();
    void showReport(int idx);
    void addElement();
    void addPart();
    void editSelected();
    void deleteSelected();
    void editPartSelected();
    void newProject();
    void saveProject();
    void loadProject();
    void loadSectionTable();
    void exportExcel();
    void exportPdf();
    void printCurrent();
    void printPreview();
    void switchTheme(int idx);
    void reloadIcons();
    QIcon iconFor(const QString& rel) const;
    int currentElementIndex() const;
    int currentReportIndex() const;
    void ensureReports();
    void setStatus(const QString& text);

    Project project;
    int curEl = -1;
    int curReport = 0;

    QString assetsRoot;
    QString currentTheme = QStringLiteral("SilveryWhite");
    int lastThemeBytes = 0;
    QVector<QPair<QAction*, QString>> iconActions; // action -> 图标相对路径

    QTreeView* treeView = nullptr;
    QStandardItemModel* treeModel = nullptr;
    QTableView* inputTable = nullptr;
    QStandardItemModel* inputModel = nullptr;
    TableInputPanel* inputPanel = nullptr;
    QTableView* sumTable = nullptr;
    QStandardItemModel* sumModel = nullptr;
    QTabWidget* tabs = nullptr;
    QLabel* statusLabel = nullptr;
    QComboBox* reportCombo = nullptr;
    QComboBox* themeCombo = nullptr;
    QTableView* reportTable = nullptr;
    QStandardItemModel* reportModel = nullptr;
    QVector<Report> reports;
};
