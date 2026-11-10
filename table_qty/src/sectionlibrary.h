#pragma once
#include <QtCore>
#include <cmath>
#include "sections.h"

// Coefficients are a snapshot on each part, never a run-time dependency on another program.
namespace sectionlib {
inline QString key(QString s) {
    return s.normalized(QString::NormalizationForm_KC).trimmed().toUpper()
        .replace("×","X").replace("*","X").remove(' ');
}
inline QJsonObject read(const QString& file) {
    QFile f(file); if (!f.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}
inline QVector<QJsonObject>& records() { static QVector<QJsonObject> r; return r; }
inline QHash<QString,int>& index() { static QHash<QString,int> i;return i; }
inline QStringList materials() { return {"Q235B","Q355B","Q355C","Q355D","Q390B","Q420B","Q235","Q355"}; }
inline QString material(QString s) {
    s=s.trimmed().toUpper();
    if (s.startsWith("Q345")) s.replace(0,4,"Q355");
    return s;
}
inline bool valid(const QJsonObject& r) {
    const double m=r["mass"].toDouble(-1), a=r["surface"].toDouble(-1);
    return !r["id"].toString().isEmpty() && std::isfinite(m) && m>0 &&
        std::isfinite(a) && a>=0 && (r["unit"]=="kg/m" || r["unit"]=="kg/m2");
}
inline void reindex() {
    index().clear();
    for(int i=0;i<records().size();++i) {
        const auto& r=records()[i];if(!valid(r))continue;
        auto aliases=r["aliases"].toArray();aliases.append(r["spec"]);
        for(auto alias:aliases) {
            const auto k=key(alias.toString());
            if(!index().contains(k) || r["default"].toBool())index()[k]=i;
        }
    }
}
inline QJsonObject resolve(const QString& spec);
inline void load(const QString& assets) {
    auto& list=records(); list.clear();
    const auto root=read(QDir(assets).filePath("data/unified_sections.json"));
    for (auto v:root["records"].toArray()) list.append(v.toObject());
    reindex();
    catalogSectionResolver()=[](const QString& spec) {
        const auto r=resolve(spec);
        SectionProps p;
        if(valid(r) && r["unit"]=="kg/m") {
            p.ok=true;p.massPerM=r["mass"].toDouble();p.surfacePerM=r["surface"].toDouble();
            p.kind=r["family"].toString();
        }
        return p;
    };
}
inline QJsonObject resolve(const QString& spec) {
    const auto i=index().constFind(key(spec));
    return i==index().cend()?QJsonObject():records().value(i.value());
}
inline constexpr int kCustomShapeCount=15; // IDs 0-5 are persisted by v1.0/v1.1; append only.
inline QJsonObject formula(int shape, double h, double b, double tw, double tf, double radius=0, double c=0) {
    // All dimensions are in mm; area is mm²; perimeter is mm.
    QJsonObject r;
    double a=0,p=0;
    QString spec,formula,basis=QStringLiteral("纵向外表面；不计端面、切口和连接扣除");
    const auto dim=[](double v){return QString::number(v,'g',12);};
    if (h<=0 || !std::isfinite(h) || radius<0 || c<0 || !std::isfinite(c)) return r;
    const double fillet=(2-kPi/2)*radius*radius, trim=2*(2-kPi/2)*radius; // two root fillets
    const bool usesRadius=shape==0||shape==2||shape==6||shape==7;
    if (!usesRadius) radius=0;
    if (!(shape==8||shape==9||shape==13||shape==14)) c=0;
    switch(shape) {
    case 0: // welded H: root fillets are optional independent geometry
        if (b<=0 || tw<=0 || tf<=0 || h<=2*tf || b<=tw ||
            radius>qMin((b-tw)/2,(h-2*tf)/2)) return r;
        a=2*b*tf+(h-2*tf)*tw+(4-kPi)*radius*radius;
        p=2*h+4*b-2*tw-4*(2-kPi/2)*radius;
        spec="WH"+dim(h)+"x"+dim(b)+"x"+dim(tw)+"x"+dim(tf);
        formula="A=2b·tf+(h−2tf)·tw+(4−π)r²"; break;
    case 1: // circular hollow section, outside only
        if (tw<=0 || h<=2*tw) return r;
        a=kPi*(h*h-(h-2*tw)*(h-2*tw))/4; p=kPi*h;
        spec="PIPE"+dim(h)+"x"+dim(tw);
        formula="A=π[D²−(D−2t)²]/4；P=πD"; break;
    case 2: // rectangular hollow, explicit outer/inner radii
        if (b<=0 || tw<=0 || qMin(h,b)<=2*tw ||
            radius>qMin(h,b)/2 || (radius>0 && radius<tw)) return r;
        a=h*b-(h-2*tw)*(b-2*tw);
        if (radius>0) a-=(4-kPi)*(radius*radius-(radius-tw)*(radius-tw));
        p=2*(h+b)-(8-2*kPi)*radius;
        spec="BOX"+dim(h)+"x"+dim(b)+"x"+dim(tw);
        formula="A=HB−(H−2t)(B−2t)−(4−π)[R²−(R−t)²]；R=0 为尖角"; break;
    case 3: // round bar
        a=kPi*h*h/4;p=kPi*h;spec="ROD"+dim(h);
        formula="A=πD²/4；P=πD";break;
    case 4: // angle with sharp corners
        if (b<=0 || tw<=0 || qMin(h,b)<=tw) return r;
        a=(h+b-tw)*tw;p=2*(h+b);spec="L"+dim(h)+"x"+dim(b)+"x"+dim(tw);
        formula="A=(a+b−t)t；P=2(a+b)，尖角截面";break;
    case 5: // plate with actual width, two broad faces only
        if (tw<=0) return r;
        a=h*tw;p=2*h;spec="FB"+dim(h)+"x"+dim(tw);
        basis=QStringLiteral("钢板双面大面；不计侧边、端面及孔洞");
        formula="W=宽×长×厚×7.85×10⁻⁶；S=2×宽×长×10⁻⁶";break;
    case 6: // welded/cut T: flange b×tf on top of web tw; optional two root fillets
        if (b<=tw || tw<=0 || tf<=0 || h<=tf || radius>qMin((b-tw)/2,h-tf)) return r;
        a=b*tf+(h-tf)*tw+fillet; p=2*(b+h)-trim;
        spec="WT"+dim(h)+"x"+dim(b)+"x"+dim(tw)+"x"+dim(tf);
        formula="A=b·tf+(h−tf)·tw+(2−π/2)r²；P=2(b+h)−2(2−π/2)r"; break;
    case 7: // welded channel, web on the back, two root fillets
        if (b<=tw || tw<=0 || tf<=0 || h<=2*tf || radius>qMin(b-tw,(h-2*tf)/2)) return r;
        a=h*tw+2*(b-tw)*tf+fillet; p=2*h+4*b-2*tw-trim;
        spec="WCH"+dim(h)+"x"+dim(b)+"x"+dim(tw)+"x"+dim(tf);
        formula="A=h·tw+2(b−tw)tf+(2−π/2)r²；P=2h+4b−2tw−2(2−π/2)r"; break;
    case 8: case 9: // cold-formed lipped C / Z, constant t, sharp corners, c measured from the outer flange face
        if (b<=0 || tw<=0 || c<=tw || h<=2*tw || b<=2*tw || 2*c>=h) return r;
        a=tw*(h+2*b+2*c-4*tw); p=2*h+4*b+4*c-6*tw;
        spec=QString(shape==8?"CFC":"CFZ")+dim(h)+"x"+dim(b)+"x"+dim(c)+"x"+dim(tw);
        formula="A=t(h+2b+2c−4t)；P=2h+4b+4c−6t；尖角，c 自翼缘外表面量起"; break;
    case 10: // solid rectangular bar
        if (b<=0) return r;
        a=h*b; p=2*(h+b); spec="BAR"+dim(h)+"x"+dim(b);
        formula="A=h·b；P=2(h+b)"; break;
    case 11: // hexagon bar, h = across flats
        a=std::sqrt(3.0)/2*h*h; p=2*std::sqrt(3.0)*h; spec="HEX"+dim(h);
        formula="A=(√3/2)s²；P=2√3·s，s 为对边距"; break;
    case 12: // octagon bar, h = across flats
        a=2*(std::sqrt(2.0)-1)*h*h; p=8*(std::sqrt(2.0)-1)*h; spec="OCT"+dim(h);
        formula="A=2(√2−1)s²；P=8(√2−1)s，s 为对边距"; break;
    case 13: // box with side plates tw, top plate tf, bottom plate c; outer surface only, sharp corners
        if (b<=0 || tw<=0 || tf<=0 || c<=0 || b<=2*tw || h<=tf+c) return r;
        a=h*b-(h-tf-c)*(b-2*tw); p=2*(h+b);
        spec="UBOX"+dim(h)+"x"+dim(b)+"x"+dim(tw)+"x"+dim(tf)+"x"+dim(c);
        formula="A=hb−(h−tf−c)(b−2tw)；P=2(h+b)，仅外表面"; break;
    case 14: // welded I with unequal flange widths b (top), c (bottom), equal flange thickness
        if (b<=tw || c<=tw || tw<=0 || tf<=0 || h<=2*tf) return r;
        a=(b+c)*tf+(h-2*tf)*tw; p=2*(b+c+h)-2*tw;
        spec="WHU"+dim(h)+"x"+dim(b)+"x"+dim(c)+"x"+dim(tw)+"x"+dim(tf);
        formula="A=(b+c)tf+(h−2tf)tw；P=2(b+c+h)−2tw"; break;
    default:return r;
    }
    if (!std::isfinite(a) || !std::isfinite(p) || a<=0 || p<=0) return {};
    r={{"id",QString("formula:v1:%1").arg(shape)},{"spec",spec},{"family",QStringLiteral("非标参数计算")},
        {"standard",QStringLiteral("几何公式 / 非标尺寸")},{"mass",a*0.00785},{"surface",p*0.001},
        {"unit","kg/m"},{"surface_basis",basis},{"source",formula},{"area_mm2",a},
        {"shape",shape},{"h",h},{"b",b},{"tw",tw},{"tf",tf},{"radius",radius},{"c",c}};
    return r;
}
}
