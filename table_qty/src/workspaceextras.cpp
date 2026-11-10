#include "workspaceextras.h"
#include "calc.h"
#include "pasteutils.h"
#include "tableinputpanel.h"
#include "reports.h"
#include <QtWidgets>
#include <cmath>

namespace {
QString userRoot() {
    const auto args=QCoreApplication::arguments();
    const int i=args.indexOf("--data-root");
    if(i>=0 && i+1<args.size()) return args[i+1];
    return QDir(QCoreApplication::applicationDirPath()).filePath("user-data");
}
QJsonObject readObject(const QString& path) {
    QFile f(path);
    if(!f.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}
QString sourcePath(const QString& assets, bool quota) {
    return QDir(assets).filePath(quota ? "data/quota_items.json" : "data/sections.json");
}
QString overridePath(bool quota,const QString& root=userRoot()) {
    return QDir(root).filePath(quota ? "enterprise_quotas.json" : "enterprise_sections.json");
}
QJsonArray overrides(bool quota,const QString& root=userRoot()) {return readObject(overridePath(quota,root)).value("items").toArray();}
bool saveOverrides(bool quota,const QJsonArray& rows,QString* error,const QString& root=userRoot()) {
    if(!QDir().mkpath(root)) {if(error) *error=QStringLiteral("无法创建企业数据目录"); return false;}
    QSaveFile f(overridePath(quota,root));
    if(!f.open(QIODevice::WriteOnly)) {if(error) *error=f.errorString(); return false;}
    f.write(QJsonDocument(QJsonObject{{"items",rows}}).toJson());
    if(!f.commit()) {if(error) *error=f.errorString(); return false;}
    return true;
}
QString sectionKind(QString norm) {
    norm=norm.toUpper();
    if(norm.startsWith("PL")) return "PL";
    if(norm.startsWith("TH") || norm.startsWith("WH")) return "TH";
    return "TABLE";
}

// Parametric measurements are independent geometry, not recovered GJG algorithms.
Part platePart(const QString& shape, double a,double b,double thickness,int count=1) {
    Part p;
    const double area = shape=="triangle" ? a*b/2 : a*b;
    p.name=shape=="triangle"?QStringLiteral("三角节点板"):QStringLiteral("矩形节点板");
    p.spec=QStringLiteral("FB%1x%2").arg(b).arg(thickness);
    p.lengthMm=a; p.count=count;
    p.measuredWeightKg=area*thickness*7.85e-6;
    p.measuredAreaM2=area*2e-6; // both large faces; edges excluded, explicitly recorded
    p.partType=QStringLiteral("节点板");
    p.measurement={{"method","independent_geometry"},{"shape",shape},
                   {"a_mm",a},{"b_mm",b},{"t_mm",thickness},{"surface_basis","two_faces_without_edges"}};
    return p;
}
Part ringPart(double outer,double inner,double t) {
    Part p=platePart("rectangle",1,1,t);
    const double area=kPi/4*(outer*outer-inner*inner);
    p.name=QStringLiteral("环形肋板"); p.spec=QStringLiteral("环板 D%1-d%2-t%3").arg(outer).arg(inner).arg(t);
    p.measuredWeightKg=area*t*7.85e-6; p.measuredAreaM2=area*2e-6;
    p.measurement={{"method","independent_geometry"},{"shape","ring"},{"outer_mm",outer},{"inner_mm",inner},
                   {"t_mm",t},{"surface_basis","two_faces_without_edges"}};
    return p;
}
}

void extras::loadLibraries(const QString& assets) {
    sectionOverrideTable().clear();
    sectionlib::load(assets);
    for(const auto& r:sectionlib::records()) if(sectionlib::valid(r) && r["unit"]=="kg/m") {
        const auto spec=r["spec"].toString();
        if(!sectionOverrideTable().contains(spec) || r["default"].toBool())
            registerSection(spec,r["mass"].toDouble(),r["surface"].toDouble(),sectionKind(spec));
    }
    for(const auto& value:overrides(false)) {
        const auto o=value.toObject(); const auto key=o["spec"].toString();
        registerSection(key,o["mass"].toDouble(),o["area"].toDouble(),sectionKind(key));
        sectionlib::records().append(QJsonObject{{"id","enterprise:"+key},{"spec",key},
            {"family",QStringLiteral("企业型材")},{"standard",QStringLiteral("企业自定义")},
            {"mass",o["mass"]},{"surface",o["area"]},{"unit","kg/m"},{"default",true},
            {"source",QStringLiteral("企业数据覆盖")},{"surface_basis",QStringLiteral("企业定义口径")}});
    }
    sectionlib::reindex();
}

void extras::editLibrary(QWidget* parent,const QString& assets,bool quota) {
    QDialog dlg(parent);
    dlg.setWindowTitle(quota?QStringLiteral("定额项目库"):QStringLiteral("型钢断面库"));
    dlg.resize(980,610);
    QVBoxLayout layout(&dlg);
    QLineEdit search; search.setPlaceholderText(QStringLiteral("搜索名称、编号、分类（最多显示前 500 条）")); layout.addWidget(&search);
    QTabWidget tabs; layout.addWidget(&tabs);
    QTableWidget reference;
    reference.setColumnCount(5);
    reference.setHorizontalHeaderLabels(quota?QStringList{QStringLiteral("库 ID"),QStringLiteral("编号"),QStringLiteral("名称"),QStringLiteral("单位"),QStringLiteral("分类")}:
                                               QStringList{QStringLiteral("规格"),QStringLiteral("单位重量（见单位）"),QStringLiteral("面积系数"),QStringLiteral("标准 / 单位"),QStringLiteral("参数")});
    reference.setEditTriggers(QAbstractItemView::NoEditTriggers);
    tabs.addTab(&reference,QStringLiteral("内置参考（只读）"));
    QVector<QStringList> rows;
    const auto raw=readObject(sourcePath(assets,quota));
    if(quota) {
        const auto libraries=raw.value("librarys").toObject();
        for(auto i=libraries.begin();i!=libraries.end();++i)
            for(const auto& v:i.value().toArray()) {
                const auto o=v.toObject();
                rows<<QStringList{i.key(),o["number"].toString(),o["name"].toString(),o["unit"].toString(),QString::number(o["category"].toInt())};
            }
    } else for(const auto& o:sectionlib::records()) {
        rows<<QStringList{o["spec"].toString(),QString::number(o["mass"].toDouble()),
            o["surface"].toDouble(-1)<0?QStringLiteral("未定义"):QString::number(o["surface"].toDouble()),
            o["standard"].toString()+" / "+o["unit"].toString(),o["parameters"].toString()};
    }
    const auto filter=[&]() {
        reference.setRowCount(0);
        for(const auto& row:rows) {
            if(!row.join(" ").contains(search.text(),Qt::CaseInsensitive)) continue;
            const int r=reference.rowCount(); reference.insertRow(r);
            for(int c=0;c<row.size();++c) reference.setItem(r,c,new QTableWidgetItem(row[c]));
            if(reference.rowCount()==500) break;
        }
        reference.resizeColumnsToContents();
    };
    QObject::connect(&search,&QLineEdit::textChanged,&dlg,filter); filter();
    QWidget enterprise;
    QVBoxLayout enterpriseLayout(&enterprise);
    QTableWidget local;
    local.setColumnCount(quota?4:3);
    local.setHorizontalHeaderLabels(quota?QStringList{QStringLiteral("库 ID"),QStringLiteral("编号"),QStringLiteral("名称"),QStringLiteral("单位")}:
                                          QStringList{QStringLiteral("规格"),QStringLiteral("kg/m"),QStringLiteral("m²/m")});
    const QStringList keys=quota?QStringList{"library","number","name","unit"}:QStringList{"spec","mass","area"};
    const auto values=overrides(quota);
    local.setRowCount(values.size());
    for(int r=0;r<values.size();++r)
        for(int c=0;c<keys.size();++c)
            local.setItem(r,c,new QTableWidgetItem(values[r].toObject()[keys[c]].toVariant().toString()));
    enterpriseLayout.addWidget(new QLabel(quota?QStringLiteral("企业定额以库 ID + 编号标识；可新增、删除，保存后作为本机参考。"):
        QStringLiteral("同名规格覆盖内置理论重量和面积，保存后重算。内置数据保持完整。")));
    enterpriseLayout.addWidget(&local);
    QHBoxLayout tools; QPushButton add(QStringLiteral("新增")),remove(QStringLiteral("删除选中")),save(QStringLiteral("保存企业数据"));
    tools.addWidget(&add);tools.addWidget(&remove);tools.addWidget(&save); enterpriseLayout.addLayout(&tools);
    QObject::connect(&add,&QPushButton::clicked,&dlg,[&](){local.insertRow(local.rowCount());});
    QObject::connect(&remove,&QPushButton::clicked,&dlg,[&](){if(local.currentRow()>=0)local.removeRow(local.currentRow());});
    QObject::connect(&save,&QPushButton::clicked,&dlg,[&](){
        QJsonArray data; QSet<QString> seen;
        for(int r=0;r<local.rowCount();++r) {
            QJsonObject row;
            for(int c=0;c<keys.size();++c) row[keys[c]]=local.item(r,c)?local.item(r,c)->text().trimmed():QString();
            const QString key=quota?row["library"].toString()+"|"+row["number"].toString():row["spec"].toString();
            bool valid=!key.isEmpty() && !seen.contains(key);
            if(quota) valid=valid && !row["library"].toString().isEmpty() && !row["number"].toString().isEmpty() &&
                !row["name"].toString().isEmpty() && !row["unit"].toString().isEmpty();
            else {
                bool a=false,b=false;
                const double mass=row["mass"].toString().toDouble(&a),area=row["area"].toString().toDouble(&b);
                valid=valid && a && b && std::isfinite(mass) && std::isfinite(area) && mass>0 && area>=0;
                row["mass"]=mass;row["area"]=area;
            }
            if(!valid){QMessageBox::warning(&dlg,QStringLiteral("未保存"),QStringLiteral("第 %1 行有缺项、重复标识或非法数值。").arg(r+1));return;}
            seen.insert(key);data.append(row);
        }
        QString error;
        if(!saveOverrides(quota,data,&error)) {QMessageBox::warning(&dlg,QStringLiteral("未保存"),error);return;}
        loadLibraries(assets);
        QMessageBox::information(&dlg,QStringLiteral("已保存"),QStringLiteral("企业数据已保存。"));
    });
    tabs.addTab(&enterprise,QStringLiteral("企业数据（可编辑）"));
    QDialogButtonBox close(QDialogButtonBox::Close);
    QObject::connect(&close,&QDialogButtonBox::rejected,&dlg,&QDialog::reject);
    layout.addWidget(&close);dlg.exec();
    // Stack widgets are detached before parent destruction by normal C++ order.
}

bool extras::insertNode(QWidget* parent,Element& element) {
    QDialog dlg(parent);dlg.setWindowTitle(QStringLiteral("节点零件参数")); QFormLayout form(&dlg);
    QComboBox shape;
    shape.addItems({QStringLiteral("矩形节点板"),QStringLiteral("三角节点板"),QStringLiteral("环形肋板"),
                    QStringLiteral("格构柱缀条（定尺型钢）"),QStringLiteral("铸钢件（图纸净重）")});
    form.addRow(QStringLiteral("类型"),&shape);
    QDoubleSpinBox a,b,t,area;QSpinBox count;QLineEdit spec,source;
    for(auto* spin:{&a,&b,&t,&area}){spin->setRange(0,1e8);spin->setDecimals(3);}
    a.setValue(400);b.setValue(200);t.setValue(10);count.setRange(1,1000000);count.setValue(1);
    spec.setText("L75x5");
    form.addRow(QStringLiteral("长/外径/定尺长(mm)/单件净重(kg)"),&a);
    form.addRow(QStringLiteral("宽/内径(mm)"),&b);form.addRow(QStringLiteral("板厚(mm)"),&t);
    form.addRow(QStringLiteral("型钢规格（缀条）"),&spec);
    form.addRow(QStringLiteral("单件面积(m²，仅铸钢件)"),&area);
    form.addRow(QStringLiteral("数量"),&count);
    form.addRow(QStringLiteral("图号/净重或尺寸依据（必填）"),&source);
    QLabel note(QStringLiteral("参数由图纸录入。板件面积按双面大面、不计侧边；相贯切口须按图纸实测长度，不自动推算。"));
    note.setWordWrap(true);form.addRow(&note);
    QDialogButtonBox buttons(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);
    form.addRow(&buttons);QObject::connect(&buttons,&QDialogButtonBox::rejected,&dlg,&QDialog::reject);
    Part result;
    QObject::connect(&buttons,&QDialogButtonBox::accepted,&dlg,[&](){
        const int mode=shape.currentIndex();
        if(source.text().trimmed().isEmpty() || a.value()<=0 ||
           (mode<3 && (t.value()<=0 || b.value()<=0 || (mode==2 && a.value()<=b.value())))) {
            QMessageBox::warning(&dlg,QStringLiteral("未添加"),QStringLiteral("请填写有效尺寸和图纸依据；环板外径须大于内径。"));return;
        }
        if(mode<2) result=platePart(mode==0?"rectangle":"triangle",a.value(),b.value(),t.value());
        else if(mode==2)result=ringPart(a.value(),b.value(),t.value());
        else if(mode==3) {
            if(!sectionProps(spec.text()).ok){QMessageBox::warning(&dlg,QStringLiteral("未添加"),QStringLiteral("型钢规格无法解析"));return;}
            result.name=QStringLiteral("格构柱缀条");result.spec=spec.text();result.lengthMm=a.value();
            result.measurement={{"method","drawing_length"},{"length_mm",a.value()}};
        } else {
            result.name=QStringLiteral("铸钢件");result.spec=QStringLiteral("按图净重");
            result.measuredWeightKg=a.value();result.measuredAreaM2=area.value();
            result.measurement={{"method","drawing_net_weight"},{"unit_weight_kg",a.value()}};
        }
        result.count=count.value();result.remark=source.text();result.measurement["source"]=source.text();
        dlg.accept();
    });
    if(dlg.exec()!=QDialog::Accepted)return false;
    element.parts.append(result);return true;
}

bool extras::runChecks(QString* error) {
    auto check=[&](bool ok,const char* label){if(!ok && error)*error=label;return ok;};
    const auto triangle=platePart("triangle",400,200,10);
    Element e;e.count=2;
    if(!check(std::abs(rowCalc(e,triangle).totalWeight-6.28)<1e-9,"triangle mass"))return false;
    const auto ring=ringPart(400,200,10);
    if(!check(std::abs(ring.measuredWeightKg-kPi/4*(160000-40000)*10*7.85e-6)<1e-9,"ring mass"))return false;
    const auto restored=Part::fromJson(ring.toJson());
    if(!check(restored.measuredWeightKg==ring.measuredWeightKg && restored.measurement==ring.measurement,"node roundtrip"))return false;
    QVector<QStringList> cells;
    if(!check(parseTsv("a\t\"b\"\"c\"\n1\t2",cells,error) && cells[0][1]=="b\"c","quoted tsv"))return false;
    Project p;Element sample;sample.parts={Part()};p.elements={sample};
    TableInputPanel panel(&p,nullptr,false);
    const int col=TableInputPanel::Spec;
    if(!check(panel.applyMatrix("H200x100x6x8\t\t6000\t2",1,col,error),"matrix import"))return false;
    const auto before=p.toJson();
    if(!check(!panel.applyMatrix("I10\t\t-5\t1",1,col,error) && p.toJson()==before,"matrix atomic rollback"))return false;
    if(!check(p.elements[0].parts[0].count==2 && p.elements[0].parts[0].lengthMm==6000,"matrix persisted"))return false;
    Part different=p.elements[0].parts.first(); different.lengthMm=3000;
    p.elements[0].parts.append(different);
    if(!check(summaryRows(p).size()==2,"different lengths must not merge"))return false;
    if(!check(buildAllReports(p)[7].rows.size()==3,"report lengths must not merge"))return false;
    p.elements[0].parts={triangle,ring};
    const auto reports=buildAllReports(p);
    if(!check(partIsPlate(triangle) && partIsPlate(ring) && reports[13].rows.size()==3,
              "parametric nodes must appear in plate report"))return false;
    if(!check(reports[5].rows[0][7].contains(QStringLiteral("净重")),"parametric calculation basis"))return false;
    Part cast;cast.spec=QStringLiteral("按图净重");cast.measuredWeightKg=10;cast.measuredAreaM2=1;
    cast.measurement={{"method","drawing_net_weight"}};
    Part cast2=cast;cast2.measuredWeightKg=20;
    p.elements[0].parts={cast,cast2};
    if(!check(craftOf(cast)=="abnormal" && summaryRows(p).size()==2,"cast weights must not merge"))return false;
    QTemporaryDir testData;
    QJsonArray enterprise{QJsonObject{{"spec","TEST"},{"mass",10.0},{"area",1.0}}};
    if(!check(testData.isValid() && saveOverrides(false,enterprise,error,testData.path()) &&
              overrides(false,testData.path())==enterprise,"enterprise persistence"))return false;
    enterprise[0]=QJsonObject{{"spec","TEST"},{"mass",20.0},{"area",2.0}};
    if(!check(saveOverrides(false,enterprise,error,testData.path()) &&
              overrides(false,testData.path())==enterprise,"enterprise update"))return false;
    if(!check(saveOverrides(false,{},error,testData.path()) &&
              overrides(false,testData.path()).isEmpty(),"enterprise remove"))return false;
    const QJsonArray quota{QJsonObject{{"library","TEST"},{"number","001"},{"name","Test"},{"unit","kg"}}};
    if(!check(saveOverrides(true,quota,error,testData.path()) &&
              overrides(true,testData.path())==quota,"enterprise quota persistence"))return false;
    if(error) error->clear();
    return true;
}
