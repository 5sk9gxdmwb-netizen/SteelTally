#pragma once
#include "sectioncalculator.h"
#include "sectiondrawing.h"
#include "startupsplash.h"
#include "sectionlibrary.h"

inline int runUiChecks(const QString& output) {
    QDir().mkpath(output);QJsonArray checks;bool ok=true;
    auto check=[&](bool value,const QString& name){checks.append(QJsonObject{{"name",name},{"ok",value}});ok=value&&ok;};
    SectionCalculator c;Part part;part.name=QStringLiteral("UI synthetic test");c.bind(&part,2);c.resize(780,850);c.show();
    auto* tree=c.findChild<QTreeWidget*>("sectionTypeTree");
    auto* mode=c.findChild<QComboBox*>("calculationMode");auto* shape=c.findChild<QComboBox*>("sectionShape");
    auto* draw=static_cast<SectionDrawing*>(c.findChild<QWidget*>("sectionDrawing"));
    auto* live=c.findChild<QCheckBox*>("calculationLive");auto* apply=c.findChild<QPushButton*>("applyCalculation");
    int commits=0;QObject::connect(&c,&SectionCalculator::commitRequested,&c,[&]{++commits;});
    check(!QApplication::windowIcon().isNull(),"application-icon");
    check(tree && tree->topLevelItemCount()==2,"grouped-type-tree");
    auto set=[&](const char* name,double value){c.findChild<QDoubleSpinBox*>(name)->setValue(value);};
    for(int i=0;i<sectionlib::kCustomShapeCount;++i){
        tree->setCurrentItem(tree->topLevelItem(1)->child(i));
        set("dimensionH",400);set("dimensionB",200);set("dimensionTw",8);set("dimensionTf",13);set("dimensionR",0);set("dimensionC",50);
        QApplication::processEvents();
        check(mode->currentIndex()==1 && shape->currentIndex()==i && draw->geometry()["shape"].toInt(-1)==i,QString("shape-%1-tree-and-drawing").arg(i));
        check(apply->isEnabled(),QString("shape-%1-valid").arg(i));
        const bool bVisible=!c.findChild<QDoubleSpinBox*>("dimensionB")->isHidden();
        check(bVisible==(i==0||i==2||i==4||i==6||i==7||i==8||i==9||i==10||i==13||i==14),QString("shape-%1-dynamic-fields").arg(i));
        check(c.grab().save(QDir(output).filePath(QString("section-%1.png").arg(i))),QString("shape-%1-screenshot").arg(i));
    }
    // Every standard family with explicit parameters must be drawable; unsupported ones must say so.
    {
        QHash<QString,bool> drawable;QHash<QString,int> failed;
        for(const auto& rec:sectionlib::records()){
            draw->setRecord(rec,true);const auto fam=rec["family"].toString();
            if(!draw->geometry().isEmpty())drawable[fam]=true;
            else if(!rec["parameters"].toString().isEmpty() && !(fam==QStringLiteral("钢轨")||fam==QStringLiteral("格栅板")) &&
                    rec["spec"]!="C50*45*30*2" && rec["spec"]!="Z250*70*2*2.5"){++failed[fam];std::fprintf(stderr,"undrawable: %s %s\n",qPrintable(fam),qPrintable(rec["spec"].toString()));}
        }
        for(const char* fam:{"工字钢","槽钢","角钢","不等边角钢","圆钢","H型钢","C型钢","Z型钢","T型钢","热轧扁钢","圆管","矩形管","方钢","钢板","焊接H型钢","焊接箱形","花纹钢板"})
            check(drawable.value(QString::fromUtf8(fam)) && failed.value(QString::fromUtf8(fam))==0,QString("standard-family-drawable-%1").arg(QString::fromUtf8(fam)));
        for(const char* fam:{"格栅板","钢轨"})
            check(!drawable.value(QString::fromUtf8(fam)),QString("standard-family-not-invented-%1").arg(QString::fromUtf8(fam)));
    }
    tree->setCurrentItem(tree->topLevelItem(1)->child(1));
    set("dimensionH",100);set("dimensionTw",5);apply->click();const int adopted=commits;
    set("dimensionH",120);
    check(commits==adopted+1 && draw->geometry()["h"].toDouble()==120,"live-input-and-drawing-update");
    const int before=commits;set("dimensionTw",80);
    check(!apply->isEnabled() && commits==before && draw->geometry().isEmpty(),"invalid-geometry-no-write");
    check(c.findChild<QLabel*>("dimensionValidation")->text().contains(QStringLiteral("无效")),"validation-message");
    Part other;other.name="Other";c.bind(&other);check(!live->isChecked(),"row-switch-disarms-live");
    auto record=sectionlib::resolve("I10");draw->setRecord(record,true);
    check(!draw->geometry().isEmpty(),"standard-explicit-dimensions-drawable");
    record.remove("parameters");draw->setRecord(record,true);check(draw->geometry().isEmpty(),"missing-standard-dimensions-not-invented");
    c.resize(650,760);QApplication::processEvents();check(c.grab().save(QDir(output).filePath("calculator-compact.png")),"compact-screenshot");
    tree->setCurrentItem(tree->topLevelItem(1)->child(0));set("dimensionH",400);set("dimensionTw",8);set("dimensionR",8);
    QApplication::processEvents();check(!draw->geometry().isEmpty(),"h-root-radius-geometry");
    check(c.grab().save(QDir(output).filePath("calculator-radius.png")),"radius-screenshot");
    QFile theme(":/assets/themes/cooldark.qss");if(theme.open(QIODevice::ReadOnly))c.setStyleSheet(QString::fromUtf8(theme.readAll()).replace(QStringLiteral("__ASSETS__"),QStringLiteral(":/assets"))+QStringLiteral("\nQWidget#sectionCalculator, QWidget#sectionParameterPane { background: #363738; }"));
    QApplication::processEvents();check(c.grab().save(QDir(output).filePath("calculator-dark.png")),"dark-screenshot");
    StartupSplash splash;splash.setStage(90,QStringLiteral("界面与型材库已就绪…"));
    check(splash.pixmap().save(QDir(output).filePath("splash.png")),"splash-screenshot");
    QFile result(QDir(output).filePath("ui-result.json"));
    if(!result.open(QIODevice::WriteOnly))return 2;
    result.write(QJsonDocument(QJsonObject{{"ok",ok},{"version",QCoreApplication::applicationVersion()},{"checks",checks}}).toJson());
    std::printf("ui-selftest: %s checks=%d\n",ok?"ok":"FAILED",checks.size());return ok?0:1;
}
