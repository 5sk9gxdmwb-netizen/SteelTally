#pragma once
#include <QObject>
#include <QByteArray>
#include <QUrl>
#include <QString>
class QMainWindow;
class QLabel;
class QAction;
namespace updater {
struct Release {
    QString version, sha256, notes, catalogVersion, rulesVersion, projectSchema;
    QUrl url;
    qint64 size = 0;
};
bool parseVersion(const QString&, int parts[3]);
int compareVersions(const QString&, const QString&);
bool verifyEnvelope(const QByteArray& envelope, const QByteArray& publicBlob, Release*, QString*);
bool verifyFile(const QString&, const Release&, QString*);
// True only when the build embedded both an HTTPS manifest URL and a usable public key.
bool configured();
// Handles only dedicated updater command modes; -1 means ordinary application startup.
int commandMode(const QStringList& arguments);
void acknowledgeHealth(const QStringList& arguments);
void attach(QMainWindow*, QLabel*, QAction*);
}
