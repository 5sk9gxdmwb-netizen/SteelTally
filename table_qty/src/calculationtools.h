#pragma once
#include <QtWidgets>
#include <functional>

// 独立计算工具：面积重量 / 围护板 / 曲线构件 / 螺栓抗滑移 / 楼梯栏杆。
// 不读写工程数据，不访问网络，不使用 Q_OBJECT。公式与引用边界见 docs/计算工具说明.md。
namespace calctools {
enum Kind { Area = 0, Panel = 1, Curved = 2, BoltSlip = 3, Stair = 4 };

struct Field {                    // preset 为界面可见的预填值；空表示必须由用户输入
    QString key, label, unit, preset;
    bool allowZero = false;       // 仅在 0 有明确含义时允许（如圆角 0=尖角）
};
struct Row { QString name; double value = 0; QString unit, note; };
struct Result {
    QString error;                // 非空即失败，rows 无效
    QVector<Row> rows;
    bool ok() const { return error.isEmpty(); }
};
using Inputs = QMap<QString, double>;
struct Spec {
    QString name;
    QVector<Field> fields;
    QStringList notes;            // 公式依据与适用边界，显示在结果下方
    bool drawing = false;         // 楼梯组合视图：显示预览并允许导出 DXF
    std::function<Result(const Inputs&)> run;
};

QVector<Spec> specs(int kind);
// 文本 -> 数值：缺失、非数、非有限值、负值（及不允许的 0）一律报错，不补默认值。
Result evaluate(const Spec& s, const QMap<QString, QString>& text);
QString toTsv(const Result& r);
QVector<QLineF> stairLines(const Inputs& in, QStringList* layers = nullptr, QString* error = nullptr);
bool writeStairDxf(const Inputs& in, const QString& file, QString* error);
}

// kind 取 calctools::Kind；返回非模态 QDialog（父对象为 owner，关闭即删除），由调用方 show()。
QWidget* createCalculationTool(int kind, QWidget* owner);
// 纯算术自检（手算对照 + 边界）；outDir 非空时写出 calculation-tools-check.json 与各工具截图。
bool runCalculationToolChecks(QString* error, const QString& outDir);
