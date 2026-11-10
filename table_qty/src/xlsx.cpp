#include "xlsx.h"
#include <QFile>
#include <QRegularExpression>
#include <QMap>

static void put16(QByteArray& b, quint16 v) { b.append(char(v & 0xFF)); b.append(char((v >> 8) & 0xFF)); }
static void put32(QByteArray& b, quint32 v) {
    b.append(char(v & 0xFF)); b.append(char((v >> 8) & 0xFF));
    b.append(char((v >> 16) & 0xFF)); b.append(char((v >> 24) & 0xFF));
}

static quint32 crc32(const QByteArray& data) {
    static quint32 table[256];
    static bool init = false;
    if (!init) {
        for (quint32 i = 0; i < 256; ++i) {
            quint32 c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        init = true;
    }
    quint32 crc = 0xFFFFFFFFu;
    for (unsigned char b : data) crc = table[(crc ^ b) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

static QString xmlEscape(const QString& s) {
    QString o;
    o.reserve(s.size());
    for (QChar c : s) {
        switch (c.unicode()) {
        case '&': o += QStringLiteral("&amp;"); break;
        case '<': o += QStringLiteral("&lt;"); break;
        case '>': o += QStringLiteral("&gt;"); break;
        case '"': o += QStringLiteral("&quot;"); break;
        case '\'': o += QStringLiteral("&apos;"); break;
        default: o += c;
        }
    }
    return o;
}

static QString colLetter(int idx) {
    QString s;
    int n = idx + 1;
    while (n > 0) {
        int rem = (n - 1) % 26;
        s.prepend(QChar('A' + rem));
        n = (n - 1) / 26;
    }
    return s;
}

static bool isNumber(const QString& s, double& out) {
    static QRegularExpression re(QStringLiteral("^-?\\d+(?:\\.\\d+)?$"));
    if (!re.match(s).hasMatch()) return false;
    bool ok = false;
    out = s.toDouble(&ok);
    return ok;
}

static QString sheetXml(const XlsxSheet& sh) {
    QString x;
    x += QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n");
    x += QStringLiteral("<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">");

    int maxCols = 0;
    for (const auto& r : sh.rows) maxCols = qMax(maxCols, r.size());
    QVector<int> widths(maxCols, 8);
    for (const auto& r : sh.rows)
        for (int c = 0; c < r.size(); ++c) widths[c] = qMax(widths[c], qMin(50, r[c].size() + 2));

    x += QStringLiteral("<cols>");
    for (int c = 0; c < maxCols; ++c)
        x += QStringLiteral("<col min=\"%1\" max=\"%1\" width=\"%2\" customWidth=\"1\"/>").arg(c + 1).arg(widths[c]);
    x += QStringLiteral("</cols><sheetData>");

    for (int r = 0; r < sh.rows.size(); ++r) {
        const QStringList& row = sh.rows[r];
        int style = 0;
        if (sh.titleRows.contains(r)) style = 2;
        else if (sh.headerRows.contains(r)) style = 1;
        x += QStringLiteral("<row r=\"%1\">").arg(r + 1);
        for (int c = 0; c < row.size(); ++c) {
            QString ref = colLetter(c) + QString::number(r + 1);
            double num = 0;
            if (isNumber(row[c], num)) {
                x += QStringLiteral("<c r=\"%1\"").arg(ref);
                const int s = style ? style : 3; // 3 = 数字右对齐
                x += QStringLiteral(" s=\"%1\"").arg(s);
                x += QStringLiteral("><v>%1</v></c>").arg(QString::number(num, 'g', 15));
            } else if (!row[c].isEmpty()) {
                x += QStringLiteral("<c r=\"%1\"").arg(ref);
                if (style) x += QStringLiteral(" s=\"%1\"").arg(style);
                x += QStringLiteral(" t=\"inlineStr\"><is><t xml:space=\"preserve\">%1</t></is></c>").arg(xmlEscape(row[c]));
            }
        }
        x += QStringLiteral("</row>");
    }
    x += QStringLiteral("</sheetData></worksheet>");
    return x;
}

static QString sanitizeSheetName(QString name) {
    static const QString bad = QStringLiteral(":\\/?*[]");
    for (QChar& c : name) if (bad.contains(c)) c = QChar('_');
    if (name.size() > 31) name = name.left(31);
    if (name.isEmpty()) name = QStringLiteral("Sheet");
    return name;
}

bool writeXlsx(const QString& path, const QVector<XlsxSheet>& sheets, QString* err) {
    if (sheets.isEmpty()) { if (err) *err = QStringLiteral("无工作表"); return false; }

    QVector<QPair<QString, QByteArray>> files; // name -> data (store)

    QString ct = QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
        "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
        "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
        "<Override PartName=\"/xl/workbook.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/>"
        "<Override PartName=\"/xl/styles.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml\"/>");
    for (int i = 0; i < sheets.size(); ++i)
        ct += QStringLiteral("<Override PartName=\"/xl/worksheets/sheet%1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/>").arg(i + 1);
    ct += QStringLiteral("</Types>");
    files.append({ QStringLiteral("[Content_Types].xml"), ct.toUtf8() });

    files.append({ QStringLiteral("_rels/.rels"), QByteArrayLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"xl/workbook.xml\"/>"
        "</Relationships>") });

    QString wb = QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<workbook xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
        "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\"><sheets>");
    for (int i = 0; i < sheets.size(); ++i)
        wb += QStringLiteral("<sheet name=\"%1\" sheetId=\"%2\" r:id=\"rId%2\"/>").arg(xmlEscape(sanitizeSheetName(sheets[i].name))).arg(i + 1);
    wb += QStringLiteral("</sheets></workbook>");
    files.append({ QStringLiteral("xl/workbook.xml"), wb.toUtf8() });

    QString rels = QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">");
    for (int i = 0; i < sheets.size(); ++i)
        rels += QStringLiteral("<Relationship Id=\"rId%1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet\" Target=\"worksheets/sheet%1.xml\"/>").arg(i + 1);
    rels += QStringLiteral("<Relationship Id=\"rIdStyles\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" Target=\"styles.xml\"/>");
    rels += QStringLiteral("</Relationships>");
    files.append({ QStringLiteral("xl/_rels/workbook.xml.rels"), rels.toUtf8() });

    QString styles = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<styleSheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">"
        "<fonts count=\"3\">"
        "<font><sz val=\"11\"/><name val=\"宋体\"/></font>"
        "<font><b/><sz val=\"11\"/><name val=\"宋体\"/></font>"
        "<font><b/><sz val=\"14\"/><name val=\"宋体\"/></font>"
        "</fonts>"
        "<fills count=\"2\"><fill><patternFill patternType=\"none\"/></fill><fill><patternFill patternType=\"gray125\"/></fill></fills>"
        "<borders count=\"1\"><border><left/><right/><top/><bottom/><diagonal/></border></borders>"
        "<cellStyleXfs count=\"1\"><xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\"/></cellStyleXfs>"
        "<cellXfs count=\"4\">"
        "<xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\"/>"
        "<xf numFmtId=\"0\" fontId=\"1\" fillId=\"0\" borderId=\"0\" xfId=\"0\" applyFont=\"1\"/>"
        "<xf numFmtId=\"0\" fontId=\"2\" fillId=\"0\" borderId=\"0\" xfId=\"0\" applyFont=\"1\"/>"
        "<xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\" applyAlignment=\"1\"><alignment horizontal=\"right\" vertical=\"center\"/></xf>"
        "</cellXfs>"
        "<cellStyles count=\"1\"><cellStyle name=\"Normal\" xfId=\"0\" builtinId=\"0\"/></cellStyles>"
        "</styleSheet>");
    files.append({ QStringLiteral("xl/styles.xml"), styles.toUtf8() });

    for (int i = 0; i < sheets.size(); ++i)
        files.append({ QStringLiteral("xl/worksheets/sheet%1.xml").arg(i + 1), sheetXml(sheets[i]).toUtf8() });

    QByteArray out;
    QVector<quint32> offsets;
    for (const auto& f : files) {
        offsets.append(out.size());
        QByteArray name = f.first.toUtf8();
        QByteArray data = f.second;
        quint32 crc = crc32(data);
        put32(out, 0x04034b50u); put16(out, 20); put16(out, 0); put16(out, 0);
        put16(out, 0); put16(out, 0x0021);
        put32(out, crc); put32(out, data.size()); put32(out, data.size());
        put16(out, name.size()); put16(out, 0);
        out.append(name); out.append(data);
    }
    int cdStart = out.size();
    for (int i = 0; i < files.size(); ++i) {
        QByteArray name = files[i].first.toUtf8();
        QByteArray data = files[i].second;
        quint32 crc = crc32(data);
        put32(out, 0x02014b50u); put16(out, 20); put16(out, 20); put16(out, 0); put16(out, 0);
        put16(out, 0); put16(out, 0x0021);
        put32(out, crc); put32(out, data.size()); put32(out, data.size());
        put16(out, name.size()); put16(out, 0); put16(out, 0); put16(out, 0); put16(out, 0);
        put32(out, 0); put32(out, offsets[i]);
        out.append(name);
    }
    int cdSize = out.size() - cdStart;
    put32(out, 0x06054b50u); put16(out, 0); put16(out, 0);
    put16(out, files.size()); put16(out, files.size());
    put32(out, cdSize); put32(out, cdStart); put16(out, 0);

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) { if (err) *err = QStringLiteral("无法写入文件"); return false; }
    f.write(out);
    f.close();
    return true;
}
