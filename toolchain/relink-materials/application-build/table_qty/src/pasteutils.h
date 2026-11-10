#pragma once
#include <QStringList>
#include <QVector>

// Excel TSV, including quoted tabs/newlines and doubled quotes.
inline bool parseTsv(QString text, QVector<QStringList>& rows, QString* error) {
    auto fail = [&](const QString& why) { if(error) *error=why; return false; };
    if(text.isEmpty() || text.size()>1000000) return fail(QStringLiteral("剪贴板为空或超过 1 MB。"));
    text.replace("\r\n","\n").replace('\r','\n');
    QString cell; QStringList row;
    bool quoted=false, ended=false;
    for(int i=0;i<text.size();++i) {
        const QChar c=text[i];
        if(quoted) {
            if(c=='"') {
                if(i+1<text.size() && text[i+1]=='"') {cell+='"'; ++i;}
                else {quoted=false; ended=true;}
            } else cell+=c;
        } else if(c=='\t' || c=='\n') {
            row<<cell; cell.clear(); ended=false;
            if(c=='\n') {rows<<row; row.clear();}
        } else if(c=='"' && cell.isEmpty() && !ended) quoted=true;
        else {
            if(ended) return fail(QStringLiteral("带引号单元格后存在无效字符。"));
            cell+=c;
        }
        if(rows.size()>10000 || row.size()>100) return fail(QStringLiteral("粘贴矩阵超过 10,000 行或 100 列。"));
    }
    if(quoted) return fail(QStringLiteral("单元格引号未闭合。"));
    if(!cell.isEmpty() || !row.isEmpty() || ended) {row<<cell; rows<<row;}
    if(rows.isEmpty()) return fail(QStringLiteral("没有可粘贴数据。"));
    const int width=rows.first().size();
    for(const auto& r:rows) if(r.size()!=width) return fail(QStringLiteral("每行列数必须一致。"));
    return true;
}
