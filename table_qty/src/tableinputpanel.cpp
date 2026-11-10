#include "tableinputpanel.h"
#include "calc.h"
#include "workspaceextras.h"
#include "pasteutils.h"
#include "partcatalog.h"
#include "sectioncalculator.h"
#include <QtWidgets>
#include <cmath>
#include <functional>
#include <algorithm>
#include <cstdio>

namespace {
const QStringList groups = {QStringLiteral("主构件"), QStringLiteral("次构件"),
    QStringLiteral("组合构件"), QStringLiteral("配件"), QStringLiteral("面板"), QStringLiteral("自定义")};
const QList<QStringList> types = {
    {QStringLiteral("钢柱"), QStringLiteral("钢梁"), QStringLiteral("吊车梁")},
    {QStringLiteral("系杆"), QStringLiteral("檩条"), QStringLiteral("隅撑"), QStringLiteral("拉条"),
     QStringLiteral("天沟"), QStringLiteral("栏杆"), QStringLiteral("门框"), QStringLiteral("窗框"),
     QStringLiteral("撑杆"), QStringLiteral("气楼"), QStringLiteral("拉索")},
    {QStringLiteral("格构柱"), QStringLiteral("钢平台"), QStringLiteral("钢楼梯"), QStringLiteral("钢爬梯"),
     QStringLiteral("型材桁架"), QStringLiteral("天窗架"), QStringLiteral("组合门架"),
     QStringLiteral("柱间支撑"), QStringLiteral("水平支撑"), QStringLiteral("马道"), QStringLiteral("支座")},
    {QStringLiteral("预埋件")},
    {QStringLiteral("屋面板"), QStringLiteral("墙面板"), QStringLiteral("楼承板"), QStringLiteral("桁架楼承板"),
     QStringLiteral("采光板"), QStringLiteral("走道板"), QStringLiteral("平台板"), QStringLiteral("格栅板")},
    // Retain the previous five engineering types without presenting them as GJG tree entries.
    {QStringLiteral("螺栓"), QStringLiteral("栓钉"), QStringLiteral("锚栓"),
     QStringLiteral("型材屋面"), QStringLiteral("钢板")}};
const QStringList headers = {
    QStringLiteral("构件类型"), QStringLiteral("构件名称"), QStringLiteral("零件类型"),
    QStringLiteral("零件名称"), QStringLiteral("编号"), QStringLiteral("材质"), QStringLiteral("规格(mm)"),
    QStringLiteral("长度公式"), QStringLiteral("长度(mm)"), QStringLiteral("数量"),
    QStringLiteral("单重(kg)"), QStringLiteral("总重(kg)"), QStringLiteral("单面积(m2)"),
    QStringLiteral("总面积(m2)"), QStringLiteral("防火时限"), QStringLiteral("防火涂料"),
    QStringLiteral("备注"), QStringLiteral("除锈等级"), QStringLiteral("底漆"), QStringLiteral("中间漆"),
    QStringLiteral("面漆"), QStringLiteral("定额子目")};
const QList<int> optionalColumns = {TableInputPanel::Code, TableInputPanel::Material,
    TableInputPanel::UnitWeight, TableInputPanel::TotalWeight, TableInputPanel::UnitArea,
    TableInputPanel::TotalArea, TableInputPanel::FireLimit, TableInputPanel::FirePaint,
    TableInputPanel::Remark, TableInputPanel::Rust, TableInputPanel::Primer,
    TableInputPanel::Mid, TableInputPanel::Top, TableInputPanel::Quota};
QString number(double value) { return !std::isfinite(value) ? QStringLiteral("待补充") :
    value == 0 ? QStringLiteral("0") : QString::number(value, 'f', 3); }

// Bounded arithmetic parser. No scripting runtime, function calls or external access.
bool evaluateLength(QString text, double& value) {
    text.remove(QRegularExpression(QStringLiteral("\\s")));
    if (text.size() > 256 || text.isEmpty()) return false;
    int pos = 0, depth = 0;
    bool ok = true;
    std::function<double()> expression, product, factor;
    factor = [&]() -> double {
        if (++depth > 32) { ok = false; --depth; return 0; }
        double v = 0;
        if (pos < text.size() && (text[pos] == '+' || text[pos] == '-')) {
            const bool minus = text[pos++] == '-';
            v = factor(); if (minus) v = -v;
        } else if (pos < text.size() && text[pos] == '(') {
            ++pos; v = expression();
            if (pos >= text.size() || text[pos++] != ')') ok = false;
        } else {
            const int start = pos;
            while (pos < text.size() && (text[pos].isDigit() || text[pos] == '.')) ++pos;
            bool parsed = false;
            v = text.mid(start, pos - start).toDouble(&parsed);
            if (!parsed) ok = false;
        }
        --depth; return v;
    };
    product = [&]() -> double {
        double v = factor();
        while (ok && pos < text.size() && (text[pos] == '*' || text[pos] == '/')) {
            const QChar op = text[pos++]; const double b = factor();
            if (op == '*') v *= b;
            else if (b == 0) ok = false;
            else v /= b;
        }
        return v;
    };
    expression = [&]() -> double {
        double v = product();
        while (ok && pos < text.size() && (text[pos] == '+' || text[pos] == '-')) {
            const QChar op = text[pos++]; const double b = product();
            v += op == '+' ? b : -b;
        }
        return v;
    };
    value = expression();
    return ok && pos == text.size() && std::isfinite(value) && value >= 0 && value <= 1e9;
}


bool editPartCell(Part& next, int column, const QString& text, QString& error) {
    bool ok = true;
    if (!next.measurement.isEmpty() && (column==TableInputPanel::Spec ||
        column==TableInputPanel::Length || column==TableInputPanel::Formula)) {
        error = QStringLiteral("参数节点的规格/尺寸由节点录入器生成；请重新添加参数节点，避免净重与尺寸失配。");
        return false;
    }
        switch (column) {
        case TableInputPanel::PartType: next.partType = text; if (next.name.isEmpty() || next.name == QStringLiteral("零件")) next.name = text; break;
        case TableInputPanel::PartName: next.name = text; ok = !text.isEmpty(); break;
        case TableInputPanel::Code: next.code = text; break;
        case TableInputPanel::Material: next.material = normalizedMaterial(text); break;
        case TableInputPanel::Spec:
            if (sectionlib::key(next.spec) != sectionlib::key(text)) next.calculation = {};
            next.spec = text; break;
        case TableInputPanel::Formula: {
            double length = 0;
            ok = text.isEmpty() || evaluateLength(text, length);
            next.lengthFormula = text;
            if (ok && !text.isEmpty()) next.lengthMm = length;
            error = QStringLiteral("长度公式支持数字、括号和 + - * /，结果须在 0 到 1,000,000,000 mm 内。");
            break;
        }
        case TableInputPanel::Length: {
            const double n = text.toDouble(&ok); ok = ok && std::isfinite(n) && n >= 0 && n <= 1e9;
            if (ok) { next.lengthMm = n; next.lengthFormula.clear(); } break;
        }
        case TableInputPanel::Count: { const int n = text.toInt(&ok); ok = ok && n > 0 && n <= 1000000; if (ok) next.count = n; break; }
        case TableInputPanel::FireLimit: next.fireLimit = text; break;
        case TableInputPanel::FirePaint: next.firePaint = text; break;
        case TableInputPanel::Remark: next.remark = text; break;
        case TableInputPanel::Rust: next.rustGrade = text; break;
        case TableInputPanel::Primer: next.primerPaint = text; break;
        case TableInputPanel::Mid: next.midPaint = text; break;
        case TableInputPanel::Top: next.topPaint = text; break;
        case TableInputPanel::Quota: next.quotaItem = text; break;
        default: break;
        }

    return ok;
}

class CompactTreeDelegate : public QStyledItemDelegate {
public:
    explicit CompactTreeDelegate(QObject* parent) : QStyledItemDelegate(parent) {}
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        QStyleOptionViewItem opt(option);
        if (opt.state.testFlag(QStyle::State_Selected)) {
            opt.rect.setWidth(qMin(opt.rect.width(), opt.fontMetrics.horizontalAdvance(index.data().toString()) + 10));
        }
        QStyledItemDelegate::paint(painter, opt, index);
    }
};

class PartTypeDelegate : public QStyledItemDelegate {
public:
    std::function<QStringList(const QModelIndex&)> choices;
    explicit PartTypeDelegate(QObject* parent) : QStyledItemDelegate(parent) {}
    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem&, const QModelIndex& index) const override {
        auto* combo = new QComboBox(parent);
        combo->setEditable(true);
        combo->addItems(choices(index));
        return combo;
    }
    void setEditorData(QWidget* editor, const QModelIndex& index) const override {
        qobject_cast<QComboBox*>(editor)->setCurrentText(index.data(Qt::EditRole).toString());
    }
    void setModelData(QWidget* editor, QAbstractItemModel* model, const QModelIndex& index) const override {
        model->setData(index, qobject_cast<QComboBox*>(editor)->currentText().trimmed());
    }
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        QStyleOptionViewItem opt(option);
        if (index.data(Qt::UserRole).toInt() >= 0) opt.rect.adjust(0, 0, -14, 0);
        QStyledItemDelegate::paint(painter, opt, index);
        if (index.data(Qt::UserRole).toInt() >= 0 &&
            (option.state.testFlag(QStyle::State_Selected) || option.state.testFlag(QStyle::State_MouseOver))) {
            QStyleOption arrow;
            arrow.rect = QRect(option.rect.right() - 12, option.rect.center().y() - 4, 9, 9);
            arrow.palette = option.palette; arrow.state = QStyle::State_Enabled;
            QApplication::style()->drawPrimitive(QStyle::PE_IndicatorArrowDown, &arrow, painter);
        }
    }
};
}

TableInputPanel::TableInputPanel(Project* proj, QWidget* parent, bool keepSettings)
    : QWidget(parent), project(proj), persist(keepSettings) {
    setObjectName(QStringLiteral("tableInputPanel"));
    QSettings settings;
    for (int c : optionalColumns) {
        const bool defaultOn = c != Rust && c != Primer && c != Mid && c != Top;
        visibility[headers[c]] = persist ? settings.value(QStringLiteral("tableInput/columns/") + headers[c], defaultOn).toBool() : defaultOn;
    }
    buildUi();
    reload();
}

QToolButton* TableInputPanel::button(const QString& text, const QString& icon, const char* name) {
    auto* b = new QToolButton(this);
    b->setObjectName(QString::fromLatin1(name));
    b->setText(text); b->setToolTip(text);
    b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    b->setIconSize(QSize(16, 16));
    iconButtons.append({b, icon});
    return b;
}

void TableInputPanel::buildUi() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 3, 8, 2); root->setSpacing(4);
    auto* title = new QLabel(QStringLiteral("表格算量"), this);
    title->setObjectName(QStringLiteral("pageTitle"));
    root->addWidget(title);
    auto* floorFrame = new QFrame(this); floorFrame->setObjectName(QStringLiteral("floorBarFrame"));
    auto* floorBar = new QHBoxLayout(floorFrame); floorBar->setContentsMargins(0, 0, 0, 0);
    floorBar->setSpacing(4);
    floorCombo = new QComboBox(this);
    floorCombo->setObjectName(QStringLiteral("floorCombo"));
    floorCombo->setFixedWidth(90);
    floorBar->addWidget(floorCombo);
    auto* copy = button(QStringLiteral("层间复制"), QStringLiteral("product/TableInputToolbar/floorCopy.svg"), "copyFloor");
    floorBar->addWidget(copy);
    auto* calculate = new QPushButton(QStringLiteral("型材查询 / 计算"), this);
    calculate->setObjectName("toggleCalculator");
    calculate->setCheckable(true);floorBar->addWidget(calculate);
    auto* undoCalc = new QPushButton(QStringLiteral("撤销本次计算"),this);floorBar->addWidget(undoCalc);
    floorBar->addStretch();
    root->addWidget(floorFrame);
    auto* split = new QSplitter(Qt::Horizontal, this);
    split->setObjectName(QStringLiteral("inputSplitter")); split->setChildrenCollapsible(false); split->setHandleWidth(5);
    root->addWidget(split, 1);
    auto makePane = [&](int minimum) {
        auto* pane = new QWidget(split); pane->setMinimumWidth(minimum);
        auto* layout = new QVBoxLayout(pane); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(3);
        split->addWidget(pane); return layout;
    };
    auto makeBar = [&](QVBoxLayout* layout) {
        auto* frame = new QFrame(this); frame->setObjectName(QStringLiteral("inputBarFrame"));
        frame->setFixedHeight(27);
        auto* bar = new QHBoxLayout(frame); bar->setContentsMargins(1, 0, 1, 0); bar->setSpacing(0);
        layout->addWidget(frame); return bar;
    };
    auto* left = makePane(105);
    auto* categoryBar = makeBar(left);
    auto* plus = button(QStringLiteral("+"), QStringLiteral("platform/Add.svg"), "addType");
    plus->setToolTip(QStringLiteral("在所选分类下新建构件类型"));
    categoryBar->addWidget(plus); categoryBar->addStretch();
    categories = new QTreeWidget(this);
    categories->setObjectName(QStringLiteral("categoryTree"));
    categories->setHeaderHidden(true); categories->setIndentation(14);
    categories->setEditTriggers(QAbstractItemView::NoEditTriggers);
    categories->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    categories->setItemDelegate(new CompactTreeDelegate(categories));
    left->addWidget(categories);
    auto* center = makePane(235);
    auto* elementBar = makeBar(center);
    auto* create = button(QStringLiteral("新建"), QStringLiteral("platform/NewFile(16).svg"), "createElement");
    auto* duplicate = button(QStringLiteral("复制"), QStringLiteral("platform/GAEA_EditCopy(16).svg"), "copyElement");
    auto* remove = button(QStringLiteral("删除"), QStringLiteral("product/TableInputToolbar/deleteRow.svg"), "deleteElement");
    auto* rename = button(QStringLiteral("重命名"), QStringLiteral("platform/Rename.svg"), "renameElement");
    for (auto* b : {create, duplicate, remove, rename}) {
        b->setFixedWidth(b == rename ? 66 : 54); elementBar->addWidget(b);
    }
    elementBar->addStretch();
    elements = new QTreeWidget(this); elements->setObjectName(QStringLiteral("elementTree"));
    elements->setHeaderHidden(true); elements->setIndentation(20);
    elements->setEditTriggers(QAbstractItemView::EditKeyPressed);
    elements->setUniformRowHeights(true); center->addWidget(elements);
    elements->setItemDelegate(new CompactTreeDelegate(elements));
    auto* right = makePane(380);
    auto* partBar = makeBar(right); partBar->setSpacing(1);
    auto* add = button(QStringLiteral("添加零件"), QStringLiteral("product/TableInputToolbar/addParts.svg"), "addPart");
    auto* del = button(QStringLiteral("删除行"), QStringLiteral("product/TableInputToolbar/deleteRow.svg"), "deleteParts");
    auto* columns = button(QStringLiteral("设置列"), QStringLiteral("product/TableInputToolbar/setColum.svg"), "columnSettings");
    auto* up = button(QStringLiteral("上移"), QStringLiteral("product/TableInputToolbar/moveUp.svg"), "moveUp");
    auto* down = button(QStringLiteral("下移"), QStringLiteral("product/TableInputToolbar/moveDown.svg"), "moveDown");
    for (auto* b : {add, del, columns, up, down}) partBar->addWidget(b);
    partBar->addStretch();
    table = new QTableWidget(0, ColumnCount, this);
    table->setObjectName(QStringLiteral("partsTable"));
    table->setHorizontalHeaderLabels(headers);
    auto* pasteAction = new QAction(QStringLiteral("粘贴 Excel 矩阵"), table);
    pasteAction->setShortcut(QKeySequence::Paste);
    pasteAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    table->addAction(pasteAction);
    connect(pasteAction, &QAction::triggered, this, &TableInputPanel::pasteClipboard);
auto* nodeAction = new QAction(QStringLiteral("参数节点零件…"), table);
    connect(nodeAction, &QAction::triggered, this, [this]() {
        if (selectedElement < 0 || selectedElement >= project->elements.size()) return;
        if (extras::insertNode(this, project->elements[selectedElement])) {
            collapsedElements.remove(selectedElement); refreshRows(); emit dataChanged();
        }
    });
    auto* blankAction = new QAction(QStringLiteral("插入空白零件行"), table);
    connect(blankAction, &QAction::triggered, this, [this]() { appendParts({QString()}); });
    auto* extraMenu = new QMenu(add);
    extraMenu->addAction(blankAction); extraMenu->addAction(nodeAction);
    extraMenu->addSeparator(); extraMenu->addAction(pasteAction);
    add->setMenu(extraMenu); add->setPopupMode(QToolButton::MenuButtonPopup);
    table->addAction(blankAction); table->addAction(nodeAction);
    table->setContextMenuPolicy(Qt::ActionsContextMenu);

    table->setAlternatingRowColors(false);
    table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::SelectedClicked | QAbstractItemView::EditKeyPressed);
    table->setWordWrap(false);
    table->horizontalHeader()->setMinimumSectionSize(48);
    table->horizontalHeader()->setDefaultSectionSize(100);
    table->horizontalHeader()->setFixedHeight(21);
    table->horizontalHeader()->setDefaultAlignment(Qt::AlignCenter);
    table->verticalHeader()->setMinimumSectionSize(21);
    table->verticalHeader()->setDefaultSectionSize(21);
    table->verticalHeader()->setMinimumWidth(22);
    table->setColumnWidth(ElementType, 80); table->setColumnWidth(Spec, 100); table->setColumnWidth(Formula, 100);
    table->setColumnWidth(PartType, 102); table->setColumnWidth(ElementName, 102);
    table->setColumnWidth(Count, 100);
    auto* delegate = new PartTypeDelegate(table);
    delegate->choices = [this](const QModelIndex& index) {
        const int e = index.data(Qt::UserRole + 1).toInt();
        return e >= 0 && e < project->elements.size() ? partTypesFor(project->elements[e].typeName) : partTypes();
    };
    table->setItemDelegateForColumn(PartType, delegate);
    auto* materialDelegate = new PartTypeDelegate(table);
    materialDelegate->choices = [](const QModelIndex&) { return materialChoices(); };
    table->setItemDelegateForColumn(Material, materialDelegate);
    right->addWidget(table);
    footer = new QLabel(this); footer->setObjectName(QStringLiteral("inputFooter")); right->addWidget(footer);
    split->setStretchFactor(0, 0); split->setStretchFactor(1, 0); split->setStretchFactor(2, 1);
    split->setSizes({140, 240, 1170});
    auto* scroll = new QScrollArea(split);
    scroll->setWidgetResizable(true);scroll->setMinimumWidth(385);
    calculator = new SectionCalculator(scroll);scroll->setWidget(calculator);
    split->addWidget(scroll);scroll->hide();
    connect(calculate,&QPushButton::toggled,this,[this,scroll](bool on){
        scroll->setVisible(on);if(on)syncCalculator();
    });
    connect(calculator,&SectionCalculator::commitRequested,this,&TableInputPanel::commitCalculation);
    connect(table,&QTableWidget::currentCellChanged,this,[this]{
        if(!updating)syncCalculator();
    });
    connect(undoCalc,&QPushButton::clicked,this,[this]{
        for(auto& e:project->elements)for(auto& p:e.parts)if(p.id==lastCalculationPart) {
            if(p.toJson()!=lastCalculationAfter) {
                emit statusMessage(QStringLiteral("该零件已被其他编辑修改，无法覆盖撤销。"));return;
            }
            p=Part::fromJson(lastCalculationBefore);lastCalculationPart.clear();
            refreshRows();emit dataChanged();return;
        }
    });
    connect(plus, &QToolButton::clicked, this, &TableInputPanel::addCategoryType);
    connect(copy, &QToolButton::clicked, this, &TableInputPanel::copyFloor);
    connect(create, &QToolButton::clicked, this, &TableInputPanel::createElement);
    connect(duplicate, &QToolButton::clicked, this, &TableInputPanel::copyElement);
    connect(remove, &QToolButton::clicked, this, &TableInputPanel::deleteElement);
    connect(rename, &QToolButton::clicked, this, &TableInputPanel::renameElement);
    connect(add, &QToolButton::clicked, this, &TableInputPanel::addPart);
    connect(del, &QToolButton::clicked, this, &TableInputPanel::deleteParts);
    connect(columns, &QToolButton::clicked, this, &TableInputPanel::showColumnSettings);
    connect(up, &QToolButton::clicked, this, [this]() { movePart(-1); });
    connect(down, &QToolButton::clicked, this, [this]() { movePart(1); });
    connect(table, &QTableWidget::cellChanged, this, &TableInputPanel::cellChanged);
    connect(table, &QTableWidget::cellClicked, this, [this](int row, int col) {
        if (row < 0 || row >= rowRefs.size()) return;
        const auto ref = rowRefs[row];
        if (ref.part == -1 && col == ElementType) {
            if (collapsedElements.contains(ref.element)) collapsedElements.remove(ref.element);
            else collapsedElements.insert(ref.element);
            refreshRows();
        }
    });
    connect(floorCombo, &QComboBox::currentTextChanged, this, [this]() { if (!updating) refreshElements(); });
    connect(categories, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* it) {
        if (updating || !it) return;
        selectedCategory = it->parent() ? it->parent()->text(0) : it->text(0);
        selectedType = it->parent() ? it->text(0) : QString();
        refreshElements();
    });
connect(table, &QTableWidget::currentCellChanged, this, [this](int row) {
        if (updating || row < 0 || row >= rowRefs.size()) return;
        selectedElement = rowRefs[row].element; syncTreeSelection();
    });
    connect(elements, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* it) {
        if (updating || !it || !it->data(0, Qt::UserRole).isValid()) return;
        selectedElement = it->data(0, Qt::UserRole).toInt();
        const int row = rowFor(selectedElement);
        if (row >= 0) {
            table->setCurrentCell(row, ElementName);
            table->scrollToItem(table->item(row, ElementName), QAbstractItemView::EnsureVisible);
        }
    });
    connect(elements, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* it) {
        if (updating || !it || !it->data(0, Qt::UserRole).isValid()) return;
        const int index = it->data(0, Qt::UserRole).toInt();
        QString name = it->text(0).trimmed();
        if (name.startsWith(QStringLiteral("(单)"))) name = name.mid(3).trimmed();
        bool valid = !name.isEmpty();
        for (int i = 0; i < project->elements.size(); ++i)
            if (i != index && project->elements[i].floor == project->elements[index].floor &&
                project->elements[i].typeName == project->elements[index].typeName &&
                project->elements[i].name == name) valid = false;
        if (valid) project->elements[index].name = name;
        else emit statusMessage(QStringLiteral("构件名称不能为空，同楼层同类型下不得重名。"));
        // Do not destroy an item while its editor is committing data.
        QTimer::singleShot(0, this, [this, valid]() { refreshElements(); if (valid) emit dataChanged(); });
    });
    connect(elements, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* it) {
        if (it && it->data(0, Qt::UserRole).isValid()) renameElement();
    });


}

QString TableInputPanel::categoryFor(const Element& el) const {
    if (groups.contains(el.category)) return el.category;
    for (int i = 0; i < groups.size(); ++i)
        if (types[i].contains(el.typeName) || project->customTypes.value(groups[i]).contains(el.typeName))
            return groups[i];
    if (el.typeName == QStringLiteral("钢支撑")) return groups[1];
    if (el.typeName == QStringLiteral("钢桁架") || el.typeName == QStringLiteral("门式刚架")) return groups[2];
    return groups.last();
}

void TableInputPanel::rebuildCategories() {
    categories->clear();
    QTreeWidgetItem* selection = nullptr;
    for (int i = 0; i < groups.size(); ++i) {
        auto* group = new QTreeWidgetItem(categories, {groups[i]});
        QStringList children = types[i] + project->customTypes.value(groups[i]);
        for (const auto& el : project->elements)
            if (categoryFor(el) == groups[i]) children << el.typeName;
        children.removeDuplicates();
        if (selectedCategory == groups[i] && selectedType.isEmpty()) selection = group;
        for (const auto& type : children) {
            auto* it = new QTreeWidgetItem(group, {type});
            it->setToolTip(0, partcatalog::verified(type)
                ? QStringLiteral("零件目录与新建默认值已按 GJG2026 实机核对。")
                : QStringLiteral("可编辑工程预设；本类型零件目录尚未完成实机核对。"));
            if (selectedCategory == groups[i] && selectedType == type) selection = it;
        }
        group->setExpanded(i < 3 || selectedCategory == groups[i]);
    }
    if (!selection) selection = categories->topLevelItem(0)->child(0);
    categories->setCurrentItem(selection);
    if (selection) {
        selectedCategory = selection->parent() ? selection->parent()->text(0) : selection->text(0);
        selectedType = selection->parent() ? selection->text(0) : QString();
    }
}

void TableInputPanel::reload() {
    updating = true;
    const QString oldFloor = floorCombo->currentText();
    floorCombo->clear(); floorCombo->addItems(project->floors);
    const int idx = floorCombo->findText(oldFloor);
    if (idx >= 0) floorCombo->setCurrentIndex(idx);
    rebuildCategories();
    updating = false;
    refreshElements();
}

void TableInputPanel::refreshElements() {
    updating = true;
    const int old = selectedElement;
    elements->clear(); visibleElements.clear(); selectedElement = -1;
    QMap<QString, QTreeWidgetItem*> roots;
    QTreeWidgetItem* selection = nullptr;
    for (int i = 0; i < project->elements.size(); ++i) {
        const auto& el = project->elements[i];
        if (el.floor != floorCombo->currentText() || categoryFor(el) != selectedCategory ||
            (!selectedType.isEmpty() && el.typeName != selectedType)) continue;
        auto* parent = roots.value(el.typeName);
        if (!parent) {
            parent = new QTreeWidgetItem(elements, {el.typeName});
            parent->setFlags(Qt::ItemIsEnabled);
            parent->setExpanded(true); roots[el.typeName] = parent;
        }
        auto* it = new QTreeWidgetItem(parent, {QStringLiteral("(单)%1").arg(el.name)});
        it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable);
        it->setData(0, Qt::UserRole, i);
        it->setToolTip(0, QStringLiteral("%1 · %2 · 数量 %3").arg(el.typeName, el.floor).arg(el.count));
        visibleElements << i;
        if (i == old) selection = it;
    }
    if (roots.isEmpty()) {
        auto* parent = new QTreeWidgetItem(elements, {selectedType.isEmpty() ? selectedCategory : selectedType});
        parent->setFlags(Qt::ItemIsEnabled); parent->setExpanded(true);
    }
    if (!selection && elements->topLevelItemCount() && elements->topLevelItem(0)->childCount())
        selection = elements->topLevelItem(0)->child(0);
    elements->setCurrentItem(selection);
    if (selection) selectedElement = selection->data(0, Qt::UserRole).toInt();
    updating = false;
    refreshRows();
}

void TableInputPanel::syncTreeSelection() {
    const QSignalBlocker blocker(elements);
    for (int r = 0; r < elements->topLevelItemCount(); ++r)
        for (int c = 0; c < elements->topLevelItem(r)->childCount(); ++c) {
            auto* it = elements->topLevelItem(r)->child(c);
            if (it->data(0, Qt::UserRole).toInt() == selectedElement) {
                elements->setCurrentItem(it); elements->scrollToItem(it); return;
            }
        }
}

int TableInputPanel::rowFor(int element, int part) const {
    const int parent = elementRows.value(element, -1);
    if (parent < 0 || part < -1) return -1;
    const int row = parent + part + 1;
    return row < rowRefs.size() && rowRefs[row].element == element && rowRefs[row].part == part ? row : -1;
}

QStringList TableInputPanel::partTypesFor(const QString& type) const { return partcatalog::names(type); }
QStringList TableInputPanel::partTypes() const {
    return partTypesFor(selectedElement >= 0 && selectedElement < project->elements.size()
        ? project->elements[selectedElement].typeName : selectedType);
}

void TableInputPanel::refreshRows() {
    updating = true;
    const int oldCol = table->currentColumn();
    const auto oldRef = table->currentRow() >= 0 && table->currentRow() < rowRefs.size()
        ? rowRefs[table->currentRow()] : RowRef();
    const int vscroll = table->verticalScrollBar()->value(), hscroll = table->horizontalScrollBar()->value();
    table->clearContents(); rowRefs.clear(); elementRows.clear();
    for (int e : visibleElements) {
        elementRows[e] = rowRefs.size();
        rowRefs.append({e, -1});
        for (int p = 0; p < project->elements[e].parts.size(); ++p) rowRefs.append({e, p});
    }
    table->setRowCount(rowRefs.size());
    auto cell = [&](int row, int col, const QString& text, bool editable) {
        auto* it = new QTableWidgetItem(text);
        it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | (editable ? Qt::ItemIsEditable : Qt::NoItemFlags));
        it->setData(Qt::UserRole, rowRefs[row].part);
        it->setData(Qt::UserRole + 1, rowRefs[row].element);
        it->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        it->setToolTip(text); table->setItem(row, col, it);
    };
    double totalWeight = 0, totalArea = 0; int partCount = 0;
    for (int e : visibleElements) {
        const auto& el = project->elements[e]; const int parent = rowFor(e);
        for (int c = 0; c < ColumnCount; ++c) cell(parent, c, {}, false);
        cell(parent, ElementType, QStringLiteral("%1 %2").arg(collapsedElements.contains(e) ? QStringLiteral("⊞") : QStringLiteral("⊟"), el.typeName), false);
        cell(parent, ElementName, QStringLiteral("(单)%1").arg(el.name), true);
        cell(parent, Count, QString::number(el.count), true);
        table->setRowHidden(parent, false);
        double tw = 0, ta = 0;
        for (int pindex = 0; pindex < el.parts.size(); ++pindex) {
            const auto& p = el.parts[pindex]; const auto q = rowCalc(el, p);
            tw += q.totalWeight; ta += q.totalArea; ++partCount;
            const int row = rowFor(e, pindex);
            const QStringList values = {QString(), QString(), p.partType,
                p.name, p.code, p.material, p.spec, p.lengthFormula, QString::number(p.lengthMm, 'g', 12),
                QString::number(p.count), number(q.unitWeight), number(q.totalWeight), number(q.unitArea),
                number(q.totalArea), p.fireLimit, p.firePaint, p.remark, p.rustGrade,
                p.primerPaint, p.midPaint, p.topPaint, p.quotaItem};
            for (int c = 0; c < ColumnCount; ++c)
                cell(row, c, values[c], c >= PartType && !(c >= UnitWeight && c <= TotalArea));
            if (!std::isfinite(q.unitWeight) || !std::isfinite(q.unitArea)) {
                table->item(row,Spec)->setBackground(QColor("#fff0b3"));
                table->item(row,Spec)->setToolTip(QStringLiteral("计算数据待补充；请打开型材查询 / 计算选择规格或输入非标尺寸。"));
            } else if (!p.calculation.isEmpty()) {
                table->item(row,Spec)->setToolTip(p.calculation["standard"].toString()+"\n"+
                    p.calculation["source"].toString()+"\n"+p.calculation["surface_basis"].toString());
            }
            table->setRowHidden(row, collapsedElements.contains(e));
        }
        cell(parent, TotalWeight, number(tw), false); cell(parent, TotalArea, number(ta), false);
        totalWeight += tw; totalArea += ta;
    }
    footer->setText(visibleElements.isEmpty() ? QStringLiteral("请选择类型并点击“新建”，直接建立构件。")
        : QStringLiteral("提示信息：%1 个构件 ｜ %2 行零件 ｜ 总重 %3 kg ｜ 总面积 %4 m²")
            .arg(visibleElements.size()).arg(partCount).arg(number(totalWeight), number(totalArea)));
    for (int c : optionalColumns) table->setColumnHidden(c, !visibility.value(headers[c]));
    int row = oldRef.element == selectedElement ? rowFor(oldRef.element, oldRef.part) : -1;
    if (row < 0 || table->isRowHidden(row)) row = rowFor(selectedElement);
    if (row >= 0) table->setCurrentCell(row, oldCol >= 0 ? oldCol : ElementName);
    table->verticalScrollBar()->setValue(vscroll); table->horizontalScrollBar()->setValue(hscroll);
    updating = false;
    syncCalculator();
}

void TableInputPanel::syncCalculator() {
    if (!calculator || updating) return;
    const int row=table->currentRow();
    if(row<0 || row>=rowRefs.size()) {calculator->bind(nullptr);return;}
    const auto ref=rowRefs[row];
    if(ref.element<0 || ref.element>=project->elements.size() || ref.part<0 ||
        ref.part>=project->elements[ref.element].parts.size()) {calculator->bind(nullptr);return;}
    const auto& e=project->elements[ref.element];
    calculator->bind(&e.parts[ref.part],e.count);
}
void TableInputPanel::commitCalculation(QJsonObject record,double length,int count,QString material,bool clearManual) {
    const int row=table->currentRow();
    if(updating || row<0 || row>=rowRefs.size() || !sectionlib::valid(record))return;
    const auto ref=rowRefs[row];
    if(ref.element<0 || ref.element>=project->elements.size() || ref.part<0 ||
        ref.part>=project->elements[ref.element].parts.size())return;
    auto& part=project->elements[ref.element].parts[ref.part];
    if(!part.measurement.isEmpty()) {
        emit statusMessage(QStringLiteral("此行为参数节点，请在节点录入器编辑；可新建普通零件使用型材计算。"));return;
    }
    const auto before=part.toJson();
    part.spec=record["spec"].toString();part.calculation=record;
    part.lengthMm=length;part.lengthFormula.clear();part.count=count;part.material=normalizedMaterial(material);
    if(clearManual) {part.measuredWeightKg=-1;part.measuredAreaM2=-1;}
    if(before==part.toJson())return;
    if(lastCalculationPart!=part.id || lastCalculationAfter!=before) {
        lastCalculationBefore=before;lastCalculationPart=part.id;
    }
    lastCalculationAfter=part.toJson();
    refreshRows();emit dataChanged();
}

bool TableInputPanel::runCalculationChecks(QString* error) {
    int n=0;
    auto check=[&](bool ok,const char* label) {
        ++n;if(!ok && error)*error=QString::fromUtf8(label);
        std::printf("section assertion %d %s: %s\n",n,ok?"ok":"FAILED",label);
        return ok;
    };
    if(!check(sectionlib::records().size()==3711,"merged library loaded"))return false;
    QSet<QString> identities;
    for(const auto& r:sectionlib::records()) {
        if(identities.contains(r["id"].toString()))return check(false,"duplicate record id");
        identities.insert(r["id"].toString());
    }
    if(!check(normalizedMaterial("Q345B")=="Q355B" &&
        Part::fromJson(QJsonObject{{"material","Q345D"}}).material=="Q355D","material migration"))return false;
    project->elements.clear();Element e;e.name="CalcTest";e.count=3;
    Part first;first.name="First";first.spec.clear();e.parts={first,Part()};project->elements={e};reload();
    table->setCurrentCell(rowFor(0,0),Spec);
    if(!check(calculator!=nullptr,"calculator exists"))return false;
    QJsonObject record;
    for(const auto& r:sectionlib::records())if(r["spec"]=="I10" && r["standard"]=="GB/T 706-2016"){record=r;break;}
    if(!check(!record.isEmpty() && std::abs(record["mass"].toDouble()-11.3)<1e-9 &&
        std::abs(record["surface"].toDouble()-0.432)<1e-9,"I10 stored values match supplied screenshot"))return false;
    commitCalculation(record,6000,2,"Q345B",false);
    auto q=rowCalc(project->elements[0],project->elements[0].parts[0]);
    if(!check(std::abs(q.totalWeight-406.8)<1e-8 && q.count==6,"length quantity and parent multiplicity"))return false;
    if(!check(project->elements[0].parts[0].material=="Q355B","inline commit normalizes material"))return false;
    const auto json=project->toJson();const auto restored=Project::fromJson(json);
    if(!check(restored.toJson()==json && std::abs(rowCalc(restored.elements[0],restored.elements[0].parts[0]).totalWeight-406.8)<1e-8,"save roundtrip retains coefficient snapshot"))return false;
    auto* materialDelegate=table->itemDelegateForColumn(Material);
    QScopedPointer<QWidget> editor(materialDelegate->createEditor(table,QStyleOptionViewItem(),table->model()->index(rowFor(0,0),Material)));
    auto* combo=qobject_cast<QComboBox*>(editor.data());
    if(!check(combo && combo->findText("Q355B")>=0 && combo->findText("Q345B")<0,"material dropdown"))return false;
    editor.reset();
    auto* live=calculator->findChild<QCheckBox*>("calculationLive");
    auto* length=calculator->findChild<QDoubleSpinBox*>("calculatorLength");
    auto* mode=calculator->findChild<QComboBox*>("calculationMode");
    live->setChecked(true);length->setValue(3000);
    if(!check(std::abs(rowCalc(project->elements[0],project->elements[0].parts[0]).totalWeight-203.4)<1e-8,"live input updates table"))return false;
    table->setCurrentCell(rowFor(0,1),Spec);
    if(!check(!live->isChecked(),"changing part disarms live binding"))return false;
    length->setValue(8000);
    if(!check(project->elements[0].parts[1].calculation.isEmpty() && project->elements[0].parts[1].lengthMm==1000,"preview does not write another row"))return false;
    table->setCurrentCell(rowFor(0,0),Spec);
    project->elements[0].parts[0].measuredWeightKg=99;syncCalculator();
    commitCalculation(record,2000,1,"Q355B",false);
    if(!check(rowCalc(project->elements[0],project->elements[0].parts[0]).unitWeight==99,"manual weight remains locked"))return false;
    commitCalculation(record,2000,1,"Q355B",true);
    if(!check(std::abs(rowCalc(project->elements[0],project->elements[0].parts[0]).unitWeight-22.6)<1e-9,"explicit manual unlock"))return false;
    auto plate=sectionlib::formula(5,200,0,10,0);
    commitCalculation(plate,400,1,"Q355B",true);
    q=rowCalc(project->elements[0],project->elements[0].parts[0]);
    if(!check(std::abs(q.unitWeight-6.28)<1e-9 && std::abs(q.unitArea-0.16)<1e-9,"nonstandard plate has actual width and two faces"))return false;
    mode->setCurrentIndex(1);
    auto* shape=calculator->findChild<QComboBox*>("sectionShape");shape->setCurrentIndex(1);
    calculator->findChild<QDoubleSpinBox*>("dimensionH")->setValue(100);
    calculator->findChild<QDoubleSpinBox*>("dimensionTw")->setValue(5);
    calculator->findChild<QPushButton*>("applyCalculation")->click();
    const double expected=kPi*(10000-8100)/4*0.00785*0.4;
    if(!check(std::abs(rowCalc(project->elements[0],project->elements[0].parts[0]).unitWeight-expected)<1e-9,"custom UI formula writes unified result"))return false;
    auto* t=calculator->findChild<QDoubleSpinBox*>("dimensionTw");t->setValue(60);
    if(!check(!calculator->findChild<QPushButton*>("applyCalculation")->isEnabled() &&
        std::abs(rowCalc(project->elements[0],project->elements[0].parts[0]).unitWeight-expected)<1e-9,"invalid dimensions do not overwrite"))return false;
    if(!check(sectionlib::formula(0,10,20,5,6).isEmpty() &&
        sectionlib::formula(2,100,50,30,0).isEmpty(),"invalid geometry rejected"))return false;
    Part missing;missing.spec="UNKNOWN_SECTION";
    if(!check(!std::isfinite(rowCalc(e,missing).unitWeight),"unknown spec not silently zero"))return false;
    Part ar;ar.spec="AreaTest";ar.calculation={{"id","area-test"},{"spec","AreaTest"},{"mass",78.5},
        {"surface",2.0},{"unit","kg/m2"},{"width_mm",200}};
    ar.lengthMm=400;
    if(!check(std::abs(rowCalc(e,ar).unitWeight-6.28)<1e-9 && std::abs(rowCalc(e,ar).unitArea-0.16)<1e-9,"areal mass uses width and length"))return false;
    ar.calculation.remove("width_mm");
    if(!check(!std::isfinite(rowCalc(e,ar).unitWeight),"missing plate width rejected"))return false;
    if(!check(calculationIssue(Project::fromJson(QJsonObject{{"elements",QJsonArray{QJsonObject{{"parts",QJsonArray{missing.toJson()}}}}}})).contains(QStringLiteral("尚未补齐")),
        "incomplete project identified"))return false;
    copyElement();
    if(!check(project->elements[0].parts[0].id!=project->elements.last().parts[0].id,"copied components have distinct row identity"))return false;
    // Use the actual widget for the deliverable screenshot, without opening a desktop window.
    selectElement(0);table->setCurrentCell(rowFor(0,0),Spec);
    calculator->findChild<QDoubleSpinBox*>("dimensionTw")->setValue(5);
    findChild<QPushButton*>("toggleCalculator")->setChecked(true);
    resize(1620,920);show();QCoreApplication::processEvents();
    const auto args=QCoreApplication::arguments();const int out=args.indexOf("--selftest-dir");
    if(out>=0 && out+1<args.size()) {
        QDir().mkpath(args[out+1]);
        if(!check(grab().save(QDir(args[out+1]).filePath("ui_calculator.png")),"calculator screenshot"))return false;
        const QString outputFolder=args.at(out+1);
        QFile f{QDir(outputFolder).filePath("calculator-project.json")};
        if(!check(f.open(QIODevice::WriteOnly) && f.write(QJsonDocument(project->toJson()).toJson())>0,"sample project persisted"))return false;
        mode->setCurrentIndex(0);
        calculator->findChild<QLineEdit*>("sectionSearch")->setText("I10");
        if(!check(calculator->findChild<QListWidget*>("sectionResults")->count()==2,
            "standard search retains distinct editions"))return false;
        const auto newEdition=sectionlib::resolve("I10");
        const auto oldEdition=*std::find_if(sectionlib::records().cbegin(),sectionlib::records().cend(),
            [](const QJsonObject& r){return r["spec"]=="I10" && r["standard"]=="GB/T 706-2008";});
        calculator->findChild<QListWidget*>("sectionResults")->setCurrentRow(1);
        commitCalculation(newEdition,1000,1,"Q355B",true);
        calculator->findChild<QPushButton*>("applyCalculation")->click();
        if(!check(grab().save(QDir(outputFolder).filePath("ui_standard_library.png")) &&
            newEdition["id"]!=oldEdition["id"],"standard library screenshot and edition identity"))return false;
    }
    hide();return true;
}

void TableInputPanel::cellChanged(int row, int column) {
    if (updating || row < 0 || row >= rowRefs.size() || !table->item(row, column)) return;
    const auto ref = rowRefs[row];
    if (ref.element < 0 || ref.element >= project->elements.size()) return;
    auto& el = project->elements[ref.element]; selectedElement = ref.element; syncTreeSelection();
    QString text = table->item(row, column)->text().trimmed();
    QString error; bool ok = true;
    if (ref.part == -1) {
        if (column == ElementName) {
            if (text.startsWith(QStringLiteral("(单)"))) text = text.mid(3).trimmed();
            ok = !text.isEmpty();
            for (int i = 0; i < project->elements.size(); ++i)
                if (i != ref.element && project->elements[i].floor == el.floor &&
                    project->elements[i].typeName == el.typeName && project->elements[i].name == text) ok = false;
            if (ok) el.name = text;
            else error = QStringLiteral("构件名称不能为空，同楼层同类型下不得重名。");
        } else if (column == Count) {
            const int n = text.toInt(&ok); ok = ok && n > 0 && n <= 1000000;
            if (ok) el.count = n;
        }
    } else if (ref.part < el.parts.size()) {
        Part next = el.parts[ref.part];
        ok = editPartCell(next, column, text, error);
        if (ok) el.parts[ref.part] = next;
    }
    if (!ok) {
        if (error.isEmpty()) error = QStringLiteral("名称不能为空；数量须为 1 到 1,000,000 的整数，长度须为有效非负数。");
        refreshRows(); footer->setText(error); emit statusMessage(error); return;
    }
    if (ref.part == -1 && column == ElementName) refreshElements(); else refreshRows();
    emit dataChanged();
}

int TableInputPanel::categoryCount() const { return categories->topLevelItemCount(); }
int TableInputPanel::currentPartIndex() const {
    const int row = table->currentRow();
    return row >= 0 && row < rowRefs.size() ? rowRefs[row].part : -1;
}

bool TableInputPanel::columnVisible(const QString& name) const { return visibility.value(name, true); }
void TableInputPanel::setColumnVisible(const QString& name, bool on) {
    const int c = headers.indexOf(name);
    if (!optionalColumns.contains(c)) return;
    visibility[name] = on; table->setColumnHidden(c, !on);
}
void TableInputPanel::saveColumns() {
    if (!persist) return;
    QSettings settings;
    for (auto it = visibility.cbegin(); it != visibility.cend(); ++it)
        settings.setValue(QStringLiteral("tableInput/columns/") + it.key(), it.value());
}

QDialog* TableInputPanel::createColumnDialog() {
    auto* dlg = new QDialog(this);
    dlg->setObjectName(QStringLiteral("columnDialog"));
    dlg->setWindowTitle(QStringLiteral("设置列")); dlg->setMinimumWidth(280);
    dlg->setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    dlg->setStyleSheet(QStringLiteral(
        "QPushButton#confirmColumns { background:#2f80d8; color:white; border:1px solid #2f80d8; }"
        "QPushButton#confirmColumns:hover { background:#2472c6; }"));
    auto* layout = new QVBoxLayout(dlg); layout->setSpacing(0);
    layout->addWidget(new QLabel(QStringLiteral("设置列"), dlg));
    QList<QCheckBox*> boxes;
    for (int c : optionalColumns) {
        auto* box = new QCheckBox(headers[c], dlg);
        box->setObjectName(QStringLiteral("column_%1").arg(c));
        box->setProperty("columnOption", true);
        box->setMinimumHeight(28); box->setChecked(visibility.value(headers[c]));
        layout->addWidget(box); boxes << box;
    }
    layout->addSpacing(8);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dlg);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("确定"));
    buttons->button(QDialogButtonBox::Ok)->setDefault(true);
    buttons->button(QDialogButtonBox::Ok)->setProperty("primary", true);
    buttons->button(QDialogButtonBox::Ok)->setObjectName(QStringLiteral("confirmColumns"));
    buttons->button(QDialogButtonBox::Ok)->setStyleSheet(QStringLiteral(
        "QPushButton { background-color:#2f80d8; color:#ffffff; border:1px solid #2f80d8; }"
        "QPushButton:hover { background-color:#2472c6; }"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, dlg, [this, dlg, boxes]() {
        for (auto* box : boxes) setColumnVisible(box->text(), box->isChecked());
        saveColumns(); dlg->accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, dlg, &QDialog::reject);
    return dlg;
}
void TableInputPanel::showColumnSettings() {
    QScopedPointer<QDialog> dlg(createColumnDialog()); dlg->exec();
}

void TableInputPanel::setTheme(const QString& assets, bool dark) {
    for (const auto& pair : iconButtons) {
        QString path = assets + QStringLiteral("/icons/") + (dark ? QStringLiteral("dark/") : QStringLiteral("light/")) + pair.second;
        if (!QFile::exists(path)) path = assets + QStringLiteral("/icons/light/") + pair.second;
        pair.first->setIcon(QIcon(path));
    }
    const QString bg = dark ? QStringLiteral("#25282e") : QStringLiteral("#ffffff");
    const QString border = dark ? QStringLiteral("#484c55") : QStringLiteral("#d7d7d7");
    const QString selected = dark ? QStringLiteral("#365d86") : QStringLiteral("#d3e7fb");
const QString fg = dark ? QStringLiteral("#eeeeee") : QStringLiteral("#111111");
    const QString head = dark ? QStringLiteral("#33373f") : QStringLiteral("#efefef");
    setStyleSheet(QStringLiteral(
        "QWidget#tableInputPanel { background:%1; color:%4; font-family:'Microsoft YaHei UI'; font-size:9pt; }"
        "QLabel#pageTitle { padding:0px 0px 4px; font-size:10pt; }"
        "QFrame#floorBarFrame, QFrame#inputBarFrame { border:1px solid %2; background:%1; }"
        "QLabel#inputFooter { padding:1px 3px; font-size:8pt; }"
        "QWidget#tableInputPanel QToolButton { padding:0px 2px; min-height:23px; border:0; background:transparent; }"
        "QWidget#tableInputPanel QToolButton:hover { background:%3; }"
        "QComboBox#floorCombo { min-height:22px; padding:0px 3px; }"
        "QTreeWidget#elementTree, QTreeWidget#categoryTree { border:1px solid %2; background:%1; color:%4; padding:2px 3px; }"
        "QTreeWidget#elementTree::item, QTreeWidget#categoryTree::item { height:24px; padding:0px; }"
        "QTreeWidget#elementTree::item:selected, QTreeWidget#categoryTree::item:selected { background:%3; color:%4; }"
        "QTableWidget#partsTable { border:1px solid %2; gridline-color:%2; background:%1; color:%4; selection-background-color:#64a6ef; selection-color:white; }"
        "QTableWidget#partsTable::item { padding:0px 3px; border:0; }"
        "QTableWidget#partsTable QHeaderView::section { background:%5; color:%4; border:0; border-right:1px solid %2; border-bottom:1px solid %2; padding:0px; }"
        "QTableWidget#partsTable QHeaderView::section:checked { background:#7db6ef; color:white; }"
        "QDialog#columnDialog QCheckBox { border:1px solid %2; border-bottom:0; padding-left:5px; }"
        "QDialog#columnDialog QPushButton[primary=\"true\"] { background:#2f80d8; color:white; }"
        ).arg(bg, border, selected, fg, head));


}

int TableInputPanel::validIconCount() const {
    int count = 0;
    for (const auto& pair : iconButtons) if (!pair.first->icon().isNull()) ++count;
    return count;
}

void TableInputPanel::selectElement(int index) {
    if (index < 0 || index >= project->elements.size()) return;
    selectedElement = index;
    const auto el = project->elements[index];
    selectedCategory = categoryFor(el); selectedType = el.typeName;
    { QSignalBlocker block(floorCombo); floorCombo->setCurrentText(el.floor); }
    reload();
}

void TableInputPanel::createElement() {
    if (selectedType.isEmpty()) {
        const auto* group = categories->currentItem();
        if (!group || !group->childCount()) {
            emit statusMessage(QStringLiteral("请先选择或添加构件类型。")); return;
        }
        selectedType = group->child(0)->text(0);
    }
    Element el; el.typeName = selectedType; el.floor = floorCombo->currentText();
    if (el.floor.isEmpty()) { el.floor = QStringLiteral("首层"); project->floors << el.floor; }
    el.category = selectedCategory; el.count = 1;
    const QString prefix = partcatalog::prefix(el.typeName) + QStringLiteral("-");
    int serial = 0;
    for (const auto& other : project->elements) {
        if (other.floor != el.floor || other.typeName != el.typeName || !other.name.startsWith(prefix)) continue;
        bool numeric = false; const int n = other.name.mid(prefix.size()).toInt(&numeric);
        if (numeric) serial = qMax(serial, n);
    }
    el.name = prefix + QString::number(serial + 1);
    el.parts = {partcatalog::blank(partTypesFor(el.typeName).first(), el.typeName),
                partcatalog::blank({}, el.typeName)};
    project->elements << el;
    selectElement(project->elements.size() - 1);
    const int row = rowFor(selectedElement);
    table->setCurrentCell(row, ElementName);
    table->scrollToItem(table->item(row, ElementName));
    table->horizontalScrollBar()->setValue(0);
    emit dataChanged();
}

void TableInputPanel::appendParts(const QStringList& names) {
    if (selectedElement < 0 || selectedElement >= project->elements.size() || names.isEmpty()) return;
    const int e = selectedElement;
    auto& parts = project->elements[e].parts;
    const int first = parts.size();
    for (const auto& name : names) parts << partcatalog::blank(name, project->elements[e].typeName);
    collapsedElements.remove(e); refreshRows();
    table->setCurrentCell(rowFor(e, first), PartType);
    table->scrollToItem(table->currentItem()); emit dataChanged();
}

QDialog* TableInputPanel::createPartDialog() {
    auto* dlg = new QDialog(this); dlg->setObjectName(QStringLiteral("partCatalogDialog"));
    dlg->setWindowTitle(QStringLiteral("选择零部件")); dlg->setMinimumWidth(720);
    auto* layout = new QVBoxLayout(dlg);
    const QString type = selectedElement >= 0 ? project->elements[selectedElement].typeName : selectedType;
    auto* title = new QLabel(selectedElement >= 0 ? QStringLiteral("(单)%1").arg(project->elements[selectedElement].name) : type, dlg);
    title->setStyleSheet(QStringLiteral("QLabel {font-size:16pt; font-weight:bold; padding:3px 0;}"));
    QFont titleFont = title->font(); titleFont.setPointSize(14); titleFont.setBold(true); title->setFont(titleFont);
    layout->addWidget(title);
    auto* all = new QCheckBox(QStringLiteral("选择全部零部件"), dlg); layout->addWidget(all);
    auto* grid = new QGridLayout; grid->setSpacing(1); layout->addLayout(grid);
    QList<QCheckBox*> allBoxes;
    int row = 0;
    for (const auto& group : partcatalog::groups(type)) {
        auto* groupBox = new QCheckBox(group.first, dlg);
        groupBox->setStyleSheet(QStringLiteral("QCheckBox {background:#fff8d9; color:#222; padding:3px;}"));
        grid->addWidget(groupBox, row, 0);
        QList<QCheckBox*> children; int col = 1;
        for (const auto& name : group.second) {
            auto* box = new QCheckBox(name, dlg); box->setProperty("partName", name);
            box->setStyleSheet(QStringLiteral("QCheckBox {border:1px solid #d7d7d7; padding:3px;}"));
            grid->addWidget(box, row, col++); children << box; allBoxes << box;
        }
        connect(groupBox, &QCheckBox::toggled, dlg, [children](bool on) { for (auto* box : children) box->setChecked(on); });
        ++row;
    }
    connect(all, &QCheckBox::toggled, dlg, [allBoxes](bool on) { for (auto* box : allBoxes) box->setChecked(on); });
    if (!allBoxes.isEmpty()) allBoxes.first()->setChecked(true);
    auto* source = new QLabel(partcatalog::verified(type)
        ? QStringLiteral("已核对目录：%1 组 %2 项；新零件防火时限 %3，可按设计修改。")
            .arg(partcatalog::groups(type).size()).arg(partcatalog::names(type).size()).arg(partcatalog::fireLimit(type))
        : QStringLiteral("可编辑工程预设；本类型零件目录尚未完成实机核对。"), dlg);
    source->setWordWrap(true); layout->addWidget(source);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dlg);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("确定"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, dlg, [this, dlg, allBoxes]() {
        QStringList names; for (auto* box : allBoxes) if (box->isChecked()) names << box->text();
        if (names.isEmpty()) return;
        appendParts(names); dlg->accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, dlg, &QDialog::reject);
    return dlg;
}

void TableInputPanel::addPart() {
    if (selectedElement < 0 || selectedElement >= project->elements.size()) {
        emit statusMessage(QStringLiteral("请先新建或选择一个构件。")); return;
    }
    QScopedPointer<QDialog> dialog(createPartDialog()); dialog->exec();
}

void TableInputPanel::addCategoryType() {
    bool ok = false;
    QString type = QInputDialog::getText(this, QStringLiteral("新建构件类型"),
        QStringLiteral("在“%1”下添加类型").arg(selectedCategory), QLineEdit::Normal, {}, &ok).trimmed();
    if (!ok || type.isEmpty()) return;
    if (!project->customTypes[selectedCategory].contains(type)) project->customTypes[selectedCategory] << type;
    selectedType = type; reload(); emit dataChanged();
}
void TableInputPanel::copyElement() {
    if (selectedElement < 0 || selectedElement >= project->elements.size()) return;
    Element copy = project->elements[selectedElement];
    for(auto& part:copy.parts)part.id=QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString base = copy.name + QStringLiteral("-副本"); copy.name = base;
    int suffix = 2;
    auto exists = [&]() { for (const auto& el : project->elements) if (el.floor == copy.floor && el.typeName == copy.typeName && el.name == copy.name) return true; return false; };
    while (exists()) copy.name = base + QString::number(suffix++);
    project->elements << copy; selectElement(project->elements.size() - 1); emit dataChanged();
}
void TableInputPanel::deleteElement() {
    if (selectedElement < 0 || selectedElement >= project->elements.size()) return;
    if (QMessageBox::question(this, QStringLiteral("删除构件"),
        QStringLiteral("删除构件“%1”及其全部零件？").arg(project->elements[selectedElement].name)) != QMessageBox::Yes) return;
    project->elements.removeAt(selectedElement); collapsedElements.clear(); selectedElement = -1; reload(); emit dataChanged();
}
void TableInputPanel::renameElement() {
    if (selectedElement < 0 || selectedElement >= project->elements.size()) return;
    syncTreeSelection();
    if (auto* it = elements->currentItem()) { elements->setFocus(); elements->editItem(it, 0); }
}

void TableInputPanel::editCurrent() {
    if (currentPartIndex() >= 0) {
        table->setCurrentCell(table->currentRow(), PartName); table->editItem(table->currentItem());
    } else renameElement();
}
void TableInputPanel::deleteSelection() {
    if (table->hasFocus() && currentPartIndex() >= 0) deleteParts(); else deleteElement();
}
void TableInputPanel::deleteParts() {
    QMap<int, QSet<int>> selected;
    for (const auto* it : table->selectedItems()) {
        const auto ref = rowRefs.value(it->row());
        if (ref.part >= 0) selected[ref.element].insert(ref.part);
    }
    if (selected.isEmpty()) return;
    for (auto it = selected.cbegin(); it != selected.cend(); ++it) {
        QList<int> indices = it.value().values(); std::sort(indices.begin(), indices.end(), std::greater<int>());
        for (int index : indices) project->elements[it.key()].parts.removeAt(index);
    }
    refreshRows(); emit dataChanged();
}

void TableInputPanel::movePart(int delta) {
    if (selectedElement < 0 || selectedElement >= project->elements.size()) return;
    auto& parts = project->elements[selectedElement].parts;
    const int index = currentPartIndex(), target = index + delta;
    if (index < 0 || index >= parts.size() || target < 0 || target >= parts.size()) return;
    parts.move(index, target); refreshRows(); table->setCurrentCell(rowFor(selectedElement, target), PartName); emit dataChanged();
}

void TableInputPanel::copyFloor() {
    if (selectedElement < 0 || selectedElement >= project->elements.size()) {
        emit statusMessage(QStringLiteral("请先选择要复制的构件。")); return;
    }
    QDialog dlg(this); dlg.setWindowTitle(QStringLiteral("层间复制")); auto* form = new QFormLayout(&dlg);
    auto* target = new QComboBox(&dlg); target->setEditable(true);
    for (const auto& floor : project->floors) if (floor != floorCombo->currentText()) target->addItem(floor);
    form->addRow(QStringLiteral("目标楼层"), target);
    auto* hint = new QLabel(QStringLiteral("复制当前选中构件及其全部零件；可输入新楼层。"), &dlg); form->addRow(hint);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg); form->addRow(buttons);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("确定"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    connect(buttons, &QDialogButtonBox::accepted, &dlg, [&]() {
        const QString floor = target->currentText().trimmed();
        if (!floor.isEmpty() && floor != floorCombo->currentText()) dlg.accept();
        else hint->setText(QStringLiteral("请输入与当前楼层不同的目标楼层。"));
    });
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted) return;
    Element copy = project->elements[selectedElement]; copy.floor = target->currentText().trimmed();
    for(auto& part:copy.parts)part.id=QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (!project->floors.contains(copy.floor)) project->floors << copy.floor;
    QString base = copy.name; int suffix = 1;
    auto exists = [&]() { for (const auto& el : project->elements) if (el.floor == copy.floor && el.typeName == copy.typeName && el.name == copy.name) return true; return false; };
    while (exists()) copy.name = base + QStringLiteral("-副本%1").arg(suffix++);
    project->elements << copy;
    { QSignalBlocker block(floorCombo);
      if (floorCombo->findText(copy.floor) < 0) floorCombo->addItem(copy.floor);
      floorCombo->setCurrentText(copy.floor); }
    selectElement(project->elements.size() - 1); emit dataChanged();
}

// Interaction checks use a separate project and disable persistent settings.

bool TableInputPanel::runInteractionChecks(QString* error) {
    int assertions = 0;
    auto check = [&](bool condition, const QString& what) {
        ++assertions;
        if (!condition) { if (error) *error = what; std::fprintf(stderr, "input assertion %d FAILED: %s\n", assertions, what.toUtf8().constData()); }
        return condition;
    };
    double length = 0;
    if (!check(evaluateLength(QStringLiteral(" -(2-5)*1000 "), length) && length == 3000, QStringLiteral("unary arithmetic"))) return false;
    if (!check(!evaluateLength(QStringLiteral("1/0"), length) && !evaluateLength(QStringLiteral("-2"), length) &&
        !evaluateLength(QStringLiteral("L-20"), length) && !evaluateLength(QStringLiteral("2**3"), length) &&
        !evaluateLength(QString(40, '(') + QStringLiteral("1") + QString(40, ')'), length), QStringLiteral("invalid arithmetic rejected"))) return false;
    prepareReferencePreview();
    if (!check(categoryCount() == 6, QStringLiteral("six categories retained"))) return false;
    if (!check(partTypes().size() == 28 && partcatalog::groups(QStringLiteral("钢柱")).size() == 6 &&
        partTypes().contains(QStringLiteral("柱身隔板")) && partTypes().contains(QStringLiteral("地脚锚栓")),
        QStringLiteral("column screenshot catalogue six groups 28 parts"))) return false;
    if (!check(partTypesFor(QStringLiteral("钢梁")).size() == 21 &&
        partTypesFor(QStringLiteral("钢楼梯")).contains(QStringLiteral("踏步板")), QStringLiteral("editable supplementary presets"))) return false;
    if (!check(project->elements.size() == 5 && table->rowCount() == 15 &&
        visibleElements.size() == 5, QStringLiteral("five component blocks share one table"))) return false;
    if (!check(elements->topLevelItemCount() == 1 && elements->topLevelItem(0)->text(0) == QStringLiteral("钢柱") &&
        elements->topLevelItem(0)->childCount() == 5, QStringLiteral("real parent child component tree"))) return false;
    for (int i = 0; i < 5; ++i) {
        const auto& el = project->elements[i];
        if (!check(el.name == QStringLiteral("GZ-%1").arg(i+1) &&
            table->item(rowFor(i), ElementName)->text() == QStringLiteral("(单)GZ-%1").arg(i+1) &&
            el.parts.size() == 2 && el.parts[0].spec.isEmpty() && el.parts[0].lengthMm == 0 &&
            el.parts[0].material == QStringLiteral("Q355B") && el.parts[1].partType.isEmpty(),
            QStringLiteral("sequential naming and unsized draft %1").arg(i))) return false;
    }
    bool modalOpened = false;
    QTimer::singleShot(0, this, [&]() {
        if (auto* dlg = qobject_cast<QDialog*>(QApplication::activeModalWidget())) { modalOpened = true; dlg->reject(); }
    });
    createElement(); QCoreApplication::processEvents();
    if (!check(!modalOpened && project->elements.last().name == QStringLiteral("GZ-6") &&
        selectedElement == 5 && table->currentRow() == rowFor(5), QStringLiteral("new component synchronous no popup"))) return false;
    table->setCurrentCell(rowFor(1, 0), Spec);
    if (!check(selectedElement == 1 && currentPartIndex() == 0 &&
        elements->currentItem()->data(0, Qt::UserRole).toInt() == 1, QStringLiteral("table selection synchronizes tree"))) return false;
    elements->setCurrentItem(elements->topLevelItem(0)->child(3));
    if (!check(selectedElement == 3 && table->currentRow() == rowFor(3), QStringLiteral("tree selection synchronizes table"))) return false;
    auto toggle = [&](int e) { QMetaObject::invokeMethod(table, "cellClicked", Q_ARG(int, rowFor(e)), Q_ARG(int, int(ElementType))); };
    toggle(0);
    if (!check(table->isRowHidden(rowFor(0, 0)) && !table->isRowHidden(rowFor(1, 0)), QStringLiteral("independent first collapse"))) return false;
    toggle(2);
    if (!check(table->isRowHidden(rowFor(0, 1)) && table->isRowHidden(rowFor(2, 1)), QStringLiteral("multiple collapsed blocks"))) return false;
    toggle(0); reload();
    if (!check(!table->isRowHidden(rowFor(0, 0)) && table->isRowHidden(rowFor(2, 0)), QStringLiteral("collapse state retained on reload"))) return false;
    toggle(2);
    for (int e : {0, 2, 5}) {
        const auto other = project->elements[(e+1)%6].toJson();
        table->setCurrentCell(rowFor(e, 0), Spec);
        table->item(rowFor(e, 0), Spec)->setText(QStringLiteral("H200x100x6x8"));
        table->item(rowFor(e, 0), Formula)->setText(QStringLiteral("(6000-2*20)/2"));
        table->item(rowFor(e, 0), Count)->setText(QStringLiteral("3"));
        if (!check(project->elements[e].parts[0].lengthMm == 2980 && project->elements[e].parts[0].count == 3 &&
            project->elements[(e+1)%6].toJson() == other, QStringLiteral("mapped edit first middle last %1").arg(e))) return false;
    }
    const auto oldPart = project->elements[5].parts[0].toJson();
    table->item(rowFor(5, 0), Formula)->setText(QStringLiteral("1/0"));
    table->item(rowFor(5, 0), Count)->setText(QStringLiteral("-1"));
    if (!check(project->elements[5].parts[0].toJson() == oldPart, QStringLiteral("invalid edits roll back"))) return false;
    const auto beforeQty = rowCalc(project->elements[2], project->elements[2].parts[0]).totalWeight;
    table->item(rowFor(2), Count)->setText(QStringLiteral("2"));
    if (!check(std::abs(rowCalc(project->elements[2], project->elements[2].parts[0]).totalWeight - beforeQty*2) < 1e-6 &&
        project->elements[1].count == 1, QStringLiteral("parent quantity and untouched neighbour"))) return false;
    table->item(rowFor(2), ElementName)->setText(QStringLiteral("(单)中间构件"));
    if (!check(project->elements[2].name == QStringLiteral("中间构件"), QStringLiteral("inline table rename prefix normalization"))) return false;
    table->item(rowFor(2), ElementName)->setText(QStringLiteral("GZ-1"));
    if (!check(project->elements[2].name == QStringLiteral("中间构件"), QStringLiteral("duplicate component name rejected"))) return false;
    elements->setCurrentItem(elements->topLevelItem(0)->child(2));
    auto* treeItem = elements->currentItem();
    treeItem->setText(0, QStringLiteral("树内改名"));
    QCoreApplication::processEvents();
    if (!check(project->elements[2].name == QStringLiteral("树内改名") &&
        table->item(rowFor(2), ElementName)->text() == QStringLiteral("(单)树内改名"), QStringLiteral("inline tree rename updates table"))) return false;
    {
        QScopedPointer<QDialog> dlg(createColumnDialog());
        const auto boxes = dlg->findChildren<QCheckBox*>();
        if (!check(boxes.size() == 14, QStringLiteral("column options retained"))) return false;
        for (auto* box : boxes) if (box->text() == headers[Rust]) box->setChecked(true);
        dlg->reject();
        if (!check(!columnVisible(headers[Rust]), QStringLiteral("cancel column settings"))) return false;
    }
    {
        QScopedPointer<QDialog> dlg(createColumnDialog());
        for (auto* box : dlg->findChildren<QCheckBox*>()) if (box->text() == headers[Rust]) box->setChecked(true);
        dlg->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
        if (!check(columnVisible(headers[Rust]) && !table->isColumnHidden(Rust), QStringLiteral("apply column settings"))) return false;
    }
    QString why;
    auto beforePaste = project->toJson();
    if (!check(!applyMatrix(QStringLiteral("X\nY\nZ"), rowFor(0, 0), Spec, &why) &&
        project->toJson() == beforePaste, QStringLiteral("cross component paste rejected atomically"))) return false;
    if (!check(!applyMatrix(QStringLiteral("X"), rowFor(0), Spec, &why), QStringLiteral("parent row paste rejected"))) return false;
    if (!check(!applyMatrix(QStringLiteral("10"), rowFor(0, 0), UnitWeight, &why) &&
        project->toJson() == beforePaste, QStringLiteral("computed columns read only"))) return false;
    if (!check(!applyMatrix(QStringLiteral("2\n-1"), rowFor(0, 0), Count, &why) &&
        project->toJson() == beforePaste, QStringLiteral("failed paste no partial write"))) return false;
    if (!check(applyMatrix(QStringLiteral("FB100x10\nROD20"), rowFor(1, 0), Spec, &why, false) &&
        project->toJson() == beforePaste, QStringLiteral("paste dry run no write"))) return false;
    if (!check(applyMatrix(QStringLiteral("FB100x10\nROD20"), rowFor(1, 0), Spec, &why) &&
        project->elements[1].parts[0].spec == QStringLiteral("FB100x10") &&
        project->elements[1].parts[1].spec == QStringLiteral("ROD20") &&
        project->elements[0].toJson() == beforePaste["elements"].toArray()[0].toObject(), QStringLiteral("paste mapped to correct component"))) return false;
    if (!check(applyMatrix(QStringLiteral("\"多行\n名称\""), rowFor(1, 0), PartName, &why) &&
        project->elements[1].parts[0].name == QStringLiteral("多行\n名称"), QStringLiteral("quoted multiline TSV preserved"))) return false;
    if (!check(applyMatrix(QStringLiteral("FB100x10\nROD20\nFB200x10"), rowFor(5, 0), Spec, &why) &&
        project->elements[5].parts.size() == 3 && project->elements[5].parts[2].lengthMm == 0,
        QStringLiteral("last block paste extends with unsized drafts"))) return false;
    table->setCurrentCell(rowFor(0), ElementName);
    const int partBefore = project->elements[0].parts.size();
    {
        QScopedPointer<QDialog> dlg(createPartDialog());
        int choices = 0;
        for (auto* box : dlg->findChildren<QCheckBox*>()) if (box->property("partName").isValid()) {
            ++choices; box->setChecked(box->text() == QStringLiteral("柱脚底板") || box->text() == QStringLiteral("抗剪键"));
        }
        if (!check(choices == 28, QStringLiteral("all screenshot parts present in selector"))) return false;
        dlg->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
    }
    if (!check(project->elements[0].parts.size() == partBefore + 2 &&
        project->elements[0].parts.last().partType == QStringLiteral("抗剪键") &&
        project->elements[0].parts.last().spec.isEmpty(), QStringLiteral("batch catalogue add no fabricated geometry"))) return false;
    table->setCurrentCell(rowFor(0, partBefore+1), PartName); movePart(-1);
    if (!check(project->elements[0].parts[partBefore].partType == QStringLiteral("抗剪键") &&
        currentPartIndex() == partBefore, QStringLiteral("move within selected component block"))) return false;
    table->clearSelection();
    table->item(rowFor(0, partBefore), PartName)->setSelected(true);
    table->item(rowFor(1, 1), PartName)->setSelected(true);
    deleteParts();
    if (!check(project->elements[0].parts.size() == partBefore+1 &&
        project->elements[1].parts.size() == 1, QStringLiteral("delete multiple block parts without deleting parents"))) return false;
    table->setCurrentCell(rowFor(0, 0), PartName); movePart(-1);
    if (!check(currentPartIndex() == 0 && project->elements[0].parts[0].partType == QStringLiteral("柱身型钢"),
        QStringLiteral("move cannot cross component parent"))) return false;
    table->setCurrentCell(rowFor(0), ElementName);
    const auto original = project->elements[0].toJson(); const int n = project->elements.size();
    copyElement();
    if (!check(project->elements.size() == n+1 && selectedElement == n &&
        project->elements.last().parts.size() == project->elements[0].parts.size(), QStringLiteral("copy component includes parts"))) return false;
    project->elements.last().parts[0].material = QStringLiteral("副本材质");
    if (!check(project->elements[0].toJson() == original, QStringLiteral("copied component independent"))) return false;
    refreshRows();
    {
        const auto restored = Project::fromJson(project->toJson());
        if (!check(restored.toJson() == project->toJson(), QStringLiteral("multi component full JSON round trip"))) return false;
    }
    Element many; many.count = 1000000; Part manyPart; manyPart.count = 1000000;
    if (!check(rowCalc(many, manyPart).count == 1000000000000LL, QStringLiteral("64 bit quantity retained"))) return false;
    const int beforeFloorCopy = project->elements.size();
    QTimer::singleShot(0, this, []() {
        auto* dlg = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dlg) return;
        dlg->findChild<QComboBox*>()->setCurrentText(QStringLiteral("自检三层"));
        dlg->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
    });
    copyFloor();
    if (!check(project->elements.size() == beforeFloorCopy+1 && project->elements.last().floor == QStringLiteral("自检三层") &&
        project->floors.contains(QStringLiteral("自检三层")) && visibleElements.size() == 1, QStringLiteral("floor copy and filtering"))) return false;
    QTimer::singleShot(0, this, []() {
        if (auto* dlg = qobject_cast<QInputDialog*>(QApplication::activeModalWidget())) {
            dlg->setTextValue(QStringLiteral("自检类型")); dlg->accept();
        }
    });
    addCategoryType();
    if (!check(project->customTypes.value(selectedCategory).contains(QStringLiteral("自检类型")) &&
        visibleElements.isEmpty() && table->rowCount() == 0, QStringLiteral("custom category empty state"))) return false;
    const int beforeNew = project->elements.size(); createElement();
    if (!check(project->elements.size() == beforeNew+1 && selectedElement == beforeNew &&
        project->elements.last().typeName == QStringLiteral("自检类型") && project->elements.last().count == 1 &&
        !QApplication::activeModalWidget(), QStringLiteral("custom component also no popup"))) return false;
    table->item(rowFor(selectedElement), ElementName)->setText(QStringLiteral("自检重命名"));
    if (!check(project->elements.last().name == QStringLiteral("自检重命名"), QStringLiteral("inline custom component rename"))) return false;
    const auto restored = Project::fromJson(project->toJson());
    if (!check(restored.customTypes == project->customTypes && restored.elements.last().category == selectedCategory,
        QStringLiteral("custom type serialization retained"))) return false;
    QTimer::singleShot(0, this, []() {
        if (auto* dlg = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) dlg->button(QMessageBox::Yes)->click();
    });
    deleteElement();
    if (!check(project->elements.size() == beforeNew && selectedElement == -1 && table->rowCount() == 0,
        QStringLiteral("delete component confirmation retained"))) return false;
    selectedType = QStringLiteral("钢柱"); selectedCategory = QStringLiteral("主构件");
    floorCombo->setCurrentText(QStringLiteral("首层")); reload();
    // A gap must not create a duplicate of a later existing sequence number.
    project->elements.removeAt(1); collapsedElements.clear(); selectedElement = -1; reload(); createElement();
    if (!check(project->elements.last().name == QStringLiteral("GZ-7"), QStringLiteral("sequence after deleting middle component"))) return false;
    categories->setCurrentItem(categories->topLevelItem(0)->child(1));
    createElement();
    const int beam = selectedElement;
    categories->setCurrentItem(categories->topLevelItem(0));
    if (!check(rowFor(beam) >= 0 && visibleElements.contains(0) &&
        elements->topLevelItemCount() == 2, QStringLiteral("category root shows multiple type trees and blocks"))) return false;
    {
        QScopedPointer<QWidget> editor(table->itemDelegateForColumn(PartType)->createEditor(table,
            QStyleOptionViewItem(), table->model()->index(rowFor(beam, 0), PartType)));
        auto* combo = qobject_cast<QComboBox*>(editor.data());
        if (!check(combo && combo->count() == 21 && combo->findText(QStringLiteral("梁身型钢")) >= 0 &&
            combo->findText(QStringLiteral("柱身型钢")) < 0, QStringLiteral("mixed type dropdown uses row context"))) return false;
    }
    // Isolated regression fixture: do not disturb the preceding multi-block indices.
    Project catalogueProject;
    TableInputPanel audit(&catalogueProject, nullptr, false);
    const QList<int> expectedCounts = {3, 11, 11, 1, 8, 5};
    if (!check(audit.categories->topLevelItemCount() == 6,
        QStringLiteral("verified tree has six category roots"))) return false;
    for (int groupIndex = 0; groupIndex < groups.size(); ++groupIndex) {
        auto* root = audit.categories->topLevelItem(groupIndex);
        if (!check(root->text(0) == groups[groupIndex] &&
            root->childCount() == expectedCounts[groupIndex],
            QStringLiteral("verified category order and count %1").arg(groupIndex))) return false;
        const QStringList children = types[groupIndex];
        for (int typeIndex = 0; typeIndex < children.size(); ++typeIndex) {
            // reload recreates tree items; never reuse an invalidated root pointer.
            audit.categories->setCurrentItem(audit.categories->topLevelItem(groupIndex)->child(typeIndex));
            const QString type = children[typeIndex];
            audit.createElement();
            const auto& created = catalogueProject.elements.last();
            if (!check(created.typeName == type && created.category == groups[groupIndex] &&
                created.name == partcatalog::prefix(type) + QStringLiteral("-1") &&
                created.parts.size() == 2 &&
                created.parts[0].partType == partcatalog::names(type).first() &&
                created.parts[1].partType.isEmpty() &&
                created.parts[0].spec.isEmpty() && created.parts[1].spec.isEmpty() &&
                created.parts[0].lengthMm == 0 && created.parts[1].lengthMm == 0 &&
                created.parts[0].count == 1 && created.parts[1].count == 1 &&
                created.parts[0].material == QStringLiteral("Q355B") &&
                created.parts[0].fireLimit == partcatalog::fireLimit(type) &&
                created.parts[1].fireLimit == partcatalog::fireLimit(type) &&
                !QApplication::activeModalWidget(),
                QStringLiteral("all 39 reference/compatible types create no popup: %1").arg(type))) return false;
        }
    }
    if (!check(catalogueProject.elements.size() == 39 &&
        partcatalog::verifiedCatalogue().size() == 11,
        QStringLiteral("34 GJG types plus five retained compatible types"))) return false;
    for (auto it = partcatalog::verifiedCatalogue().cbegin();
         it != partcatalog::verifiedCatalogue().cend(); ++it) {
        const QString type = it.key();
        int index = -1;
        for (int i = 0; i < catalogueProject.elements.size(); ++i)
            if (catalogueProject.elements[i].typeName == type) { index = i; break; }
        if (!check(index >= 0, QStringLiteral("verified type can be located: %1").arg(type))) return false;
        audit.selectElement(index);
        QStringList dialogNames;
        {
            QScopedPointer<QDialog> dialog(audit.createPartDialog());
            for (auto* box : dialog->findChildren<QCheckBox*>()) {
                if (!box->property("partName").isValid()) continue;
                dialogNames << box->property("partName").toString();
                box->setChecked(true);
            }
            if (!check(dialogNames == partcatalog::names(type),
                QStringLiteral("catalogue dialog order/content: %1").arg(type))) return false;
            dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
        }
        auto& parts = catalogueProject.elements[index].parts;
        if (!check(parts.size() == 2 + dialogNames.size() &&
            parts.last().partType == dialogNames.last() &&
            parts.last().fireLimit == it->fireLimit && parts.last().spec.isEmpty() &&
            parts.last().lengthMm == 0,
            QStringLiteral("catalogue batch append applies typed defaults: %1").arg(type))) return false;
        audit.appendParts({QString()});
        if (!check(parts.last().partType.isEmpty() && parts.last().fireLimit == it->fireLimit &&
            parts.last().material == QStringLiteral("Q355B"),
            QStringLiteral("blank add applies typed defaults: %1").arg(type))) return false;
        // A type-filtered view has this element as its final visible block.
        const int end = parts.size() - 1;
        if (!check(audit.applyMatrix(QStringLiteral("FB100x10\nFB120x10"),
            audit.rowFor(index, end), Spec, &why) &&
            parts.size() == end + 2 && parts.last().fireLimit == it->fireLimit &&
            parts.last().spec == QStringLiteral("FB120x10"),
            QStringLiteral("paste extension applies typed defaults: %1").arg(type))) return false;
        audit.table->item(audit.rowFor(index, 0), FireLimit)->setText(QStringLiteral("0.75h"));
        audit.reload();
        if (!check(catalogueProject.elements[index].parts[0].fireLimit == QStringLiteral("0.75h") &&
            Project::fromJson(catalogueProject.toJson()).toJson() == catalogueProject.toJson(),
            QStringLiteral("user fire override and all-type serialization retained: %1").arg(type))) return false;
    }
    Part legacyBlank;
    legacyBlank.fireLimit.clear();
    if (!check(Part::fromJson(legacyBlank.toJson()).fireLimit.isEmpty() &&
        partcatalog::fireLimit(QStringLiteral("钢楼梯")).isEmpty(),
        QStringLiteral("old blanks and unaudited defaults never backfilled"))) return false;
    Element legacyFastener;
    legacyFastener.typeName = QStringLiteral("螺栓");
    legacyFastener.category = QStringLiteral("配件");
    legacyFastener.name = QStringLiteral("旧配件");
    catalogueProject.elements << legacyFastener;
    audit.selectElement(catalogueProject.elements.size() - 1);
    if (!check(audit.selectedCategory == QStringLiteral("配件") &&
        catalogueProject.elements.last().category == QStringLiteral("配件") &&
        catalogueProject.elements.last().parts.isEmpty(),
        QStringLiteral("saved legacy categories and empty parts preserved"))) return false;
    std::printf("input: assertions=%d passed\n", assertions);
    return true;
}

bool TableInputPanel::applyMatrix(const QString& text, int startRow, int startColumn,
                                 QString* error, bool commit) {
    auto fail = [&](const QString& msg) { if(error) *error = msg; return false; };
    if (selectedElement < 0 || selectedElement >= project->elements.size())
        return fail(QStringLiteral("请先选择构件。"));
    if (startRow < 0 || startRow >= rowRefs.size() || rowRefs[startRow].part < 0 ||
        startColumn < PartType || startColumn >= ColumnCount)
        return fail(QStringLiteral("请从零件子行的可编辑列开始粘贴。"));
    QVector<QStringList> matrix;
    if (!parseTsv(text, matrix, error)) return false;
    QVector<int> columns;
    for (int c = startColumn; c < ColumnCount; ++c)
        if (!table->isColumnHidden(c)) columns << c;
    const auto ref = rowRefs[startRow];
    auto next = project->elements[ref.element].parts;
    if (ref.part + matrix.size() > next.size() && visibleElements.last() != ref.element)
        return fail(QStringLiteral("粘贴将跨过下一个构件父行；请先添加足够的零件行或分构件粘贴。"));
    for (int r = 0; r < matrix.size(); ++r) {
        if (matrix[r].size() > columns.size()) return fail(QStringLiteral("粘贴列数超过剩余可见列。"));
        const int row = ref.part + r;
        while (next.size() <= row) next.append(partcatalog::blank({}, project->elements[ref.element].typeName));
        // Apply length before formula so an explicit formula takes precedence.
        QList<int> order;
        for (int c = 0; c < matrix[r].size(); ++c)
            if (columns[c] != Formula) order << c;
        for (int c = 0; c < matrix[r].size(); ++c)
            if (columns[c] == Formula) order << c;
        for (int c : order) {
            const int column = columns[c];
            if (column >= UnitWeight && column <= TotalArea) {
                if (!matrix[r][c].trimmed().isEmpty())
                    return fail(QStringLiteral("第 %1 行包含只读计算列；请只粘贴录入列。").arg(r+1));
                continue;
            }
            QString why;
            if (!editPartCell(next[row], column, matrix[r][c].trimmed(), why))
                return fail(QStringLiteral("第 %1 行「%2」无效：%3").arg(r+1).arg(headers[column], why));
        }
    }
    if (commit) {
        project->elements[ref.element].parts = next;
        refreshRows(); emit dataChanged();
    }
    return true;
}

void TableInputPanel::pasteClipboard() {
    const QString text = QApplication::clipboard()->text();
    const int row = table->currentRow(), col = table->currentColumn();
    QString error;
    if (!applyMatrix(text, row, col, &error, false)) {
        QMessageBox::warning(this, QStringLiteral("粘贴未应用"), error); return;
    }
    QVector<QStringList> matrix;
    if (!parseTsv(text, matrix, &error)) return;
    QDialog preview(this);
    preview.setWindowTitle(QStringLiteral("Excel 粘贴预览")); preview.resize(850, 420);
    QVBoxLayout layout(&preview);
    layout.addWidget(new QLabel(QStringLiteral("共 %1 行；确认后一次写入，错误时整批不应用。").arg(matrix.size())));
    auto* grid = new QTableWidget(qMin(100, int(matrix.size())), matrix.first().size(), &preview);
    grid->setEditTriggers(QAbstractItemView::NoEditTriggers);
    for (int r=0;r<grid->rowCount();++r)
        for(int c=0;c<matrix[r].size();++c) grid->setItem(r,c,new QTableWidgetItem(matrix[r][c]));
    layout.addWidget(grid);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel, &preview);
    layout.addWidget(buttons);
    connect(buttons,&QDialogButtonBox::accepted,&preview,&QDialog::accept);
    connect(buttons,&QDialogButtonBox::rejected,&preview,&QDialog::reject);
    if (preview.exec()==QDialog::Accepted && !applyMatrix(text,row,col,&error,true))
        QMessageBox::warning(this,QStringLiteral("粘贴未应用"),error);
}

void TableInputPanel::prepareReferencePreview() {
    project->elements.clear(); project->floors = {QStringLiteral("首层")}; project->customTypes.clear();
    selectedElement = -1; selectedCategory = QStringLiteral("主构件"); selectedType = QStringLiteral("钢柱");
    collapsedElements.clear(); reload();
    for (int i = 0; i < 5; ++i) createElement();
    table->setCurrentCell(rowFor(selectedElement), ElementName);
}
