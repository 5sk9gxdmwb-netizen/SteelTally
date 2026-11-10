#include "mainwindow.h"
#include "updater.h"
#include "calc.h"
#include "reporter.h"
#include "reports.h"
#include "sections.h"
#include "xlsx.h"
#include "tableinputpanel.h"
#include "workspaceextras.h"

#include <QAction>
#include <QMenuBar>
#include <QMenu>
#include <QSaveFile>
#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QDate>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QStandardItemModel>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableView>
#include <QTableWidget>
#include <QToolBar>
#include <QTreeView>
#include <QVBoxLayout>
#include <cstdio>
#include "helpdialog.h"

// Built-in assets always come from this executable; enterprise overrides stay external.
static QString resolveAssetsDir() {
    return QStringLiteral(":/assets");
}

// 零件编辑对话框
static bool editPartDialog(QWidget* parent, Part& part, bool isNew) {
    QDialog dlg(parent);
    dlg.setWindowTitle(isNew ? QStringLiteral("添加零件") : QStringLiteral("编辑零件"));
    auto* name = new QLineEdit(part.name);
    auto* material = new QComboBox;
    material->setEditable(true);
    material->addItems(materialChoices());
    material->setCurrentText(part.material);
    auto* spec = new QLineEdit(part.spec);
    auto* length = new QDoubleSpinBox;
    length->setRange(0.0, 1e9);
    length->setDecimals(2);
    length->setValue(part.lengthMm);
    auto* count = new QSpinBox;
    count->setRange(1, 100000000);
    count->setValue(part.count);
    auto* fireLimit = new QLineEdit(part.fireLimit);
    auto* fireGrade = new QLineEdit(part.fireGrade);
    auto* rust = new QLineEdit(part.rustGrade);
    auto* remark = new QLineEdit(part.remark);
    auto* preview = new QLabel;

    auto updatePreview = [=]() {
        SectionProps sp = sectionProps(spec->text());
        if (!sp.ok) {
            preview->setText(QStringLiteral("规格无法识别（H/I/C/L/PIPE/SHS/BOX/ROD/CZ/ZZ/TH/FB/PL）"));
        } else {
            double L = length->value() / 1000.0;
            preview->setText(QStringLiteral("单重 %1 kg ｜ 单面积 %2 m2")
                                 .arg(sp.massPerM * L, 0, 'f', 3)
                                 .arg(sp.surfacePerM * L, 0, 'f', 3));
        }
    };
    QObject::connect(spec, &QLineEdit::textChanged, preview, [=](const QString&) { updatePreview(); });
    QObject::connect(length, &QDoubleSpinBox::textChanged, preview, [=](const QString&) { updatePreview(); });

    auto* form = new QFormLayout;
    form->addRow(QStringLiteral("零件名称"), name);
    form->addRow(QStringLiteral("材质"), material);
    form->addRow(QStringLiteral("规格"), spec);
    form->addRow(QStringLiteral("长度(mm)"), length);
    form->addRow(QStringLiteral("数量"), count);
    form->addRow(QStringLiteral("防火时限"), fireLimit);
    form->addRow(QStringLiteral("防火等级"), fireGrade);
    form->addRow(QStringLiteral("除锈等级"), rust);
    form->addRow(QStringLiteral("备注"), remark);
    form->addRow(QStringLiteral("预览"), preview);

    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    QObject::connect(box, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    auto* lay = new QVBoxLayout(&dlg);
    auto* holder = new QWidget;
    holder->setLayout(form);
    lay->addWidget(holder);
    lay->addWidget(box);
    updatePreview();

    if (dlg.exec() != QDialog::Accepted) return false;
    part.name = name->text().trimmed().isEmpty() ? QStringLiteral("零件") : name->text().trimmed();
    part.material = normalizedMaterial(material->currentText());
    if(sectionlib::key(part.spec)!=sectionlib::key(spec->text()))part.calculation={};
    part.spec = spec->text().trimmed();
    part.lengthMm = length->value();
    part.count = count->value();
    part.fireLimit = fireLimit->text().trimmed();
    part.fireGrade = fireGrade->text().trimmed();
    part.rustGrade = rust->text().trimmed();
    part.remark = remark->text().trimmed();
    return true;
}

// 构件编辑对话框
static bool editElementDialog(QWidget* parent, const Project& proj, Element& el, bool isNew) {
    QDialog dlg(parent);
    dlg.setWindowTitle(isNew ? QStringLiteral("新建构件") : QStringLiteral("编辑构件"));
    auto* type = new QComboBox;
    type->setEditable(true);
    type->addItems({ QStringLiteral("钢柱"), QStringLiteral("钢梁"), QStringLiteral("钢支撑"), QStringLiteral("钢桁架"),
                     QStringLiteral("钢平台"), QStringLiteral("钢楼梯"), QStringLiteral("栏杆"), QStringLiteral("门式刚架"),
                     QStringLiteral("型材屋面"), QStringLiteral("钢板"), QStringLiteral("螺栓"), QStringLiteral("栓钉"),
                     QStringLiteral("锚栓") });
    type->setCurrentText(el.typeName);
    auto* name = new QLineEdit(el.name);
    auto* floor = new QComboBox;
    floor->setEditable(true);
    floor->addItems(proj.floors);
    floor->setCurrentText(el.floor);
    auto* count = new QSpinBox;
    count->setRange(1, 100000000);
    count->setValue(el.count);

    auto* form = new QFormLayout;
    form->addRow(QStringLiteral("构件类型"), type);
    form->addRow(QStringLiteral("构件名称"), name);
    form->addRow(QStringLiteral("楼层"), floor);
    form->addRow(QStringLiteral("数量"), count);

    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    QObject::connect(box, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    auto* lay = new QVBoxLayout(&dlg);
    auto* holder = new QWidget;
    holder->setLayout(form);
    lay->addWidget(holder);
    lay->addWidget(box);

    if (dlg.exec() != QDialog::Accepted) return false;
    el.typeName = type->currentText().trimmed().isEmpty() ? QStringLiteral("钢柱") : type->currentText().trimmed();
    el.name = name->text().trimmed().isEmpty() ? QStringLiteral("构件") : name->text().trimmed();
    el.floor = floor->currentText().trimmed().isEmpty() ? QStringLiteral("首层") : floor->currentText().trimmed();
    el.count = count->value();
    return true;
}

// ---------------------------------------------------------------------------
// 主窗口：皮肤 / 图标 / 工具条
// ---------------------------------------------------------------------------

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    assetsRoot = resolveAssetsDir();
    extras::loadLibraries(assetsRoot);
    buildUi();
    setTheme(QStringLiteral("SilveryWhite"));
}

QIcon MainWindow::iconFor(const QString& rel) const {
    const QString dir = (currentTheme == QStringLiteral("CoolDark")) ? QStringLiteral("dark")
                                                                     : QStringLiteral("light");
    QString p = QDir(assetsRoot).filePath(QStringLiteral("icons/%1/%2").arg(dir, rel));
    if (!QFile::exists(p)) p = QDir(assetsRoot).filePath(QStringLiteral("icons/light/%1").arg(rel));
    return QFile::exists(p) ? QIcon(p) : QIcon();
}

void MainWindow::reloadIcons() {
    for (auto& pr : iconActions) pr.first->setIcon(iconFor(pr.second));
    if (tabs) {
        tabs->setTabIcon(0, iconFor(QStringLiteral("product/Ribbon/GJG_TableInPut(16).svg")));
        tabs->setTabIcon(1, iconFor(QStringLiteral("product/Ribbon/GJG_CalcSetting(16).svg")));
        tabs->setTabIcon(2, iconFor(QStringLiteral("product/Ribbon/GJG_ReportView(16).svg")));
    }
    setWindowIcon(QIcon(QStringLiteral(":/assets/icons/light/product/App/logo.ico")));
}

bool MainWindow::setTheme(const QString& name) {
    const bool dark = name.compare(QStringLiteral("CoolDark"), Qt::CaseInsensitive) == 0;
    const QString file = dark ? QStringLiteral("cooldark.qss") : QStringLiteral("silverywhite.qss");
    QFile f(QDir(assetsRoot).filePath(QStringLiteral("themes/") + file));
    if (!f.open(QIODevice::ReadOnly)) {
        setStatus(QStringLiteral("皮肤文件缺失：%1").arg(file));
        return false;
    }
    QString qss = QString::fromUtf8(f.readAll());
    f.close();
    lastThemeBytes = qss.size();

    QString a = assetsRoot;
    a.replace(QLatin1Char('\\'), QLatin1Char('/'));
    qss.replace(QStringLiteral("__ASSETS__"), a);

    currentTheme = dark ? QStringLiteral("CoolDark") : QStringLiteral("SilveryWhite");
    qss += dark ? QStringLiteral("\nQWidget#sectionCalculator, QWidget#sectionParameterPane { background: #363738; }")
                : QStringLiteral("\nQWidget#sectionCalculator, QWidget#sectionParameterPane { background: #f4f4f4; }");
    qApp->setStyleSheet(qss);
    reloadIcons();
    if (inputPanel) inputPanel->setTheme(assetsRoot, dark);
    if (themeCombo) {
        QSignalBlocker blocker(themeCombo);
        themeCombo->setCurrentIndex(dark ? 1 : 0);
    }
    return true;
}

void MainWindow::switchTheme(int idx) {
    setTheme(idx == 1 ? QStringLiteral("CoolDark") : QStringLiteral("SilveryWhite"));
    setStatus(QStringLiteral("已切换皮肤：%1")
                  .arg(idx == 1 ? QStringLiteral("CoolDark 深色") : QStringLiteral("SilveryWhite 银白")));
}

int MainWindow::validIconCount() const {
    int n = 0;
    for (const auto& pr : iconActions)
        if (!pr.first->icon().isNull()) ++n;
    return n;
}

void MainWindow::buildUi() {
    setWindowTitle(QStringLiteral("SteelTally v%1 · 表格算量").arg(QCoreApplication::applicationVersion()));
    resize(1620, 860);

    auto* tb = addToolBar(QStringLiteral("main"));
    buildToolBar(tb);

    auto* split = new QSplitter(Qt::Horizontal, this);
    setCentralWidget(split);

    treeView = new QTreeView(split);
    treeModel = new QStandardItemModel(this);
    treeModel->setHorizontalHeaderLabels({ QStringLiteral("构件类型 / 构件") });
    treeView->setModel(treeModel);
    treeView->setUniformRowHeights(true);
    treeView->setAlternatingRowColors(true);
    split->addWidget(treeView);

    tabs = new QTabWidget(split);

    auto* inputTab = new QWidget;
    auto* inputLay = new QVBoxLayout(inputTab);
    inputLay->setContentsMargins(0, 0, 0, 0);
    inputPanel = new TableInputPanel(&project, inputTab);
    inputPanel->setTheme(assetsRoot, false);
    inputLay->addWidget(inputPanel);
    tabs->addTab(inputTab, QStringLiteral("表格算量输入"));

    auto* sumTab = new QWidget;
    auto* sumLay = new QVBoxLayout(sumTab);
    sumLay->setContentsMargins(0, 0, 0, 0);
    sumTable = new QTableView(sumTab);
    sumModel = new QStandardItemModel(this);
    sumTable->setModel(sumModel);
    sumTable->setAlternatingRowColors(true);
    sumTable->horizontalHeader()->setStretchLastSection(true);
    sumTable->verticalHeader()->setDefaultSectionSize(22);
    sumLay->addWidget(sumTable);
    tabs->addTab(sumTab, QStringLiteral("表格算量构件汇总表"));

    auto* repTab = new QWidget;
    auto* repLay = new QVBoxLayout(repTab);
    repLay->setContentsMargins(6, 6, 6, 0);
    auto* repTop = new QHBoxLayout;
    repTop->addWidget(new QLabel(QStringLiteral("报表：")));
    reportCombo = new QComboBox(repTab);
    repTop->addWidget(reportCombo, 1);
    repLay->addLayout(repTop);
    reportTable = new QTableView(repTab);
    reportModel = new QStandardItemModel(this);
    reportTable->setModel(reportModel);
    reportTable->setAlternatingRowColors(true);
    reportTable->horizontalHeader()->setStretchLastSection(true);
    reportTable->verticalHeader()->setDefaultSectionSize(22);
    repLay->addWidget(reportTable);
    tabs->addTab(repTab, QStringLiteral("全部报表"));
    connect(reportCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int i) {
        showReport(i);
    });

    split->addWidget(tabs);
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 4);
    split->setSizes({ 260, 1080 });

    statusLabel = new QLabel(this);
    statusBar()->addWidget(statusLabel);

    connect(treeView->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this]() {
        curEl = currentElementIndex();
        refreshInput();
    });
    connect(inputPanel, &TableInputPanel::dataChanged, this, [this]() {
        curEl = inputPanel->currentElementIndex();
        refreshSummary();
    });
    connect(inputPanel, &TableInputPanel::statusMessage, this, &MainWindow::setStatus);
    treeView->setVisible(false);
    split->setSizes({0, 1360});

    auto* dataMenu = menuBar()->addMenu(QStringLiteral("数据维护"));
    dataMenu->addAction(QStringLiteral("型钢断面库（查阅/企业覆盖）"), this, [this]() {
        extras::editLibrary(this, assetsRoot, false); refreshInput(); refreshSummary();
    });
    dataMenu->addAction(QStringLiteral("定额库（查阅/企业覆盖）"), this, [this]() {
        extras::editLibrary(this, assetsRoot, true);
    });
    auto* helpMenu = menuBar()->addMenu(QStringLiteral("帮助"));
    const QStringList helpItems{QStringLiteral("使用说明"), QStringLiteral("计算依据"),
                                QStringLiteral("更新日志"), QStringLiteral("关于")};
    for (int i = 0; i < helpItems.size(); ++i)
        helpMenu->addAction(helpItems[i], this, [this, i]() { showTableHelp(this, i); });
    reloadIcons();
}

void MainWindow::buildToolBar(QToolBar* tb) {
    tb->setMovable(false);
    tb->setFloatable(false);
    tb->setIconSize(QSize(18, 18));
    tb->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

    auto addBtn = [&](const QString& text, const QString& iconRel, const QString& tip,
                      void (MainWindow::*fn)()) {
        auto* act = tb->addAction(iconFor(iconRel), text);
        act->setToolTip(tip);
        connect(act, &QAction::triggered, this, fn);
        iconActions.append(qMakePair(act, iconRel));
        return act;
    };

    addBtn(QStringLiteral("新建工程"), QStringLiteral("platform/NewFile(16).svg"),
           QStringLiteral("清空当前工程，新建空白工程"), &MainWindow::newProject);
    addBtn(QStringLiteral("打开"), QStringLiteral("platform/OpenFile(16).svg"),
           QStringLiteral("打开工程 JSON"), &MainWindow::loadProject);
    addBtn(QStringLiteral("保存"), QStringLiteral("platform/SaveFile(16).svg"),
           QStringLiteral("保存工程 JSON"), &MainWindow::saveProject);
    tb->addSeparator();
    addBtn(QStringLiteral("新建构件"), QStringLiteral("platform/Add.svg"),
           QStringLiteral("新增构件节点"), &MainWindow::addElement);
    addBtn(QStringLiteral("添加零件"), QStringLiteral("product/TableInputToolbar/addParts.svg"),
           QStringLiteral("在当前构件下添加零件行"), &MainWindow::addPart);
    addBtn(QStringLiteral("编辑"), QStringLiteral("platform/EditElement.svg"),
           QStringLiteral("编辑选中构件 / 零件"), &MainWindow::editSelected);
    addBtn(QStringLiteral("删除"), QStringLiteral("product/TableInputToolbar/deleteRow.svg"),
           QStringLiteral("删除选中构件 / 零件行"), &MainWindow::deleteSelected);
    tb->addSeparator();
    addBtn(QStringLiteral("刷新汇总"), QStringLiteral("icon/Refresh.svg"),
           QStringLiteral("重新计算并刷新汇总与报表"), &MainWindow::refreshSummary);
    addBtn(QStringLiteral("截面表"), QStringLiteral("platform/FileManager(16).svg"),
           QStringLiteral("加载标准截面 CSV（规格,kg/m,m2/m）"), &MainWindow::loadSectionTable);
    addBtn(QStringLiteral("示例工程"), QStringLiteral("product/GJG/GJG_Normal.svg"),
           QStringLiteral("载入内置示例钢结构工程"), &MainWindow::loadSample);
    tb->addSeparator();
    addBtn(QStringLiteral("打印预览"), QStringLiteral("product/Report/preview.svg"),
           QStringLiteral("预览当前报表"), &MainWindow::printPreview);
    addBtn(QStringLiteral("打印"), QStringLiteral("product/Report/print.svg"),
           QStringLiteral("打印当前报表"), &MainWindow::printCurrent);
    addBtn(QStringLiteral("导出PDF"), QStringLiteral("product/Report/print.svg"),
           QStringLiteral("把全部报表导出为一个 PDF"), &MainWindow::exportPdf);
    addBtn(QStringLiteral("导出Excel"), QStringLiteral("product/Report/multiExport.svg"),
           QStringLiteral("导出 3 张合并工作表"), &MainWindow::exportExcel);

    tb->addSeparator();
    tb->addWidget(new QLabel(QStringLiteral("皮肤 "), tb));
    themeCombo = new QComboBox(tb);
    themeCombo->addItem(QStringLiteral("银白 SilveryWhite"));
    themeCombo->addItem(QStringLiteral("深色 CoolDark"));
    themeCombo->setMinimumWidth(150);
    themeCombo->setToolTip(QStringLiteral("切换界面皮肤（配色 + 图标同步）"));
    tb->addWidget(themeCombo);
    connect(themeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::switchTheme);
    addToolBarBreak();
    auto* updateBar = addToolBar(QStringLiteral("版本与更新"));
    updateBar->setMovable(false);
    updateBar->setFloatable(false);
    auto* spacer = new QWidget(updateBar);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    updateBar->addWidget(spacer);
    // Placeholder until the updater attaches; it immediately replaces this with the real state.
    auto* updateStatus = new QLabel(QStringLiteral("v%1 · %2").arg(QCoreApplication::applicationVersion())
        .arg(updater::configured() ? QStringLiteral("等待检查更新") : QStringLiteral("未配置更新源")), updateBar);
    updateStatus->setObjectName(QStringLiteral("updateStatus"));
    updateBar->addWidget(updateStatus);
    auto* check = updateBar->addAction(QStringLiteral("检查更新"));
    check->setObjectName(QStringLiteral("checkUpdatesAction"));
    updater::attach(this, updateStatus, check);
}

// ---------------------------------------------------------------------------
// 数据刷新与构件 / 零件编辑
// ---------------------------------------------------------------------------

int MainWindow::currentElementIndex() const {
    if (inputPanel) return inputPanel->currentElementIndex();
    auto idxs = treeView->selectionModel()->selectedIndexes();
    if (idxs.isEmpty()) return -1;
    QVariant v = idxs.first().data(Qt::UserRole);
    return v.isValid() ? v.toInt() : -1;
}

int MainWindow::currentReportIndex() const {
    int i = reportCombo ? reportCombo->currentIndex() : 0;
    if (i < 0 || i >= reports.size()) i = 0;
    return i;
}

int MainWindow::summaryRowCount() const {
    return sumModel ? sumModel->rowCount() : 0;
}

int MainWindow::inputCategoryCount() const {
    return inputPanel ? inputPanel->categoryCount() : 0;
}

int MainWindow::inputPartTypeCount() const {
    return inputPanel ? inputPanel->partTypeCount() : 0;
}

bool MainWindow::runInputChecks(QString* error) {
    const auto originalJson = project.toJson();
    Project testProject = project;
    TableInputPanel testPanel(&testProject, nullptr, false);
    if (!testPanel.runInteractionChecks(error)) return false;
    auto* table = inputPanel->findChild<QTableWidget*>(QStringLiteral("partsTable"));
    const int index = inputPanel->currentElementIndex();
    if (!table || index < 0) { *error = QStringLiteral("main input selection"); return false; }
    const int previousCount = project.elements[index].count;
    double beforeWeight = 0, beforeArea = 0;
    totals(project, beforeWeight, beforeArea);
    table->item(0, TableInputPanel::Count)->setText(QString::number(previousCount + 1));
    double afterWeight = 0, afterArea = 0;
    totals(project, afterWeight, afterArea);
    bool summaryMatches = true;
    const auto expectedRows = summaryRows(project);
    for (int row = 0; row < expectedRows.size(); ++row)
        for (int col = 0; col < expectedRows[row].size(); ++col)
            summaryMatches = summaryMatches && sumModel->index(row, col).data().toString() == expectedRows[row][col];
    const bool changed = afterWeight > beforeWeight && tabs->currentIndex() == 0 && summaryMatches &&
                         project.elements[index].count == previousCount + 1 &&
                         sumModel->rowCount() == summaryRows(project).size() && reports.size() == 14;
    table->item(0, TableInputPanel::Count)->setText(QString::number(previousCount));
    if (!changed || project.toJson() != originalJson) { *error = QStringLiteral("main live summary or tab focus or restore"); return false; }
    std::printf("input: main live-summary, reports and tab-focus passed\n");
    return true;
}

void MainWindow::setStatus(const QString& text) {
    if (statusLabel) statusLabel->setText(text);
}

void MainWindow::ensureReports() {
    if (reports.isEmpty()) reports = buildAllReports(project);
}

void MainWindow::refreshTree() {
    if (inputPanel) {
        inputPanel->reload();
        return;
    }
    treeModel->clear();
    treeModel->setHorizontalHeaderLabels({ QStringLiteral("构件类型 / 构件") });
    QMap<QString, QStandardItem*> typeItems;
    QMap<QString, int> counts;
    for (const auto& e : project.elements) counts[e.typeName]++;
    for (int i = 0; i < project.elements.size(); ++i) {
        const Element& e = project.elements[i];
        QStandardItem* parentItem = typeItems.value(e.typeName);
        if (!parentItem) {
            parentItem = new QStandardItem(iconFor(QStringLiteral("product/GJG/GJG_Normal.svg")),
                                           QStringLiteral("%1（%2）").arg(e.typeName).arg(counts.value(e.typeName)));
            parentItem->setEditable(false);
            typeItems.insert(e.typeName, parentItem);
            treeModel->appendRow(parentItem);
        }
        auto* child = new QStandardItem(QStringLiteral("%1  [%2] ×%3").arg(e.name, e.floor).arg(e.count));
        child->setEditable(false);
        child->setData(i, Qt::UserRole);
        parentItem->appendRow(child);
    }
    treeView->expandAll();
}

void MainWindow::refreshInput() {
    if (inputPanel) {
        inputPanel->reload();
        curEl = inputPanel->currentElementIndex();
        return;
    }
    inputModel->clear();
    inputModel->setHorizontalHeaderLabels({ QStringLiteral("零件名称"), QStringLiteral("材质"), QStringLiteral("规格"),
        QStringLiteral("长度(mm)"), QStringLiteral("数量"), QStringLiteral("单重(kg)"), QStringLiteral("总重(kg)"),
        QStringLiteral("单面积(m2)"), QStringLiteral("总面积(m2)"), QStringLiteral("防火面积(m2)"),
        QStringLiteral("底漆(m2)"), QStringLiteral("中间漆(m2)"), QStringLiteral("面漆(m2)"), QStringLiteral("备注") });
    if (curEl < 0 || curEl >= project.elements.size()) {
        setStatus(QStringLiteral("请选择左侧构件"));
        return;
    }
    const Element& e = project.elements[curEl];
    for (const auto& p : e.parts) {
        RowQty q = rowCalc(e, p);
        QList<QStandardItem*> row;
        auto add = [&](const QString& t) { auto* it = new QStandardItem(t); it->setEditable(false); row << it; };
        add(p.name); add(p.material); add(p.spec);
        add(QString::number(p.lengthMm, 'f', 1)); add(QString::number(p.count));
        add(QString::number(q.unitWeight, 'f', 3)); add(QString::number(q.totalWeight, 'f', 3));
        add(QString::number(q.unitArea, 'f', 3)); add(QString::number(q.totalArea, 'f', 3));
        add(QString::number(q.fireArea, 'f', 3)); add(QString::number(q.primer, 'f', 3));
        add(QString::number(q.mid, 'f', 3)); add(QString::number(q.top, 'f', 3)); add(p.remark);
        for (auto* it : row) it->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        row.first()->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        inputModel->appendRow(row);
    }
    setStatus(QStringLiteral("当前构件：%1 / %2 ｜ 楼层 %3 ｜ 数量 %4 ｜ 零件 %5")
                  .arg(e.typeName, e.name, e.floor).arg(e.count).arg(e.parts.size()));
}

void MainWindow::refreshSummary() {
    sumModel->clear();
    sumModel->setHorizontalHeaderLabels(summaryColumns());
    for (const auto& r : summaryRows(project)) {
        QList<QStandardItem*> row;
        for (const auto& t : r) { auto* it = new QStandardItem(t); it->setEditable(false); row << it; }
        sumModel->appendRow(row);
    }
    double tw = 0, ta = 0;
    totals(project, tw, ta);
    setStatus(QStringLiteral("工程：%1 ｜ 构件 %2 个 ｜ 总重 %3 kg ｜ 总面积 %4 m2")
                  .arg(project.name).arg(project.elements.size()).arg(tw, 0, 'f', 3).arg(ta, 0, 'f', 3));

    reports = buildAllReports(project);
    if (reportCombo) {
        reportCombo->blockSignals(true);
        reportCombo->clear();
        for (const auto& r : reports)
            reportCombo->addItem(QStringLiteral("%1 [%2]").arg(r.title, r.group));
        reportCombo->blockSignals(false);
        if (reportCombo->count() > 0) {
            reportCombo->setCurrentIndex(0);
            showReport(0);
        }
    }
}

void MainWindow::showReport(int idx) {
    if (!reportModel || idx < 0 || idx >= reports.size()) return;
    curReport = idx;
    const Report& r = reports[idx];
    reportModel->clear();
    reportModel->setHorizontalHeaderLabels(r.headers);
    for (const auto& row : r.rows) {
        QList<QStandardItem*> items;
        for (const auto& t : row) {
            auto* it = new QStandardItem(t);
            it->setEditable(false);
            items << it;
        }
        reportModel->appendRow(items);
    }
}

void MainWindow::newProject() {
    project = Project();
    project.name = QStringLiteral("新建钢结构工程");
    project.summaryDate = QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"));
    project.floors = { QStringLiteral("首层") };
    curEl = -1;
    refreshTree();
    refreshInput();
    refreshSummary();
    setStatus(QStringLiteral("已新建空白工程"));
}

void MainWindow::addElement() {
    if (inputPanel) { inputPanel->createElement(); return; }
    Element e;
    if (!editElementDialog(this, project, e, true)) return;
    project.elements.push_back(e);
    if (!project.floors.contains(e.floor)) project.floors << e.floor;
    refreshTree();
    refreshSummary();
}

void MainWindow::addPart() {
    if (inputPanel) { inputPanel->addPart(); return; }
    if (curEl < 0 || curEl >= project.elements.size()) {
        QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("请先在左侧选择一个构件"));
        return;
    }
    Part p;
    if (!editPartDialog(this, p, true)) return;
    project.elements[curEl].parts.push_back(p);
    refreshInput();
    refreshSummary();
}

void MainWindow::editPartSelected() {
    if (inputPanel) { inputPanel->editCurrent(); return; }
    if (curEl < 0 || curEl >= project.elements.size()) return;
    QModelIndex idx = inputTable->currentIndex();
    if (!idx.isValid()) return;
    int row = idx.row();
    if (row < 0 || row >= project.elements[curEl].parts.size()) return;
    Part p = project.elements[curEl].parts[row];
    if (!editPartDialog(this, p, false)) return;
    project.elements[curEl].parts[row] = p;
    refreshInput();
    refreshSummary();
}

void MainWindow::editSelected() {
    if (inputPanel) { inputPanel->editCurrent(); return; }
    if (inputTable->selectionModel() && inputTable->selectionModel()->hasSelection()) {
        editPartSelected();
        return;
    }
    int i = currentElementIndex();
    if (i < 0 || i >= project.elements.size()) return;
    Element e = project.elements[i];
    if (!editElementDialog(this, project, e, false)) return;
    project.elements[i] = e;
    if (!project.floors.contains(e.floor)) project.floors << e.floor;
    refreshTree();
    refreshInput();
    refreshSummary();
}

void MainWindow::deleteSelected() {
    if (inputPanel) { inputPanel->deleteSelection(); return; }
    if (inputTable->selectionModel() && inputTable->selectionModel()->hasSelection()) {
        int row = inputTable->currentIndex().row();
        if (curEl >= 0 && curEl < project.elements.size() && row >= 0 && row < project.elements[curEl].parts.size()) {
            if (QMessageBox::question(this, QStringLiteral("确认"), QStringLiteral("删除选中的零件行？")) == QMessageBox::Yes) {
                project.elements[curEl].parts.removeAt(row);
                refreshInput();
                refreshSummary();
            }
        }
        return;
    }
    int i = currentElementIndex();
    if (i < 0 || i >= project.elements.size()) return;
    if (QMessageBox::question(this, QStringLiteral("确认"),
            QStringLiteral("删除构件 %1？").arg(project.elements[i].name)) == QMessageBox::Yes) {
        project.elements.removeAt(i);
        curEl = -1;
        refreshTree();
        refreshInput();
        refreshSummary();
    }
}

void MainWindow::saveProject() {
    QString path = QFileDialog::getSaveFileName(this, QStringLiteral("保存工程"), QStringLiteral("project.json"),
                                                QStringLiteral("工程 (*.json)"));
    if (path.isEmpty()) return;
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, QStringLiteral("错误"), QStringLiteral("无法写入文件"));
        return;
    }
    f.write(QJsonDocument(project.toJson()).toJson(QJsonDocument::Indented));
    if (!f.commit()) { QMessageBox::warning(this, QStringLiteral("保存失败"), f.errorString()); return; }
    setStatus(QStringLiteral("已保存：%1").arg(path));
}

void MainWindow::loadProject() {
    QString path = QFileDialog::getOpenFileName(this, QStringLiteral("打开工程"), QString(),
                                                QStringLiteral("工程 (*.json)"));
    if (path.isEmpty()) return;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, QStringLiteral("错误"), QStringLiteral("无法读取文件"));
        return;
    }
    QJsonParseError err{};
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    f.close();
    if (err.error != QJsonParseError::NoError) {
        QMessageBox::warning(this, QStringLiteral("错误"), QStringLiteral("JSON 解析失败：%1").arg(err.errorString()));
        return;
    }
    if (!doc.isObject() || !doc.object().value("elements").isArray()) {
        QMessageBox::warning(this, QStringLiteral("打开失败"), QStringLiteral("这不是最新版表格算量工程；原人工算量格式不在此处打开。")); return;
    }
    project = Project::fromJson(doc.object());
    curEl = -1;
    refreshTree();
    refreshInput();
    refreshSummary();
}

// ---------------------------------------------------------------------------
// 导出与打印
// ---------------------------------------------------------------------------

// Excel：三张合并工作表（按报表分组：做法汇总分析 / 构件汇总分析 / 明细表）
bool MainWindow::exportExcelTo(const QString& path) {
    const auto issue=calculationIssue(project);
    if(!issue.isEmpty()){setStatus(QStringLiteral("请先补齐计算数据：")+issue);return false;}
    ensureReports();

    struct GroupDef {
        QString group;
        QString sheet;
    };
    const QVector<GroupDef> defs = {
        { QStringLiteral("做法汇总分析"), QStringLiteral("做法汇总分析") },
        { QStringLiteral("构件汇总分析"), QStringLiteral("构件汇总分析") },
        { QStringLiteral("明细表"), QStringLiteral("明细表") },
    };

    QHash<QString, int> sheetOf;
    for (int i = 0; i < defs.size(); ++i) sheetOf.insert(defs[i].group, i);

    QVector<XlsxSheet> sheets(defs.size());
    for (int i = 0; i < defs.size(); ++i) sheets[i].name = defs[i].sheet;
    QVector<bool> hasBlock(defs.size(), false);

    for (const auto& r : reports) {
        const int idx = sheetOf.value(r.group, defs.size() - 1);
        XlsxSheet& sh = sheets[idx];
        if (hasBlock[idx]) sh.rows.push_back(QStringList());
        hasBlock[idx] = true;
        sh.titleRows.push_back(sh.rows.size());
        sh.rows.push_back(QStringList{ r.title });
        sh.headerRows.push_back(sh.rows.size());
        sh.rows.push_back(r.headers);
        for (const auto& row : r.rows) sh.rows.push_back(row);
    }
    for (int i = 0; i < sheets.size(); ++i)
        if (sheets[i].rows.isEmpty()) sheets[i].rows.push_back(QStringList{ QStringLiteral("（无数据）") });

    QString err;
    const bool ok = writeXlsx(path, sheets, &err);
    if (!ok) setStatus(QStringLiteral("Excel 导出失败：%1").arg(err));
    return ok;
}

void MainWindow::exportExcel() {
    QString path = QFileDialog::getSaveFileName(this, QStringLiteral("导出 Excel"),
                                                QStringLiteral("GJG表格算量.xlsx"),
                                                QStringLiteral("Excel (*.xlsx)"));
    if (path.isEmpty()) return;
    if (!exportExcelTo(path)) {
        QMessageBox::warning(this, QStringLiteral("导出失败"), QStringLiteral("写入 xlsx 失败"));
        return;
    }
    ensureReports();
    setStatus(QStringLiteral("已导出 Excel：%1（3 张合并表 / %2 张报表）").arg(path).arg(reports.size()));
}

// PDF / 打印
bool MainWindow::exportPdfTo(const QString& path) {
    const auto issue=calculationIssue(project);
    if(!issue.isEmpty()){setStatus(QStringLiteral("请先补齐计算数据：")+issue);return false;}
    ensureReports();
    if (reports.isEmpty()) return false;
    QString err;
    const bool ok = reporter::exportPdf(path, reports, &err);
    if (!ok) setStatus(QStringLiteral("PDF 导出失败：%1").arg(err));
    return ok;
}

void MainWindow::exportPdf() {
    QString path = QFileDialog::getSaveFileName(this, QStringLiteral("导出 PDF"),
                                                QStringLiteral("GJG表格算量-全部报表.pdf"),
                                                QStringLiteral("PDF (*.pdf)"));
    if (path.isEmpty()) return;
    if (!exportPdfTo(path)) {
        QMessageBox::warning(this, QStringLiteral("导出失败"), QStringLiteral("PDF 写入失败"));
        return;
    }
    setStatus(QStringLiteral("已导出 PDF：%1（%2 张报表）").arg(path).arg(reports.size()));
}

void MainWindow::printCurrent() {
    ensureReports();
    if (reports.isEmpty()) return;
    QVector<Report> one{ reports[currentReportIndex()] };
    QString err;
    if (!reporter::printReports(this, one, &err)) {
        setStatus(err.isEmpty() ? QStringLiteral("已取消打印") : QStringLiteral("打印失败：%1").arg(err));
        return;
    }
    setStatus(QStringLiteral("已发送打印：%1").arg(one.first().title));
}

void MainWindow::printPreview() {
    ensureReports();
    if (reports.isEmpty()) return;
    QVector<Report> one{ reports[currentReportIndex()] };
    QString err;
    if (!reporter::previewReports(this, one, &err))
        setStatus(QStringLiteral("预览失败：%1").arg(err));
}

// ---------------------------------------------------------------------------
// 截面表与示例工程
// ---------------------------------------------------------------------------

void MainWindow::loadSectionTable() {
    QString path = QFileDialog::getOpenFileName(this, QStringLiteral("加载标准截面表"), QString(),
                                                QStringLiteral("CSV/文本 (*.csv *.txt);;所有文件 (*)"));
    if (path.isEmpty()) return;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, QStringLiteral("错误"), QStringLiteral("无法读取文件"));
        return;
    }
    int n = 0;
    while (!f.atEnd()) {
        QString line = QString::fromUtf8(f.readLine()).trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) continue;
        QStringList parts = line.split(QRegularExpression(QStringLiteral("[,;\\t]")));
        if (parts.size() < 3) continue;
        bool ok1 = false, ok2 = false;
        double kg = parts[1].trimmed().toDouble(&ok1);
        double ar = parts[2].trimmed().toDouble(&ok2);
        if (ok1 && ok2) { registerSection(parts[0].trimmed(), kg, ar); ++n; }
    }
    f.close();
    refreshInput();
    refreshSummary();
    setStatus(QStringLiteral("已加载标准截面 %1 条：%2").arg(n).arg(path));
}

void MainWindow::loadSample() {
    project = Project();
    project.name = QStringLiteral("示例钢结构工程");
    project.summaryDate = QStringLiteral("2026-11-08");
    project.floors = { QStringLiteral("首层"), QStringLiteral("二层") };

    auto mk = [](const QString& spec, const QString& mat, double len, int cnt, const QString& fl,
                 const QString& fg, const QString& rust, const QString& remark) {
        Part p;
        p.name = QStringLiteral("零件");
        p.spec = spec;
        p.material = mat;
        p.lengthMm = len;
        p.count = cnt;
        p.fireLimit = fl;
        p.fireGrade = fg;
        p.rustGrade = rust;
        p.remark = remark;
        return p;
    };
    auto addEl = [&](const QString& type, const QString& name, const QString& floor, int cnt, const Part& p) {
        Element e;
        e.typeName = type;
        e.name = name;
        e.floor = floor;
        e.count = cnt;
        e.parts = { p };
        project.elements.push_back(e);
    };
    addEl(QStringLiteral("钢柱"), QStringLiteral("KZ1"), QStringLiteral("首层"), 4,
          mk(QStringLiteral("H400x200x8x13"), QStringLiteral("Q355B"), 6000, 1,
             QStringLiteral("2.0"), QStringLiteral("二级"), QStringLiteral("Sa2.5"), QString()));
    addEl(QStringLiteral("钢柱"), QStringLiteral("KZ1"), QStringLiteral("二层"), 4,
          mk(QStringLiteral("H400x200x8x13"), QStringLiteral("Q355B"), 4000, 1,
             QStringLiteral("2.0"), QStringLiteral("二级"), QStringLiteral("Sa2.5"), QString()));
    addEl(QStringLiteral("钢梁"), QStringLiteral("GL1"), QStringLiteral("二层"), 8,
          mk(QStringLiteral("H300x150x6.5x9"), QStringLiteral("Q355B"), 6000, 1,
             QStringLiteral("1.5"), QStringLiteral("二级"), QStringLiteral("Sa2.5"), QString()));
    addEl(QStringLiteral("钢支撑"), QStringLiteral("ZC1"), QStringLiteral("首层"), 4,
          mk(QStringLiteral("L75x5"), QStringLiteral("Q235B"), 4500, 2,
             QString(), QString(), QStringLiteral("Sa2.5"), QStringLiteral("交叉支撑")));
    addEl(QStringLiteral("钢板"), QStringLiteral("GB1"), QStringLiteral("首层"), 2,
          mk(QStringLiteral("PL10"), QStringLiteral("Q235B"), 500, 1,
             QString(), QString(), QStringLiteral("Sa2.5"), QString()));

    curEl = -1;
    refreshTree();
    refreshInput();
    refreshSummary();
}
