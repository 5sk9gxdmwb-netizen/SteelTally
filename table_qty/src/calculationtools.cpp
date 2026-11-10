#include "calculationtools.h"
#include <cmath>

namespace calctools {
namespace {
constexpr double kPi = 3.14159265358979323846;
Field F(const QString& k, const QString& l, const QString& u, const QString& p = QString(), bool z = false) {
    Field f; f.key = k; f.label = l; f.unit = u; f.preset = p; f.allowZero = z; return f;
}
Result fail(const QString& m) { Result r; r.error = m; return r; }
void add(Result& r, const QString& n, double v, const QString& u, const QString& note = QString()) {
    r.rows.push_back({n, v, u, note});
}
bool whole(double v) { return std::fabs(v - std::round(v)) < 1e-9; }
const QString kSurf = QStringLiteral("纵向外表面，不计端面、切口和连接扣除");
const QString kRho = QStringLiteral("钢材密度 7850 kg/m³ 为常用取值，可按材料证明修改");
Field rho() { return F("rho", "密度", "kg/m³", "7850"); }
Field cnt() { return F("n", "数量", "件", "1"); }

// 线材：截面积 a(mm²)、外周长 p(mm)、长度 L(mm)
Result linear(double a, double p, const Inputs& in, const QString& areaNote) {
    const double L = in.value("L"), n = in.value("n"), d = in.value("rho");
    if (!whole(n)) return fail(QStringLiteral("数量必须为整数"));
    Result r;
    add(r, "截面积", a, "mm²", areaNote);
    add(r, "单位长度重量", a * 1e-6 * d, "kg/m");
    add(r, "单件体积", a * L * 1e-9, "m³");
    add(r, "单件重量", a * L * 1e-9 * d, "kg");
    add(r, "总体积", a * L * 1e-9 * n, "m³");
    add(r, "总重量", a * L * 1e-9 * d * n, "kg");
    add(r, "总重量", a * L * 1e-9 * d * n / 1000, "t");
    add(r, "外表面积（单件）", p * L * 1e-6, "m²", kSurf);
    return r;
}
Result rectTube(double h, double b, double t, double R, const Inputs& in) {
    if (qMin(h, b) <= 2 * t) return fail(QStringLiteral("壁厚过大：min(H,B) 必须大于 2t"));
    if (R > qMin(h, b) / 2 || (R > 0 && R < t))
        return fail(QStringLiteral("外圆角 R 须满足 t ≤ R ≤ min(H,B)/2；R=0 表示尖角"));
    double a = h * b - (h - 2 * t) * (b - 2 * t);
    if (R > 0) a -= (4 - kPi) * (R * R - (R - t) * (R - t));
    return linear(a, 2 * (h + b) - (8 - 2 * kPi) * R, in,
                  QStringLiteral("A=HB−(H−2t)(B−2t)−(4−π)[R²−(R−t)²]；与截面库非标管同式"));
}

QVector<Spec> areaSpecs() {
    QVector<Spec> v;
    const QStringList common{kRho, QStringLiteral("体积 V=截面积×长度；重量 W=V×密度；单位已在字段中标明")};
    { Spec s; s.name = "自定义展开面积"; s.notes = common;
      s.fields = {F("A", "展开面积", "m²"), F("t", "厚度", "mm"), rho(), cnt()};
      s.run = [](const Inputs& in) {
          if (!whole(in.value("n"))) return fail("数量必须为整数");
          const double V = in.value("A") * in.value("t") / 1000, W = V * in.value("rho"), n = in.value("n");
          Result r; add(r, "单件体积", V, "m³", "V=A×t/1000"); add(r, "单件重量", W, "kg");
          add(r, "总重量", W * n, "kg"); add(r, "总重量", W * n / 1000, "t"); return r; };
      v << s; }
    { Spec s; s.name = "钢板 / 花纹板"; s.notes = common;
      s.notes << QStringLiteral("花纹板的凸纹增重随产品标准而异，本工具不内置；如需计入请把标准给出的附加值填入“花纹附加”（0 表示不计）");
      s.fields = {F("L", "长度", "mm"), F("B", "宽度", "mm"), F("t", "基础厚度", "mm"), rho(),
                  F("extra", "花纹附加", "kg/m²", "0", true), cnt()};
      s.run = [](const Inputs& in) {
          if (!whole(in.value("n"))) return fail("数量必须为整数");
          const double ar = in.value("L") * in.value("B") * 1e-6, V = ar * in.value("t") / 1000;
          const double W = V * in.value("rho") + ar * in.value("extra"), n = in.value("n");
          Result r; add(r, "单件面积", ar, "m²"); add(r, "单件体积", V, "m³"); add(r, "单件重量", W, "kg");
          add(r, "总重量", W * n, "kg"); add(r, "总重量", W * n / 1000, "t");
          add(r, "双面大面面积（单件）", 2 * ar, "m²", "不计侧边、端面及孔洞"); return r; };
      v << s; }
    { Spec s; s.name = "圆管"; s.notes = common;
      s.fields = {F("D", "外径", "mm"), F("t", "壁厚", "mm"), F("L", "长度", "mm"), rho(), cnt()};
      s.run = [](const Inputs& in) {
          const double D = in.value("D"), t = in.value("t");
          if (D <= 2 * t) return fail("壁厚过大：外径必须大于 2t");
          return linear(kPi * (D * D - (D - 2 * t) * (D - 2 * t)) / 4, kPi * D, in, "A=π[D²−(D−2t)²]/4"); };
      v << s; }
    { Spec s; s.name = "方管"; s.notes = common;
      s.fields = {F("a", "边长", "mm"), F("t", "壁厚", "mm"), F("R", "外圆角半径", "mm", "0", true),
                  F("L", "长度", "mm"), rho(), cnt()};
      s.run = [](const Inputs& in) { return rectTube(in.value("a"), in.value("a"), in.value("t"), in.value("R"), in); };
      v << s; }
    { Spec s; s.name = "矩形管"; s.notes = common;
      s.fields = {F("H", "高度", "mm"), F("B", "宽度", "mm"), F("t", "壁厚", "mm"),
                  F("R", "外圆角半径", "mm", "0", true), F("L", "长度", "mm"), rho(), cnt()};
      s.run = [](const Inputs& in) { return rectTube(in.value("H"), in.value("B"), in.value("t"), in.value("R"), in); };
      v << s; }
    { Spec s; s.name = "角钢"; s.notes = common;
      s.notes << QStringLiteral("尖角截面，不计根部圆角和趾部圆弧；查表型钢请以截面库理论重量为准");
      s.fields = {F("a", "边长 a", "mm"), F("b", "边长 b", "mm"), F("t", "厚度", "mm"),
                  F("L", "长度", "mm"), rho(), cnt()};
      s.run = [](const Inputs& in) {
          const double a = in.value("a"), b = in.value("b"), t = in.value("t");
          if (qMin(a, b) <= t) return fail("厚度过大：边长必须大于 t");
          return linear((a + b - t) * t, 2 * (a + b), in, "A=(a+b−t)t"); };
      v << s; }
    { Spec s; s.name = "圆钢"; s.notes = common;
      s.fields = {F("D", "直径", "mm"), F("L", "长度", "mm"), rho(), cnt()};
      s.run = [](const Inputs& in) {
          const double D = in.value("D");
          return linear(kPi * D * D / 4, kPi * D, in, "A=πD²/4"); };
      v << s; }
    { Spec s; s.name = "方钢"; s.notes = common;
      s.fields = {F("a", "边长", "mm"), F("L", "长度", "mm"), rho(), cnt()};
      s.run = [](const Inputs& in) {
          const double a = in.value("a");
          return linear(a * a, 4 * a, in, "A=a²"); };
      v << s; }
    return v;
}
}  // namespace
namespace {
// 板材：t 厚(mm)、dev 展开宽、eff 有效宽，折算系数 dev/eff 须由使用者按所选板型给出
bool profileOk(double dev, double eff, QString* e) {
    if (dev < eff) { *e = QStringLiteral("展开宽度不得小于有效宽度（展开系数 ≥ 1）"); return false; }
    return true;
}
Result perArea(double kgm2, double area, const QString& note) {
    Result r; add(r, "单位面积重量", kgm2, "kg/m²", note); add(r, "铺设面积", area, "m²");
    add(r, "总重量", kgm2 * area, "kg"); add(r, "总重量", kgm2 * area / 1000, "t"); return r;
}
Result steelSheet(const Inputs& in, double extra, const QString& note) {
    QString e; if (!profileOk(in.value("dev"), in.value("eff"), &e)) return fail(e);
    const double k = in.value("dev") / in.value("eff");
    Result r = perArea(in.value("t") / 1000 * in.value("rho") * k + extra, in.value("A"), note);
    add(r, "展开系数 dev/eff", k, "");
    if (extra > 0) add(r, "附加重量", extra, "kg/m²", "按使用者填入值直接相加");
    return r;
}
QVector<Spec> panelSpecs() {
    QVector<Spec> v;
    const QString sh = QStringLiteral("每 m² 铺设面积重量=厚度×密度×(展开宽度/有效宽度)；展开/有效宽度取自所选板型的产品资料，本工具不内置板型库；搭接、封边、紧固件另计");
    { Spec s; s.name = "压型单板"; s.notes << sh;
      s.fields = {F("t", "钢板厚度", "mm"), F("dev", "展开宽度", "mm"), F("eff", "有效宽度", "mm"), rho(), F("A", "铺设面积", "m²")};
      s.run = [](const Inputs& in) { return steelSheet(in, 0, "W=t·ρ·dev/eff"); }; v << s; }
    { Spec s; s.name = "金属夹芯板"; s.notes << sh << QStringLiteral("芯材密度由使用者按产品资料填入；不计胶粘剂和板缝附件");
      s.fields = {F("t1", "上面板厚", "mm"), F("dev1", "上面板展开宽", "mm"), F("eff1", "上面板有效宽", "mm"),
                  F("t2", "下面板厚", "mm"), F("dev2", "下面板展开宽", "mm"), F("eff2", "下面板有效宽", "mm"),
                  F("tc", "芯材厚度", "mm"), F("rhoc", "芯材密度", "kg/m³"), rho(), F("A", "铺设面积", "m²")};
      s.run = [](const Inputs& in) {
          QString e; if (!profileOk(in.value("dev1"), in.value("eff1"), &e) || !profileOk(in.value("dev2"), in.value("eff2"), &e)) return fail(e);
          const double top = in.value("t1") / 1000 * in.value("rho") * in.value("dev1") / in.value("eff1");
          const double bot = in.value("t2") / 1000 * in.value("rho") * in.value("dev2") / in.value("eff2");
          const double core = in.value("tc") / 1000 * in.value("rhoc");
          Result r = perArea(top + bot + core, in.value("A"), "W=上面板+下面板+芯材");
          add(r, "上面板", top, "kg/m²"); add(r, "下面板", bot, "kg/m²"); add(r, "芯材", core, "kg/m²"); return r; };
      v << s; }
    { Spec s; s.name = "玻璃钢采光板"; s.notes << sh << QStringLiteral("玻璃钢密度随树脂/玻纤含量而异，须按产品资料输入");
      s.fields = {F("t", "板厚", "mm"), F("dev", "展开宽度", "mm"), F("eff", "有效宽度", "mm"), F("rho", "板材密度", "kg/m³"), F("A", "铺设面积", "m²")};
      s.run = [](const Inputs& in) { return steelSheet(in, 0, "W=t·ρ·dev/eff"); }; v << s; }
    { Spec s; s.name = "直立锁边板"; s.notes << sh << QStringLiteral("固定座、锁边附件等按“附加重量”填入（0 表示不计）");
      s.fields = {F("t", "钢板厚度", "mm"), F("dev", "展开宽度", "mm"), F("eff", "有效宽度", "mm"), rho(),
                  F("extra", "附加重量", "kg/m²", "0", true), F("A", "铺设面积", "m²")};
      s.run = [](const Inputs& in) { return steelSheet(in, in.value("extra"), "W=t·ρ·dev/eff+附加"); }; v << s; }
    { Spec s; s.name = "PC阳光板"; s.notes << QStringLiteral("多层中空板重量取决于壁结构，按厂家提供的面密度计算，不用板厚×密度估算");
      s.fields = {F("w", "面密度（厂家资料）", "kg/m²"), F("A", "铺设面积", "m²")};
      s.run = [](const Inputs& in) { return perArea(in.value("w"), in.value("A"), "W=面密度"); }; v << s; }
    { Spec s; s.name = "天沟 / 泛水"; s.notes << QStringLiteral("重量=展开宽度×厚度×长度×密度；展开宽度取折弯后展平的总宽，不计搭接和封头");
      s.fields = {F("dev", "展开宽度", "mm"), F("t", "板厚", "mm"), F("L", "总长度", "m"), rho()};
      s.run = [](const Inputs& in) {
          const double ar = in.value("dev") / 1000 * in.value("L");
          const double W = ar * in.value("t") / 1000 * in.value("rho");
          Result r; add(r, "展开面积", ar, "m²"); add(r, "体积", ar * in.value("t") / 1000, "m³");
          add(r, "总重量", W, "kg"); add(r, "总重量", W / 1000, "t"); return r; };
      v << s; }
    return v;
}
Result arcResult(double R, double theta, const Inputs& in) {
    if (!whole(in.value("n"))) return fail("数量必须为整数");
    const double len = R * theta, w = len / 1000 * in.value("um"), n = in.value("n");
    Result r; add(r, "半径", R, "mm"); add(r, "圆心角", theta * 180 / kPi, "°");
    add(r, "中心线弧长", len, "mm", "L=Rθ"); add(r, "单件重量", w, "kg");
    add(r, "总重量", w * n, "kg"); add(r, "总重量", w * n / 1000, "t"); return r;
}
QVector<Spec> curvedSpecs() {
    QVector<Spec> v;
    const QString nt = QStringLiteral("重量=中心线弧长×单位长度重量；单位重量取自型钢表或面积重量工具；弯弧伸长、损耗和接头不计");
    { Spec s; s.name = "半径 + 圆心角"; s.notes << nt;
      s.fields = {F("R", "中心线半径", "mm"), F("deg", "圆心角", "°"), F("um", "单位长度重量", "kg/m"), cnt()};
      s.run = [](const Inputs& in) {
          if (in.value("deg") > 360) return fail("圆心角不得大于 360°");
          return arcResult(in.value("R"), in.value("deg") * kPi / 180, in); };
      v << s; }
    { Spec s; s.name = "弦长 + 矢高"; s.notes << nt << QStringLiteral("R=(c²/4+f²)/(2f)；矢高大于半弦长时按优弧计算");
      s.fields = {F("c", "弦长", "mm"), F("f", "矢高", "mm"), F("um", "单位长度重量", "kg/m"), cnt()};
      s.run = [](const Inputs& in) {
          const double c = in.value("c"), f = in.value("f"), R = (c * c / 4 + f * f) / (2 * f);
          return arcResult(R, 2 * std::atan2(c / 2, R - f), in); };
      v << s; }
    return v;
}
QVector<Spec> boltSpecs() {
    QVector<Spec> v; Spec s; s.name = "高强度螺栓抗滑移";
    s.notes << QStringLiteral("单栓抗滑移承载力 Nv=0.9·k·nf·μ·P（GB 50017 抗剪摩擦型连接的表达式形式，未对照标准原文核验）")
            << QStringLiteral("孔型系数 k、摩擦面抗滑移系数 μ、预拉力 P 必须由使用者按现行标准和螺栓等级取值，本工具不内置取值表")
            << QStringLiteral("只给出算术比值 N/(n·Nv)，不构成连接是否满足规范的结论；未含拉剪联合、净截面和构造要求");
    s.fields = {F("k", "孔型系数 k", ""), F("nf", "传力摩擦面数 nf", "个"), F("mu", "抗滑移系数 μ", ""),
                F("P", "单栓预拉力 P", "kN"), F("nb", "螺栓数量", "个"), F("N", "作用剪力 N", "kN")};
    s.run = [](const Inputs& in) {
        if (!whole(in.value("nf")) || !whole(in.value("nb"))) return fail("摩擦面数和螺栓数量必须为整数");
        if (in.value("mu") > 1) return fail("抗滑移系数 μ 不应大于 1，请核对输入");
        if (in.value("k") > 1) return fail("孔型系数 k 不应大于 1（常用 1.0 / 0.85 / 0.7），请核对输入");
        const double nv = 0.9 * in.value("k") * in.value("nf") * in.value("mu") * in.value("P");
        const double grp = nv * in.value("nb");
        Result r; add(r, "单栓抗滑移承载力 Nv", nv, "kN", "Nv=0.9·k·nf·μ·P");
        add(r, "螺栓组承载力 n·Nv", grp, "kN"); add(r, "作用剪力 N", in.value("N"), "kN");
        add(r, "N/(n·Nv)", in.value("N") / grp, "", "算术比值，≤1 仅表示按所填系数不超过该公式值"); return r; };
    v << s; return v;
}
// 总高 H、级高 h 必须整除（不自动取整）；n 级踢面、n-1 级踏面，水平投影长 L=(n-1)b
bool stairGeom(const Inputs& in, int* n, double* L, QString* e) {
    const double q = in.value("H") / in.value("h");
    if (std::fabs(q - std::round(q)) > 1e-6) {
        *e = QStringLiteral("总高/级高 = %1 不是整数；请调整级高或总高，工具不自动取整").arg(q, 0, 'g', 8); return false;
    }
    *n = int(std::lround(q));
    if (*n < 2) { *e = QStringLiteral("踢面数不足 2 级"); return false; }
    if (*n > 1000) { *e = QStringLiteral("踢面数 %1 超过 1000，请核对级高单位（应为 mm）").arg(*n); return false; }
    *L = (*n - 1) * in.value("b"); return true;
}
QVector<Spec> stairSpecs() {
    QVector<Spec> v;
    { Spec s; s.name = "楼梯梯段";
      s.notes << QStringLiteral("梯梁按两端简支的斜梁：荷载按水平投影计，M=wL²/8，V=wL/2（L 为水平跨度）；垂直梯梁方向挠度 δ=5wk·L⁴/(384·E·I·cos²θ)，不计轴力、踏步板和平台梁的影响")
                << QStringLiteral("荷载 g、q 为使用者给出的面荷载（kN/m²，按水平投影），分项系数 γG、γQ 由使用者给出；本工具不内置荷载规范取值，也不判定是否符合规范")
                << QStringLiteral("假定两根梯梁平均分担全宽荷载；梯梁自重须计入恒载 g，工具不自动添加")
                << QStringLiteral("2h+b 仅作显示，不作合格判断");
      s.fields = {F("H", "层高（总高）", "mm"), F("h", "级高", "mm"), F("b", "踏步宽", "mm"), F("B", "梯段宽度", "mm"),
                  F("g", "恒载（面）", "kN/m²"), F("q", "活载（面）", "kN/m²"), F("gG", "恒载分项系数", ""), F("gQ", "活载分项系数", ""),
                  F("E", "弹性模量", "N/mm²", "206000"), F("I", "单根梯梁惯性矩", "mm⁴"), F("W", "单根梯梁截面模量", "mm³"),
                  F("f", "所取强度设计值", "N/mm²"), F("lim", "挠度限值 跨度/", "")};
      s.run = [](const Inputs& in) {
          int n; double L; QString e; if (!stairGeom(in, &n, &L, &e)) return fail(e);
          const double H = in.value("H"), Ls = std::sqrt(H * H + L * L), c2 = L * L / (Ls * Ls);
          const double wd = (in.value("gG") * in.value("g") + in.value("gQ") * in.value("q")) * in.value("B") / 1000 / 2;  // kN/m=N/mm，每根梯梁
          const double wk = (in.value("g") + in.value("q")) * in.value("B") / 1000 / 2;
          const double M = wd * L * L / 8, sg = M / in.value("W");
          const double dl = 5 * wk * std::pow(L, 4) / (384 * in.value("E") * in.value("I") * c2);
          Result r; add(r, "踢面数", n, "级"); add(r, "踏面数", n - 1, "级"); add(r, "水平投影长 L", L, "mm");
          add(r, "斜长", Ls, "mm"); add(r, "倾角", std::atan2(H, L) * 180 / kPi, "°");
          add(r, "2h+b", 2 * in.value("h") + in.value("b"), "mm", "仅显示");
          add(r, "单根梯梁线荷载（设计值）", wd, "kN/m"); add(r, "单根梯梁线荷载（标准值）", wk, "kN/m");
          add(r, "跨中弯矩 M", M / 1e6, "kN·m"); add(r, "支座竖向反力", wd * L / 2000, "kN", "V=wL/2");
          add(r, "弯曲应力 σ=M/W", sg, "N/mm²"); add(r, "σ/f", sg / in.value("f"), "", "算术比值");
          add(r, "垂直梯梁挠度 δ", dl, "mm"); add(r, "δ/斜长", dl / Ls, "");
          add(r, "1/限值", 1 / in.value("lim"), "", dl / Ls <= 1 / in.value("lim") ? "按所填限值比较：δ/斜长 ≤ 1/限值 成立" : "按所填限值比较：δ/斜长 ≤ 1/限值 不成立");
          return r; };
      v << s; }
    { Spec s; s.name = "栏杆";
      s.notes << QStringLiteral("扶手按立柱间简支梁：M=qs²/8，δ=5qs⁴/(384EI)；立柱按底部固接悬臂：P=q·s，M=P·Hp，δ=P·Hp³/(3EI)；q 为使用者给出的水平线荷载设计值")
                << QStringLiteral("不含构造要求（高度、间距、防坠落）；本工具不内置栏杆荷载规范取值，也不判定是否符合规范");
      s.fields = {F("s", "立柱间距", "mm"), F("q", "水平线荷载 q", "kN/m"), F("E", "弹性模量", "N/mm²", "206000"),
                  F("Ir", "扶手惯性矩", "mm⁴"), F("Wr", "扶手截面模量", "mm³"),
                  F("Hp", "立柱高度", "mm"), F("Ip", "立柱惯性矩", "mm⁴"), F("Wp", "立柱截面模量", "mm³"), F("f", "所取强度设计值", "N/mm²")};
      s.run = [](const Inputs& in) {
          const double sp = in.value("s"), q = in.value("q"), E = in.value("E"), Hp = in.value("Hp");
          const double Mr = q * sp * sp / 8, P = q * sp, Mp = P * Hp;
          Result r; add(r, "扶手跨中弯矩", Mr / 1e6, "kN·m"); add(r, "扶手 σ", Mr / in.value("Wr"), "N/mm²");
          add(r, "扶手 σ/f", Mr / in.value("Wr") / in.value("f"), "");
          add(r, "扶手挠度", 5 * q * std::pow(sp, 4) / (384 * E * in.value("Ir")), "mm");
          add(r, "立柱顶集中力 P", P / 1000, "kN"); add(r, "立柱根部弯矩", Mp / 1e6, "kN·m");
          add(r, "立柱 σ", Mp / in.value("Wp"), "N/mm²"); add(r, "立柱 σ/f", Mp / in.value("Wp") / in.value("f"), "");
          add(r, "立柱顶位移", P * std::pow(Hp, 3) / (3 * E * in.value("Ip")), "mm"); return r; };
      v << s; }
    { Spec s; s.name = "组合视图 / DXF"; s.drawing = true;
      s.notes << QStringLiteral("侧立面线框示意：踢面、踏面、踏步前缘连线、扶手线与立柱；用于底图，不含梯梁截面、节点和板厚")
                << QStringLiteral("立柱自起点按间距布置，终点处补一根");
      s.fields = {F("H", "层高（总高）", "mm"), F("h", "级高", "mm"), F("b", "踏步宽", "mm"),
                  F("s", "立柱水平间距", "mm"), F("Hr", "栏杆竖向高度", "mm")};
      s.run = [](const Inputs& in) {
          QString e; const auto ln = stairLines(in, nullptr, &e); if (!e.isEmpty()) return fail(e);
          int n; double L; stairGeom(in, &n, &L, &e);
          Result r; add(r, "踢面数", n, "级"); add(r, "水平投影长", L, "mm"); add(r, "线段总数", ln.size(), "条"); return r; };
      v << s; }
    return v;
}
}  // namespace

QVector<Spec> specs(int kind) {
    switch (kind) {
    case Area: return areaSpecs();
    case Panel: return panelSpecs();
    case Curved: return curvedSpecs();
    case BoltSlip: return boltSpecs();
    case Stair: return stairSpecs();
    default: return {};
    }
}

Result evaluate(const Spec& s, const QMap<QString, QString>& text) {
    Inputs in; QStringList missing, bad;
    for (const Field& f : s.fields) {
        const QString t = text.value(f.key).trimmed();
        if (t.isEmpty()) { missing << f.label; continue; }
        bool ok = false; const double d = QLocale::c().toDouble(t, &ok);
        if (!ok || !std::isfinite(d) || d < 0 || (d == 0 && !f.allowZero)) { bad << f.label; continue; }
        in[f.key] = d;
    }
    if (!missing.isEmpty()) return fail(QStringLiteral("缺少输入：") + missing.join("、"));
    if (!bad.isEmpty()) return fail(QStringLiteral("输入无效（须为有限正数）：") + bad.join("、"));
    Result r = s.run(in);
    if (r.ok()) for (const Row& w : r.rows) if (!std::isfinite(w.value)) return fail(QStringLiteral("计算结果非有限值，请核对输入"));
    return r;
}

QString toTsv(const Result& r) {
    QString o = QStringLiteral("名称\t数值\t单位\t说明\n");
    for (const Row& w : r.rows)
        o += w.name + '\t' + QString::number(w.value, 'g', 10) + '\t' + w.unit + '\t' + w.note + '\n';
    return o;
}

QVector<QLineF> stairLines(const Inputs& in, QStringList* layers, QString* error) {
    int n = 0; double L = 0; QString e; QVector<QLineF> v; QStringList ly;
    if (!in.contains("s") || !in.contains("Hr") || in.value("s") <= 0 || in.value("Hr") <= 0) e = QStringLiteral("缺少立柱间距或栏杆高度");
    else if (!in.contains("H") || !in.contains("h") || !in.contains("b") || in.value("h") <= 0 || in.value("b") <= 0) e = QStringLiteral("缺少层高、级高或踏步宽");
    else if (stairGeom(in, &n, &L, &e) && L / in.value("s") > 5000) e = QStringLiteral("立柱数超过 5000，请核对立柱间距单位（应为 mm）");
    if (!e.isEmpty()) { if (error) *error = e; if (layers) layers->clear(); return {}; }
    const double h = in.value("h"), b = in.value("b"), s = in.value("s"), Hr = in.value("Hr"), k = h / b;
    auto put = [&](const char* layer, double x1, double y1, double x2, double y2) { v << QLineF(x1, y1, x2, y2); ly << layer; };
    for (int i = 0; i < n; ++i) put("STAIR", i * b, i * h, i * b, (i + 1) * h);
    for (int i = 0; i < n - 1; ++i) put("STAIR", i * b, (i + 1) * h, (i + 1) * b, (i + 1) * h);
    put("STAIR", 0, h, L, n * h);
    put("RAIL", 0, h + Hr, L, n * h + Hr);
    for (double x = 0; x < L - 1e-9; x += s) put("RAIL", x, h + k * x, x, h + k * x + Hr);
    put("RAIL", L, n * h, L, n * h + Hr);
    if (layers) *layers = ly;
    return v;
}

bool writeStairDxf(const Inputs& in, const QString& file, QString* error) {
    QStringList ly; QString e; const auto lines = stairLines(in, &ly, &e);
    if (lines.isEmpty()) { if (error) *error = e; return false; }
    QString o = QStringLiteral("0\nSECTION\n2\nENTITIES\n");
    for (int i = 0; i < lines.size(); ++i) {
        const QLineF& l = lines[i];
        o += QStringLiteral("0\nLINE\n8\n%1\n10\n%2\n20\n%3\n30\n0\n11\n%4\n21\n%5\n31\n0\n")
                 .arg(ly[i]).arg(l.x1(), 0, 'f', 3).arg(l.y1(), 0, 'f', 3).arg(l.x2(), 0, 'f', 3).arg(l.y2(), 0, 'f', 3);
    }
    o += QStringLiteral("0\nENDSEC\n0\nEOF\n");
    QSaveFile f(file);
    if (!f.open(QIODevice::WriteOnly) || f.write(o.toLatin1()) < 0 || !f.commit()) {
        if (error) { *error = QStringLiteral("无法写入 DXF：") + file; }
        return false;
    }
    return true;
}
}  // namespace calctools

namespace {
using namespace calctools;
class StairPreview : public QWidget {
public:
    QVector<QLineF> lines; QStringList layers;
    explicit StairPreview(QWidget* p = nullptr) : QWidget(p) { setMinimumHeight(220); setObjectName("preview"); }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter g(this); g.setRenderHint(QPainter::Antialiasing); g.fillRect(rect(), Qt::white);
        if (lines.isEmpty()) { g.drawText(rect(), Qt::AlignCenter, QStringLiteral("输入尺寸并点击“计算”后显示")); return; }
        QRectF box; for (const QLineF& l : lines) box = box.united(QRectF(l.p1(), l.p2()).normalized());
        const double k = qMin((width() - 30) / qMax(box.width(), 1.0), (height() - 30) / qMax(box.height(), 1.0));
        auto map = [&](QPointF p) { return QPointF(15 + (p.x() - box.left()) * k, height() - 15 - (p.y() - box.top()) * k); };
        for (int i = 0; i < lines.size(); ++i) {
            g.setPen(QPen(layers.value(i) == "RAIL" ? QColor(200, 60, 40) : QColor(30, 60, 120), 1.4));
            g.drawLine(map(lines[i].p1()), map(lines[i].p2()));
        }
    }
};
QString fmt(double v) { return QString::number(v, 'g', 8); }
}  // namespace

QWidget* createCalculationTool(int kind, QWidget* owner) {
    const QVector<Spec> sp = specs(kind);
    if (sp.isEmpty()) return nullptr;
    static const QStringList titles{"面积重量计算器", "围护板计算器", "曲线构件重量", "高强度螺栓抗滑移", "钢楼梯 / 栏杆校核"};
    auto* dlg = new QDialog(owner);
    dlg->setAttribute(Qt::WA_DeleteOnClose); dlg->setWindowTitle(titles.value(kind)); dlg->resize(940, 640);
    auto* combo = new QComboBox; combo->setObjectName("subkind");
    for (const Spec& s : sp) combo->addItem(s.name);
    auto* formHost = new QWidget; auto* form = new QFormLayout(formHost);
    auto* table = new QTableWidget(0, 4); table->setObjectName("results");
    table->setHorizontalHeaderLabels({"名称", "数值", "单位", "说明"});
    table->horizontalHeader()->setStretchLastSection(true); table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    auto* preview = new StairPreview; auto* status = new QLabel; status->setObjectName("status"); status->setWordWrap(true);
    auto* notes = new QLabel; notes->setObjectName("notes"); notes->setWordWrap(true); notes->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto* calc = new QPushButton("计算"); calc->setObjectName("compute");
    auto* copy = new QPushButton("复制结果"); copy->setObjectName("copy");
    auto* dxf = new QPushButton("导出 DXF…"); dxf->setObjectName("dxf");
    auto last = QSharedPointer<Result>::create();
    auto* split = new QSplitter; auto* right = new QWidget; auto* rl = new QVBoxLayout(right);
    rl->addWidget(table, 3); rl->addWidget(preview, 2); rl->addWidget(status);
    split->addWidget(formHost); split->addWidget(right); split->setStretchFactor(1, 1);
    auto* top = new QVBoxLayout(dlg); auto* bar = new QHBoxLayout;
    bar->addWidget(calc); bar->addWidget(copy); bar->addWidget(dxf); bar->addStretch();
    top->addWidget(combo); top->addWidget(split, 1); top->addWidget(notes); top->addLayout(bar);

    auto texts = [form, formHost]() {
        QMap<QString, QString> t;
        for (auto* e : formHost->findChildren<QLineEdit*>()) t[e->objectName()] = e->text();
        Q_UNUSED(form); return t; };
    auto rebuild = [=]() {
        const Spec& s = sp[combo->currentIndex()];
        while (form->rowCount()) form->removeRow(0);
        for (const Field& f : s.fields) {
            auto* e = new QLineEdit(f.preset); e->setObjectName(f.key);
            e->setPlaceholderText(f.preset.isEmpty() ? QStringLiteral("必填") : QString());
            form->addRow(f.label + (f.unit.isEmpty() ? QString() : " (" + f.unit + ")"), e);
        }
        notes->setText(QStringLiteral("依据与边界：\n· ") + s.notes.join("\n· "));
        preview->setVisible(s.drawing); dxf->setVisible(s.drawing);
        table->setRowCount(0); status->clear(); *last = Result(); preview->lines.clear(); preview->update();
    };
    QObject::connect(combo, &QComboBox::currentIndexChanged, dlg, rebuild);
    QObject::connect(calc, &QPushButton::clicked, dlg, [=]() {
        const Spec& s = sp[combo->currentIndex()]; const auto t = texts();
        *last = evaluate(s, t); table->setRowCount(0); preview->lines.clear(); preview->layers.clear();
        if (!last->ok()) { status->setText(QStringLiteral("<span style='color:#b00020'>%1</span>").arg(last->error.toHtmlEscaped())); preview->update(); return; }
        status->setText(QStringLiteral("计算完成。结果仅为按所填数据的算术值。"));
        for (const Row& w : last->rows) {
            const int r = table->rowCount(); table->insertRow(r);
            const QStringList c{w.name, fmt(w.value), w.unit, w.note};
            for (int i = 0; i < 4; ++i) table->setItem(r, i, new QTableWidgetItem(c[i]));
        }
        table->resizeColumnsToContents();
        if (s.drawing) {
            Inputs in; for (const Field& f : s.fields) in[f.key] = QLocale::c().toDouble(t.value(f.key));
            preview->lines = stairLines(in, &preview->layers); preview->update();
        }
    });
    QObject::connect(copy, &QPushButton::clicked, dlg, [=]() {
        if (!last->ok() || last->rows.isEmpty()) return;
        dlg->setProperty("copiedText", toTsv(*last));  // 剪贴板可能被其他程序占用，属性供自检读取
        QGuiApplication::clipboard()->setText(toTsv(*last)); });
    QObject::connect(dxf, &QPushButton::clicked, dlg, [=]() {
        const Spec& s = sp[combo->currentIndex()]; Inputs in;
        const Result chk = evaluate(s, texts());  // 与“计算”同一套输入校验
        if (!chk.ok()) { status->setText(chk.error); return; }
        for (const Field& f : s.fields) in[f.key] = QLocale::c().toDouble(texts().value(f.key));
        const QString file = QFileDialog::getSaveFileName(dlg, "导出 DXF 底图", "stair-base.dxf", "DXF (*.dxf)");
        if (file.isEmpty()) return;
        QString e; if (!writeStairDxf(in, file, &e)) status->setText(e); else status->setText("已导出：" + file);
    });
    rebuild();
    return dlg;
}
namespace {
// 期望值由独立的 Python 脚本按教科书公式手算（不调用本文件函数），见 docs/计算工具说明.md
struct Want { QString name, unit; double v; };
struct Case { int kind; QString spec, in; QVector<Want> want; QString shot; };
struct Bad { int kind; QString spec, in, err; };
const QString S = QStringLiteral("rho=7850;n=1;");

QVector<Case> goodCases() {
    return {
    {0, "自定义展开面积", "A=12.5;t=3;rho=7850;n=1", {{"单件重量", "kg", 294.375}}, ""},
    {0, "钢板 / 花纹板", "L=2000;B=1000;t=10;rho=7850;extra=0;n=1", {{"单件重量", "kg", 157.0}}, ""},
    {0, "钢板 / 花纹板", "L=2000;B=1000;t=10;rho=7850;extra=5;n=1", {{"单件重量", "kg", 167.0}}, ""},
    {0, "圆管", "D=114;t=4;L=6000;rho=7850;n=3", {{"截面积", "mm²", 1382.300767579509}, {"单位长度重量", "kg/m", 10.851061025499146},
        {"总重量", "kg", 195.31909845898468}, {"外表面积（单件）", "m²", 2.148849375055418}}, "area-round-tube"},
    {0, "方管", "a=100;t=5;R=0;L=6000;" + S, {{"单位长度重量", "kg/m", 14.915}}, ""},
    {0, "方管", "a=100;t=5;R=10;L=6000;" + S, {{"截面积", "mm²", 1835.6194490192345}, {"单位长度重量", "kg/m", 14.409612674800991}}, ""},
    {0, "矩形管", "H=200;B=100;t=6;R=0;L=3000;rho=7850;n=2", {{"总重量", "kg", 162.7776}, {"外表面积（单件）", "m²", 1.8}}, ""},
    {0, "角钢", "a=100;b=100;t=10;L=1000;" + S, {{"单位长度重量", "kg/m", 14.915}}, ""},
    {0, "圆钢", "D=20;L=1000;rho=7850;n=4", {{"单位长度重量", "kg/m", 2.4661502330679874}, {"总重量", "kg", 9.864600932271951}}, ""},
    {0, "方钢", "a=20;L=1000;" + S, {{"单位长度重量", "kg/m", 3.14}}, ""},
    {1, "压型单板", "t=0.8;dev=1250;eff=1000;rho=7850;A=100", {{"单位面积重量", "kg/m²", 7.85}, {"总重量", "kg", 785.0}}, ""},
    {1, "金属夹芯板", "t1=0.5;dev1=1250;eff1=1000;t2=0.5;dev2=1000;eff2=1000;tc=75;rhoc=14;rho=7850;A=10",
        {{"单位面积重量", "kg/m²", 9.88125}}, "panel-sandwich"},
    {1, "玻璃钢采光板", "t=1.5;dev=1200;eff=1000;rho=1800;A=40", {{"总重量", "kg", 129.6}}, ""},
    {1, "直立锁边板", "t=0.7;dev=600;eff=400;rho=7850;extra=0.3;A=1", {{"单位面积重量", "kg/m²", 8.5425}}, ""},
    {1, "PC阳光板", "w=2;A=30", {{"总重量", "kg", 60.0}}, ""},
    {1, "天沟 / 泛水", "dev=800;t=2;L=50;rho=7850", {{"总重量", "kg", 628.0}}, ""},
    {2, "半径 + 圆心角", "R=5000;deg=90;um=20;n=1", {{"单件重量", "kg", 157.07963267948966}}, ""},
    {2, "弦长 + 矢高", "c=10000;f=2000;um=20;n=2", {{"半径", "mm", 7250.0}, {"圆心角", "°", 87.20563794540725},
        {"中心线弧长", "mm", 11034.684936258582}, {"总重量", "kg", 441.3873974503433}}, "curved-chord-rise"},
    {3, "高强度螺栓抗滑移", "k=1;nf=1;mu=0.45;P=155;nb=6;N=300", {{"单栓抗滑移承载力 Nv", "kN", 62.775},
        {"螺栓组承载力 n·Nv", "kN", 376.65}, {"N/(n·Nv)", "", 0.796495420151334}}, "bolt-slip"},
    {4, "楼梯梯段", "H=3000;h=150;b=260;B=1200;g=1;q=3.5;gG=1.3;gQ=1.5;E=206000;I=17800000;W=178000;f=215;lim=250",
        {{"水平投影长 L", "mm", 4940.0}, {"斜长", "mm", 5779.584760170924}, {"倾角", "°", 31.26978449168357},
         {"跨中弯矩 M", "kN·m", 11.9882685}, {"支座竖向反力", "kN", 9.7071}, {"弯曲应力 σ=M/W", "N/mm²", 67.34982303370786},
         {"垂直梯梁挠度 δ", "mm", 7.815603051195866}, {"δ/斜长", "", 0.0013522776073907304}}, "stair-stringer"},
    {4, "栏杆", "s=1500;q=0.5;E=206000;Ir=120000;Wr=4000;Hp=1100;Ip=300000;Wp=8000;f=215",
        {{"扶手跨中弯矩", "kN·m", 0.140625}, {"扶手 σ", "N/mm²", 35.15625}, {"扶手挠度", "mm", 1.3332922481796117},
         {"立柱顶集中力 P", "kN", 0.75}, {"立柱根部弯矩", "kN·m", 0.825}, {"立柱 σ", "N/mm²", 103.125},
         {"立柱顶位移", "mm", 5.384304207119741}}, "stair-handrail"},
    {4, "组合视图 / DXF", "H=3000;h=150;b=260;s=1500;Hr=1100", {{"踢面数", "级", 20}, {"水平投影长", "mm", 4940}, {"线段总数", "条", 46}}, "stair-combined"},
    };
}
QVector<Bad> badCases() {
    return {
    {3, "高强度螺栓抗滑移", "k=1;nf=1;mu=0.45;P=;nb=6;N=300", "缺少输入"},
    {0, "圆钢", "D=0;L=1000;" + S, "输入无效"},
    {0, "圆钢", "D=-5;L=1000;" + S, "输入无效"},
    {0, "圆钢", "D=abc;L=1000;" + S, "输入无效"},
    {0, "圆钢", "D=20;L=1000;rho=7850;n=1.5", "数量必须为整数"},
    {0, "圆管", "D=8;t=4;L=1000;" + S, "壁厚过大"},
    {0, "方管", "a=100;t=5;R=3;L=1000;" + S, "外圆角"},
    {0, "方管", "a=100;t=5;R=;L=1000;" + S, "缺少输入"},
    {1, "压型单板", "t=0.8;dev=900;eff=1000;rho=7850;A=1", "展开宽度"},
    {2, "半径 + 圆心角", "R=5000;deg=400;um=20;n=1", "360"},
    {3, "高强度螺栓抗滑移", "k=1;nf=1;mu=1.5;P=155;nb=6;N=300", "不应大于 1"},
    {3, "高强度螺栓抗滑移", "k=10;nf=1;mu=0.45;P=155;nb=6;N=300", "孔型系数"},
    {4, "组合视图 / DXF", "H=3000;h=0.15;b=260;s=1500;Hr=1100", "超过 1000"},
    {4, "组合视图 / DXF", "H=3000;h=150;b=260;s=0.1;Hr=1100", "立柱数"},
    {4, "楼梯梯段", "H=3000;h=170;b=260;B=1200;g=1;q=3.5;gG=1.3;gQ=1.5;E=206000;I=1e7;W=1e5;f=215;lim=250", "不是整数"},
    {4, "组合视图 / DXF", "H=150;h=150;b=260;s=1500;Hr=1100", "不足 2 级"},
    };
}
QMap<QString, QString> parseInputs(const QString& s) {
    QMap<QString, QString> m;
    for (const QString& p : s.split(';')) { const int i = p.indexOf('='); if (i > 0) m[p.left(i)] = p.mid(i + 1); }
    return m;
}
struct Tally {
    QStringList fails; int checks = 0;
    void ok(bool c, const QString& what) { ++checks; if (!c) fails << what; }
    void eq(const QString& what, double got, double want) {
        ok(std::fabs(got - want) <= 1e-9 * qMax(1.0, std::fabs(want)),
           QStringLiteral("%1: 得到 %2 期望 %3").arg(what).arg(got, 0, 'g', 15).arg(want, 0, 'g', 15));
    }
};
const Spec* findSpec(const QVector<Spec>& v, const QString& n, int* idx) {
    for (int i = 0; i < v.size(); ++i) if (v[i].name == n) { *idx = i; return &v[i]; }
    return nullptr;
}
void driveUi(const Case& c, const Result& want, const QString& outDir, Tally& t) {
    const QString tag = QStringLiteral("UI[%1/%2]").arg(c.kind).arg(c.spec);
    auto* dlg = createCalculationTool(c.kind, nullptr);
    t.ok(dlg != nullptr, tag + " 创建失败"); if (!dlg) return;
    const QVector<Spec> sp = specs(c.kind); int idx = 0; findSpec(sp, c.spec, &idx);
    auto* combo = dlg->findChild<QComboBox*>("subkind"); combo->setCurrentIndex(idx);
    const auto in = parseInputs(c.in);
    for (auto it = in.cbegin(); it != in.cend(); ++it) {
        auto* e = dlg->findChild<QLineEdit*>(it.key()); t.ok(e != nullptr, tag + " 缺少输入框 " + it.key());
        if (e) e->setText(it.value());
    }
    dlg->findChild<QPushButton*>("compute")->click();
    auto* table = dlg->findChild<QTableWidget*>("results");
    t.ok(table->rowCount() == want.rows.size(), tag + " 结果行数与引擎不一致");
    if (table->rowCount() > 0) t.ok(table->item(0, 1)->text() == fmt(want.rows[0].value), tag + " 首行显示值不一致");
    dlg->findChild<QPushButton*>("copy")->click();
    t.ok(dlg->property("copiedText").toString().startsWith(QStringLiteral("名称\t数值")), tag + " 复制结果未生效");
    if (c.spec == "组合视图 / DXF") {
        auto* pv = static_cast<StairPreview*>(dlg->findChild<QWidget*>("preview"));
        t.ok(pv && pv->lines.size() == 46, tag + " 预览线段数应为 46");
    }
    if (!outDir.isEmpty() && !c.shot.isEmpty()) {
        dlg->show(); QCoreApplication::processEvents();
        t.ok(dlg->grab().save(QDir(outDir).filePath(c.shot + ".png")), tag + " 截图保存失败");
    }
    dlg->close();
}
}  // namespace
bool runCalculationToolChecks(QString* error, const QString& outDir) {
    Tally t;
    if (!qobject_cast<QApplication*>(QCoreApplication::instance())) {
        if (error) { *error = QStringLiteral("需要 QApplication 才能自检界面"); }
        return false;
    }
    if (!outDir.isEmpty()) QDir().mkpath(outDir);
    for (const Case& c : goodCases()) {
        int idx = 0; const QVector<Spec> sp = specs(c.kind); const Spec* s = findSpec(sp, c.spec, &idx);
        t.ok(s != nullptr, "未找到工具 " + c.spec); if (!s) continue;
        const Result r = evaluate(*s, parseInputs(c.in));
        t.ok(r.ok(), c.spec + " 计算失败：" + r.error); if (!r.ok()) continue;
        for (const Want& w : c.want) {
            const Row* hit = nullptr;
            for (const Row& row : r.rows) if (row.name == w.name && row.unit == w.unit) { hit = &row; break; }
            t.ok(hit != nullptr, c.spec + " 缺少结果行 " + w.name + "[" + w.unit + "]");
            if (hit) t.eq(c.spec + "/" + w.name, hit->value, w.v);
        }
        driveUi(c, r, outDir, t);
    }
    for (const Bad& b : badCases()) {
        int idx = 0; const QVector<Spec> sp = specs(b.kind); const Spec* s = findSpec(sp, b.spec, &idx);
        t.ok(s != nullptr, "未找到工具 " + b.spec); if (!s) continue;
        const Result r = evaluate(*s, parseInputs(b.in));
        t.ok(!r.ok() && r.error.contains(b.err), b.spec + " [" + b.in + "] 应报错“" + b.err + "”，实际：" + (r.ok() ? "通过" : r.error));
    }
    { // 界面空输入：必须报缺失，不得补默认值
        auto* dlg = createCalculationTool(BoltSlip, nullptr);
        dlg->findChild<QPushButton*>("compute")->click();
        t.ok(dlg->findChild<QLabel*>("status")->text().contains("缺少输入") && dlg->findChild<QTableWidget*>("results")->rowCount() == 0, "界面空输入应提示缺少输入");
        dlg->close();
    }
    { // DXF：线段数、起点坐标；写入临时目录，不碰工程
        QTemporaryDir tmp; QString e;
        const Inputs in{{"H", 3000}, {"h", 150}, {"b", 260}, {"s", 1500}, {"Hr", 1100}};
        const QString f = QDir(outDir.isEmpty() ? tmp.path() : outDir).filePath("stair-base.dxf");
        t.ok(writeStairDxf(in, f, &e), "DXF 写入失败：" + e);
        QFile fi(f); fi.open(QIODevice::ReadOnly); const QString d = QString::fromLatin1(fi.readAll());
        t.ok(d.count("\nLINE\n") == 46 && d.endsWith("0\nEOF\n"), "DXF 线段数应为 46 且以 EOF 结束");
        t.ok(d.contains("\n10\n0.000\n20\n0.000\n30\n0\n11\n0.000\n21\n150.000\n"), "DXF 首条踢面坐标不符");
        t.ok(!writeStairDxf({{"H", 3000}, {"h", 170}, {"b", 260}, {"s", 1500}, {"Hr", 1100}}, tmp.filePath("x.dxf"), &e) && !QFile::exists(tmp.filePath("x.dxf")), "非法输入不应生成 DXF");
    }
    if (!outDir.isEmpty()) {
        QJsonObject o{{"ok", t.fails.isEmpty()}, {"checks", t.checks}, {"failures", QJsonArray::fromStringList(t.fails)}};
        QSaveFile f(QDir(outDir).filePath("calculation-tools-check.json"));
        if (f.open(QIODevice::WriteOnly)) { f.write(QJsonDocument(o).toJson()); f.commit(); }
    }
    if (error) *error = t.fails.isEmpty() ? QStringLiteral("%1 项检查通过").arg(t.checks) : t.fails.join("; ");
    return t.fails.isEmpty();
}
