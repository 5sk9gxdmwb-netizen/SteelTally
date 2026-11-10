#include "sectioncalculator.h"
#include "sectionlibrary.h"

SectionCalculator::SectionCalculator(QWidget* parent):QWidget(parent) {
    setObjectName("sectionCalculator");
    setMinimumWidth(360);
    auto* root=new QVBoxLayout(this);root->setContentsMargins(8,4,8,4);
    auto* heading=new QLabel(QStringLiteral("型材查询与实时计算"));root->addWidget(heading);
    target=new QLabel;target->setWordWrap(true);root->addWidget(target);
    mode=new QComboBox;mode->setObjectName("calculationMode");
    mode->addItems({QStringLiteral("标准规格 / 原有型材库"),QStringLiteral("非标尺寸 / 手动计算")});root->addWidget(mode);
    standardPage=new QWidget;auto* sl=new QVBoxLayout(standardPage);sl->setContentsMargins(0,0,0,0);
    family=new QComboBox;family->setObjectName("sectionFamily");family->addItem(QStringLiteral("全部品类"));
    standard=new QComboBox;standard->setObjectName("sectionStandard");standard->addItem(QStringLiteral("全部版本及来源"));
    QSet<QString> families,standards;
    for(const auto& r:sectionlib::records()) {families.insert(r["family"].toString());standards.insert(r["standard"].toString());}
    auto fs=families.values(),ss=standards.values();fs.sort();ss.sort();family->addItems(fs);standard->addItems(ss);
    sl->addWidget(family);sl->addWidget(standard);
    search=new QLineEdit;search->setObjectName("sectionSearch");search->setPlaceholderText(QStringLiteral("搜索规格，例如 I10、HN400、PIPE"));sl->addWidget(search);
    list=new QListWidget;list->setObjectName("sectionResults");list->setMinimumHeight(110);sl->addWidget(list);
    root->addWidget(standardPage,1);
    customPage=new QWidget;auto* form=new QFormLayout(customPage);form->setContentsMargins(0,0,0,0);
    shape=new QComboBox;shape->setObjectName("sectionShape");shape->addItems({QStringLiteral("焊接H型截面"),QStringLiteral("圆管"),
        QStringLiteral("方矩管 / 箱形"),QStringLiteral("圆钢"),QStringLiteral("角钢（尖角）"),QStringLiteral("钢板（双面大面）")});
    form->addRow(QStringLiteral("截面"),shape);
    auto spin=[&](const char* name,double value) {
        auto* v=new QDoubleSpinBox;v->setObjectName(name);v->setRange(0,1e6);v->setDecimals(3);v->setValue(value);
        v->setKeyboardTracking(false);return v;
    };
    h=spin("dimensionH",400);b=spin("dimensionB",200);tw=spin("dimensionTw",8);tf=spin("dimensionTf",13);radius=spin("dimensionR",0);
    form->addRow(QStringLiteral("高 / 直径 / 板宽 (mm)"),h);form->addRow(QStringLiteral("宽 / 另一肢 (mm)"),b);
    form->addRow(QStringLiteral("腹板 / 管壁 / 板厚 (mm)"),tw);form->addRow(QStringLiteral("翼缘厚度 (mm)"),tf);
    form->addRow(QStringLiteral("H根部 / 箱形外圆角 (mm)"),radius);root->addWidget(customPage);
    auto* inputs=new QFormLayout;
    material=new QComboBox;material->setEditable(true);material->setObjectName("calculatorMaterial");material->addItems(sectionlib::materials());
    length=spin("calculatorLength",1000);length->setRange(0,1e9);
    width=spin("calculatorPlateWidth",1000);
    count=new QSpinBox;count->setObjectName("calculatorCount");count->setRange(1,1000000);
    inputs->addRow(QStringLiteral("材质"),material);inputs->addRow(QStringLiteral("长度 (mm)"),length);
    inputs->addRow(QStringLiteral("面积型材板宽 (mm)"),width);inputs->addRow(QStringLiteral("零件数量"),count);root->addLayout(inputs);
    details=new QLabel;details->setWordWrap(true);details->setTextInteractionFlags(Qt::TextSelectableByMouse);root->addWidget(details);
    preview=new QLabel;preview->setObjectName("calculationPreview");preview->setWordWrap(true);root->addWidget(preview);
    clearManual=new QCheckBox(QStringLiteral("替换当前行的人工重量 / 面积"));root->addWidget(clearManual);
    live=new QCheckBox(QStringLiteral("采用后随输入实时更新当前行"));live->setObjectName("calculationLive");live->setChecked(false);root->addWidget(live);
    apply=new QPushButton(QStringLiteral("采用结果并开启实时联动"));apply->setObjectName("applyCalculation");root->addWidget(apply);
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
    for(auto* v:{h,b,tw,tf,radius,length,width})
        connect(v,&QDoubleSpinBox::valueChanged,this,[this] {updatePreview();});
    connect(count,&QSpinBox::valueChanged,this,[this] {updatePreview();});
    connect(material,&QComboBox::currentTextChanged,this,[this] {updatePreview();});
    connect(clearManual,&QCheckBox::toggled,this,[this] {updatePreview();});
    for(auto* label:{heading,target,details,preview}) label->setSizePolicy(QSizePolicy::Preferred,QSizePolicy::Maximum);
    customPage->setSizePolicy(QSizePolicy::Preferred,QSizePolicy::Maximum);
    root->addStretch();
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
    auto r=mode->currentIndex()==0?current:sectionlib::formula(shape->currentIndex(),h->value(),b->value(),tw->value(),tf->value(),radius->value());
    r["width_mm"]=width->value();
    return r;
}
void SectionCalculator::updatePreview() {
    if(loading)return;
    standardPage->setVisible(mode->currentIndex()==0);customPage->setVisible(mode->currentIndex()==1);
    b->setEnabled(shape->currentIndex()==0 || shape->currentIndex()==2 || shape->currentIndex()==4);
    tw->setEnabled(shape->currentIndex()!=3);tf->setEnabled(shape->currentIndex()==0);
    radius->setEnabled(shape->currentIndex()==0 || shape->currentIndex()==2);
    const auto r=selectedRecord();const bool ok=sectionlib::valid(r);
    const bool plate=r["unit"]=="kg/m2";width->setEnabled(plate);
    const double factor=length->value()/1000*(plate?width->value()/1000:1);
    const double weight=manualWeight&&!clearManual->isChecked()?lockedWeight:r["mass"].toDouble()*factor;
    const double area=manualArea&&!clearManual->isChecked()?lockedArea:r["surface"].toDouble()*factor;
    details->setText(ok ? QString("%1\n%2\n%3\n%4\n%5 %6；表面积系数 %7")
        .arg(r["spec"].toString(),r["standard"].toString(),r["source"].toString(),r["surface_basis"].toString())
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
            tw->setValue(current["tw"].toDouble());tf->setValue(current["tf"].toDouble());radius->setValue(current["radius"].toDouble());
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
