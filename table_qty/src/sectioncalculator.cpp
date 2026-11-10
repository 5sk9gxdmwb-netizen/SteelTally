#include "sectioncalculator.h"
#include "sectionlibrary.h"
#include "sectiondrawing.h"

SectionCalculator::SectionCalculator(QWidget* parent):QWidget(parent) {
    setObjectName("sectionCalculator");
    setAttribute(Qt::WA_StyledBackground,true);
    setMinimumWidth(650);
    auto* root=new QVBoxLayout(this);root->setContentsMargins(8,4,8,4);
    auto* heading=new QLabel(QStringLiteral("型材查询与实时计算"));root->addWidget(heading);
    target=new QLabel;target->setWordWrap(true);root->addWidget(target);
    mode=new QComboBox;mode->setObjectName("calculationMode");
    mode->addItems({QStringLiteral("标准规格 / 原有型材库"),QStringLiteral("非标尺寸 / 手动计算")});root->addWidget(mode);
    auto* split=new QSplitter(Qt::Horizontal);split->setObjectName("sectionSplitter");split->setChildrenCollapsible(false);root->addWidget(split,1);
    auto* scroll=new QScrollArea;scroll->setWidgetResizable(true);scroll->setFrameShape(QFrame::NoFrame);scroll->setMinimumWidth(300);scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);split->addWidget(scroll);
    auto* left=new QWidget;left->setObjectName("sectionParameterPane");left->setAttribute(Qt::WA_StyledBackground,true);scroll->setWidget(left);auto* controls=new QVBoxLayout(left);controls->setContentsMargins(0,0,4,0);
    auto* right=new QWidget;split->addWidget(right);auto* results=new QVBoxLayout(right);results->setContentsMargins(4,0,0,0);
    drawing=new SectionDrawing;results->addWidget(drawing,1);split->setStretchFactor(1,1);split->setSizes({290,350});
    types=new QTreeWidget;types->setObjectName("sectionTypeTree");types->setHeaderHidden(true);types->setMinimumHeight(130);types->setMaximumHeight(230);controls->addWidget(types);
    standardPage=new QWidget;auto* sl=new QVBoxLayout(standardPage);sl->setContentsMargins(0,0,0,0);
    family=new QComboBox;family->setObjectName("sectionFamily");family->addItem(QStringLiteral("全部品类"));
    standard=new QComboBox;standard->setObjectName("sectionStandard");standard->addItem(QStringLiteral("全部版本及来源"));
    QSet<QString> families,standards;
    for(const auto& r:sectionlib::records()) {families.insert(r["family"].toString());standards.insert(r["standard"].toString());}
    auto fs=families.values(),ss=standards.values();fs.sort();ss.sort();family->addItems(fs);standard->addItems(ss);
    sl->addWidget(family);sl->addWidget(standard);
    search=new QLineEdit;search->setObjectName("sectionSearch");search->setPlaceholderText(QStringLiteral("搜索规格，例如 I10、HN400、PIPE"));sl->addWidget(search);
    list=new QListWidget;list->setObjectName("sectionResults");list->setMinimumHeight(110);sl->addWidget(list);
    controls->addWidget(standardPage,1);
    customPage=new QWidget;dimensionForm=new QFormLayout(customPage);auto* form=dimensionForm;form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);form->setRowWrapPolicy(QFormLayout::WrapLongRows);form->setContentsMargins(0,0,0,0);
    shape=new QComboBox;shape->setObjectName("sectionShape");shape->addItems({QStringLiteral("焊接H型截面"),QStringLiteral("圆管"),
        QStringLiteral("方矩管 / 箱形"),QStringLiteral("圆钢"),QStringLiteral("角钢（等/不等边，尖角）"),QStringLiteral("钢板（双面大面）"),
        QStringLiteral("T形截面（焊接/切割）"),QStringLiteral("槽形截面（焊接）"),QStringLiteral("冷弯卷边C形"),QStringLiteral("冷弯卷边Z形"),
        QStringLiteral("实心矩形棒"),QStringLiteral("六角钢"),QStringLiteral("八角钢"),QStringLiteral("箱形（板厚不等）"),QStringLiteral("焊接H（上下翼缘宽不等）")});
    form->addRow(QStringLiteral("截面"),shape);
    auto spin=[&](const char* name,double value) {
        auto* v=new QDoubleSpinBox;v->setObjectName(name);v->setRange(0,1e6);v->setDecimals(3);v->setValue(value);
        v->setSuffix(QStringLiteral(" mm"));v->setKeyboardTracking(true);v->setMinimumWidth(110);return v;
    };
    h=spin("dimensionH",400);b=spin("dimensionB",200);tw=spin("dimensionTw",8);tf=spin("dimensionTf",13);radius=spin("dimensionR",0);extra=spin("dimensionC",50);
    form->addRow(QStringLiteral("高 / 直径 / 板宽 (mm)"),h);form->addRow(QStringLiteral("宽 / 另一肢 (mm)"),b);
    form->addRow(QStringLiteral("腹板 / 管壁 / 板厚 (mm)"),tw);form->addRow(QStringLiteral("翼缘厚度 (mm)"),tf);
    form->addRow(QStringLiteral("H根部 / 箱形外圆角 (mm)"),radius);form->addRow(QStringLiteral("附加尺寸 c (mm)"),extra);controls->addWidget(customPage);
    validation=new QLabel;validation->setObjectName("dimensionValidation");validation->setWordWrap(true);controls->addWidget(validation);
    auto* inputs=new QFormLayout;inputs->setRowWrapPolicy(QFormLayout::WrapLongRows);inputs->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    material=new QComboBox;material->setEditable(true);material->setObjectName("calculatorMaterial");material->addItems(sectionlib::materials());
    length=spin("calculatorLength",1000);length->setRange(0,1e9);
    width=spin("calculatorPlateWidth",1000);
    count=new QSpinBox;count->setObjectName("calculatorCount");count->setRange(1,1000000);
    inputs->addRow(QStringLiteral("材质"),material);inputs->addRow(QStringLiteral("长度 (mm)"),length);
    inputs->addRow(QStringLiteral("面积型材板宽 (mm)"),width);inputs->addRow(QStringLiteral("零件数量"),count);controls->addLayout(inputs);controls->addStretch();
    details=new QLabel;details->setWordWrap(true);details->setTextInteractionFlags(Qt::TextSelectableByMouse);results->addWidget(details);
    preview=new QLabel;preview->setObjectName("calculationPreview");preview->setWordWrap(true);results->addWidget(preview);
    clearManual=new QCheckBox(QStringLiteral("替换当前行的人工重量 / 面积"));results->addWidget(clearManual);
    live=new QCheckBox(QStringLiteral("采用后随输入实时更新当前行"));live->setObjectName("calculationLive");live->setChecked(false);results->addWidget(live);
    apply=new QPushButton(QStringLiteral("采用结果并开启实时联动"));apply->setObjectName("applyCalculation");results->addWidget(apply);
    auto* standardRoot=new QTreeWidgetItem(types,{QStringLiteral("标准型材库")});standardRoot->setData(0,Qt::UserRole,-1);
    for(int i=0;i<family->count();++i){auto* item=new QTreeWidgetItem(standardRoot,{family->itemText(i)});item->setData(0,Qt::UserRole,i);}
    auto* customRoot=new QTreeWidgetItem(types,{QStringLiteral("非标参数截面")});customRoot->setData(0,Qt::UserRole,-1);
    for(int i=0;i<shape->count();++i){auto* item=new QTreeWidgetItem(customRoot,{shape->itemText(i)});item->setData(0,Qt::UserRole,100+i);}
    types->expandAll();family->hide();
    connect(types,&QTreeWidget::currentItemChanged,this,[this](QTreeWidgetItem* item,QTreeWidgetItem*){
        if(!item || loading)return;const int key=item->data(0,Qt::UserRole).toInt();if(key<0)return;
        {QSignalBlocker m(mode),f(family),s(shape);mode->setCurrentIndex(key>=100?1:0);
         if(key>=100)shape->setCurrentIndex(key-100);else family->setCurrentIndex(key);}
        if(key<100)filter();else updatePreview();
    });
    connect(apply,&QPushButton::clicked,this,[this]{if(!bound)return;live->setChecked(true);submit();});
    connect(mode,&QComboBox::currentIndexChanged,this,[this] {updatePreview();});
    connect(family,&QComboBox::currentIndexChanged,this,[this] {filter();});
    connect(standard,&QComboBox::currentIndexChanged,this,[this] {filter();});
    connect(search,&QLineEdit::textChanged,this,[this] {filter();});
    connect(list,&QListWidget::currentRowChanged,this,[this](int) {
        if(loading)return;
        current=list->currentItem()?sectionlib::records().value(list->currentItem()->data(Qt::UserRole).toInt()):QJsonObject();
        updatePreview();
    });
    connect(shape,&QComboBox::currentIndexChanged,this,[this] {updatePreview();});
    for(auto* v:{h,b,tw,tf,radius,extra,length,width})
        connect(v,&QDoubleSpinBox::valueChanged,this,[this] {updatePreview();});
    connect(count,&QSpinBox::valueChanged,this,[this] {updatePreview();});
    connect(material,&QComboBox::currentTextChanged,this,[this] {updatePreview();});
    connect(clearManual,&QCheckBox::toggled,this,[this] {updatePreview();});
    for(auto* label:{heading,target,details,preview}) label->setSizePolicy(QSizePolicy::Preferred,QSizePolicy::Maximum);
    customPage->setSizePolicy(QSizePolicy::Preferred,QSizePolicy::Maximum);

    filter();bind(nullptr);
}
void SectionCalculator::filter() {
    if(loading)return;
    loading=true;list->clear();
    for(int i=0;i<sectionlib::records().size();++i) {
        const auto& r=sectionlib::records()[i];
        if(family->currentIndex()>0 && r["family"]!=family->currentText())continue;
        if(standard->currentIndex()>0 && r["standard"]!=standard->currentText())continue;
        if(!sectionlib::key(r["spec"].toString()).contains(sectionlib::key(search->text())))continue;
        auto* item=new QListWidgetItem(QString("%1  |  %2 %3  |  %4")
            .arg(r["spec"].toString()).arg(r["mass"].toDouble(),0,'f',3).arg(r["unit"].toString(),r["standard"].toString()),list);
        item->setData(Qt::UserRole,i);item->setToolTip(r["standard"].toString()+"\n"+r["source"].toString());
        if(current["id"]==r["id"])list->setCurrentItem(item);
    }
    // Keep the current coefficients if filtering hides the selected record.
    loading=false;updatePreview();
}
QJsonObject SectionCalculator::selectedRecord() const {
    auto r=mode->currentIndex()==0?current:sectionlib::formula(shape->currentIndex(),h->value(),b->value(),tw->value(),tf->value(),radius->value(),extra->value());
    r["width_mm"]=width->value();
    return r;
}
void SectionCalculator::updatePreview() {
    if(loading)return;
    standardPage->setVisible(mode->currentIndex()==0);customPage->setVisible(mode->currentIndex()==1);
    updateDimensions();
    const auto r=selectedRecord();const bool ok=sectionlib::valid(r);
    drawing->setRecord(r,mode->currentIndex()==0);
    const bool plate=r["unit"]=="kg/m2";width->setEnabled(plate);
    const double factor=length->value()/1000*(plate?width->value()/1000:1);
    const double weight=manualWeight&&!clearManual->isChecked()?lockedWeight:r["mass"].toDouble()*factor;
    const double area=manualArea&&!clearManual->isChecked()?lockedArea:r["surface"].toDouble()*factor;
    details->setText(ok ? QString("%1\n%2\n%3\n%4\n%5 %6；表面积系数 %7")
        .arg(r["spec"].toString(),r["standard"].toString(),r["source"].toString().replace(QStringLiteral("10⁻⁶"),QStringLiteral("10^-6")),r["surface_basis"].toString())
        .arg(r["mass"].toDouble(),0,'f',6).arg(r["unit"].toString()).arg(r["surface"].toDouble(),0,'f',6)
        :QStringLiteral("请选择规格并补齐有效尺寸。没有面积数据的条目不能自动回写。"));
    const qint64 n=qint64(count->value())*parentCount;
    preview->setText(ok ? QStringLiteral("单件 %1 kg / %2 m²\n合计 %3 件：%4 kg / %5 m²%6")
        .arg(weight,0,'f',3).arg(area,0,'f',3).arg(n).arg(weight*n,0,'f',3).arg(area*n,0,'f',3)
        .arg((manualWeight||manualArea)&&!clearManual->isChecked()?QStringLiteral("\n保留人工覆盖值；勾选“替换”后使用新结果"):QString())
        :QStringLiteral("结果待补充，不写入零值"));
    apply->setEnabled(bound && ok && (!plate||width->value()>0));
    if(live->isChecked() && apply->isEnabled())submit();
}
void SectionCalculator::submit() {
    auto r=selectedRecord();if(loading || !bound || !sectionlib::valid(r))return;
    if(r["unit"]=="kg/m2" && width->value()<=0)return;
    emit commitRequested(r,length->value(),count->value(),sectionlib::material(material->currentText()),clearManual->isChecked());
}
void SectionCalculator::bind(const Part* part,int elementCount) {
    loading=true;bound=part!=nullptr;parentCount=elementCount;
    if(!part){boundId.clear();live->setChecked(false);target->setText(QStringLiteral("请选择一行零件，再采用计算结果。"));}
    else {
        // Live mode survives an update of the same part, but never follows to another part.
        if(boundId!=part->id){live->setChecked(false);clearManual->setChecked(false);}
        boundId=part->id;target->setText(QStringLiteral("当前零件：%1（父构件数量 %2）").arg(part->name).arg(elementCount));
        current=part->calculation;
        if(current.isEmpty())current=sectionlib::resolve(part->spec);
        mode->setCurrentIndex(current["id"].toString().startsWith("formula:")?1:0);
        if(mode->currentIndex()==1) {
            shape->setCurrentIndex(current["shape"].toInt());
            h->setValue(current["h"].toDouble());b->setValue(current["b"].toDouble());
            tw->setValue(current["tw"].toDouble());tf->setValue(current["tf"].toDouble());radius->setValue(current["radius"].toDouble());extra->setValue(current["c"].toDouble());
        }
        length->setValue(part->lengthMm);count->setValue(part->count);
        width->setValue(current["width_mm"].toDouble(1000));material->setCurrentText(sectionlib::material(part->material));
        manualWeight=part->measuredWeightKg>=0;manualArea=part->measuredAreaM2>=0;
        lockedWeight=part->measuredWeightKg;lockedArea=part->measuredAreaM2;
    }
    loading=false;
    // Render without a second commit after the table's refresh.
    const QSignalBlocker block(this);updatePreview();
}

void SectionCalculator::closeEvent(QCloseEvent* event) {
    // Closing the window must never leave an armed live binding behind: the window is
    // only hidden (not destroyed), so reopening rebinds to the current row explicitly.
    live->setChecked(false);
    emit windowClosed();
    QWidget::closeEvent(event);
}

void SectionCalculator::updateDimensions() {
    const int type=shape->currentIndex();const bool custom=mode->currentIndex()==1;
    // Which inputs each shape uses; unused rows are hidden and stored as zero by sectionlib::formula.
    static const struct {bool b,tw,tf,r,c;const char *hl,*bl,*twl,*tfl,*cl,*rule;} kShapes[sectionlib::kCustomShapeCount]={
        {1,1,1,1,0,"高度 h (mm)","宽度 b (mm)","腹板 tw (mm)","翼缘 tf (mm)","","h > 2tf，b > tw；r ≤ min((b−tw)/2, (h−2tf)/2)"},
        {0,1,0,0,0,"直径 D (mm)","","壁厚 t (mm)","","","D > 2t，壁厚 t > 0"},
        {1,1,0,1,0,"高度 h (mm)","宽度 b (mm)","壁厚 t (mm)","","","h、b > 2t；r = 0 或 t ≤ r ≤ min(h,b)/2"},
        {0,0,0,0,0,"直径 D (mm)","","","","","直径 D > 0"},
        {1,1,0,0,0,"一肢 h (mm)","另一肢 b (mm)","厚度 t (mm)","","","h、b > t，厚度 t > 0；按尖角计算，肢可不等"},
        {0,1,0,0,0,"板宽 b (mm)","","板厚 t (mm)","","","板宽、板厚 > 0；仅计算双面大面"},
        {1,1,1,1,0,"总高 h (mm)","翼缘宽 b (mm)","腹板 tw (mm)","翼缘 tf (mm)","","h > tf，b > tw；r ≤ min((b−tw)/2, h−tf)"},
        {1,1,1,1,0,"高度 h (mm)","翼缘长 b (mm)","腹板 tw (mm)","翼缘 tf (mm)","","h > 2tf，b > tw；r ≤ min(b−tw, (h−2tf)/2)；腹板在背面"},
        {1,1,0,0,1,"高度 h (mm)","翼缘宽 b (mm)","板厚 t (mm)","","卷边 c (mm，自翼缘外表面量起)","c > t，2c < h，h > 2t，b > 2t；尖角"},
        {1,1,0,0,1,"高度 h (mm)","翼缘长 b (mm，自腹板外表面量起)","板厚 t (mm)","","卷边 c (mm，自翼缘外表面量起)","c > t，2c < h，h > 2t，b > 2t；尖角"},
        {1,0,0,0,0,"高度 h (mm)","宽度 b (mm)","","","","h、b > 0"},
        {0,0,0,0,0,"对边距 s (mm)","","","","","对边距 s > 0；表面积按外周长"},
        {0,0,0,0,0,"对边距 s (mm)","","","","","对边距 s > 0；表面积按外周长"},
        {1,1,1,0,1,"高度 h (mm)","宽度 b (mm)","两侧腹板 tw (mm)","上板厚 tf (mm)","下板厚 c (mm)","b > 2tw，h > tf + c；仅计外表面，尖角"},
        {1,1,1,0,1,"高度 h (mm)","上翼缘宽 b (mm)","腹板 tw (mm)","翼缘厚 tf (mm，上下相同)","下翼缘宽 c (mm)","h > 2tf，b、c > tw；腹板居中"},
    };
    const auto& k=kShapes[qBound(0,type,sectionlib::kCustomShapeCount-1)];
    dimensionForm->setRowVisible(b,k.b);dimensionForm->setRowVisible(tw,k.tw);dimensionForm->setRowVisible(tf,k.tf);
    dimensionForm->setRowVisible(radius,k.r);dimensionForm->setRowVisible(extra,k.c);
    auto label=[&](QWidget* field,const QString& text){if(text.isEmpty())return;qobject_cast<QLabel*>(dimensionForm->labelForField(field))->setText(text);field->setAccessibleName(text);};
    label(h,QString::fromUtf8(k.hl));label(b,QString::fromUtf8(k.bl));label(tw,QString::fromUtf8(k.twl));label(tf,QString::fromUtf8(k.tfl));label(extra,QString::fromUtf8(k.cl));
    label(radius,type==2?QStringLiteral("外圆角 r (mm)"):QStringLiteral("根部圆角 r (mm)"));
    const QString rule=QString::fromUtf8(k.rule);
    const bool valid=!sectionlib::formula(type,h->value(),b->value(),tw->value(),tf->value(),radius->value(),extra->value()).isEmpty();
    validation->setVisible(custom);validation->setText((valid?QStringLiteral("尺寸有效："):QStringLiteral("尺寸无效，不回写："))+rule);
    QPalette vp=validation->palette();vp.setColor(QPalette::WindowText,valid?palette().color(QPalette::Text):QColor("#c04040"));validation->setPalette(vp);
    auto* selected=types->topLevelItem(custom?1:0)->child(custom?type:family->currentIndex());
    if(types->currentItem()!=selected){QSignalBlocker block(types);types->setCurrentItem(selected);}
    types->scrollToItem(selected);
}
