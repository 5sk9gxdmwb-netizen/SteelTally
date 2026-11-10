#include "updater.h"
#if __has_include("updateconfig.h")
#include "updateconfig.h"
#else
#define STEELTALLY_UPDATE_URL ""
#define STEELTALLY_UPDATE_PUBLIC_KEY ""
#endif
#include <QApplication>
#include <QMainWindow>
#include <QLabel>
#include <QAction>
#include <QMessageBox>
#include <QProgressDialog>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QSaveFile>
#include <QStandardPaths>
#include <QRegularExpression>
#include <QTimer>
#include <QDateTime>
#include <QProcess>
#include <QUuid>
#include <QSet>
#include <QEventLoop>
#include <QElapsedTimer>
#include <QNetworkCookieJar>
#include <QNetworkCookie>
#include <functional>
#include <QtEndian>
#include <cmath>
#include <cstdio>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#endif

namespace updater {
namespace {
constexpr qint64 MaxEnvelope = 96 * 1024;
constexpr qint64 MaxDownload = 512LL * 1024 * 1024;
const QString StagePrefix = QStringLiteral("SteelTally-update-");
QString uniqueId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
QByteArray trustedKey() { return QByteArray::fromBase64(QByteArray(STEELTALLY_UPDATE_PUBLIC_KEY)); }
bool fail(QString* error, const QString& message) { if (error) *error = message; return false; }
bool https(const QUrl& url) {
    return url.isValid() && url.scheme() == QStringLiteral("https") && !url.host().isEmpty()
        && url.userInfo().isEmpty() && !url.hasFragment() && url.port(443) == 443;
}
// Qt's JSON object parser accepts duplicate keys; reject them before interpretation.
bool uniqueJsonKeys(const QByteArray& raw) {
    QList<QSet<QString>> objects;
    for (qsizetype i = 0; i < raw.size(); ++i) {
        if (raw[i] == '{') objects.append(QSet<QString>{});
        else if (raw[i] == '}') { if (objects.isEmpty()) return false; objects.removeLast(); }
        else if (raw[i] == '"') {
            const qsizetype begin = i++;
            for (; i < raw.size(); ++i) {
                if (raw[i] == '\\') { ++i; continue; }
                if (raw[i] == '"') break;
            }
            if (i >= raw.size()) return false;
            qsizetype next = i + 1;
            while (next < raw.size() && (raw[next] == ' ' || raw[next] == '\t' || raw[next] == '\r' || raw[next] == '\n')) ++next;
            if (next < raw.size() && raw[next] == ':') {
                if (objects.isEmpty()) return false;
                const auto name = QJsonDocument::fromJson("[" + raw.mid(begin, i - begin + 1) + "]").array();
                if (name.size() != 1 || !name[0].isString() || objects.last().contains(name[0].toString())) return false;
                objects.last().insert(name[0].toString());
            }
        }
    }
    return objects.isEmpty();
}
bool exactKeys(const QJsonObject& o, const QStringList& keys) {
    const QStringList actual = o.keys();
    return QSet<QString>(actual.begin(), actual.end()) == QSet<QString>(keys.begin(), keys.end());
}
bool decode(const QJsonValue& value, QByteArray* out, int max) {
    if (!value.isString() || value.toString().size() > max * 2) return false;
    const QByteArray text = value.toString().toLatin1();
    const auto decoded = QByteArray::fromBase64Encoding(text, QByteArray::AbortOnBase64DecodingErrors);
    if (!decoded || decoded.decoded.size() > max || decoded.decoded.toBase64() != text) return false;
    *out = decoded.decoded;
    return true;
}
bool writeBytes(const QString& path, const QByteArray& bytes) {
    QSaveFile f(path); return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size() && f.commit();
}
QByteArray readBounded(const QString& path, qint64 max) {
    QFile f(path); if (!f.open(QIODevice::ReadOnly) || f.size() < 1 || f.size() > max) return {};
    return f.read(max + 1);
}
void log(const QString& stage, const QString& text) {
    QFile f(QDir(stage).filePath(QStringLiteral("update.log")));
    if (f.open(QIODevice::Append)) f.write((QDateTime::currentDateTimeUtc().toString(Qt::ISODate) + " " + text + "\n").toUtf8());
}
#ifdef Q_OS_WIN
struct Algorithm {
    BCRYPT_ALG_HANDLE value = nullptr;
    Algorithm() { BCryptOpenAlgorithmProvider(&value, BCRYPT_RSA_ALGORITHM, nullptr, 0); }
    ~Algorithm() { if (value) BCryptCloseAlgorithmProvider(value, 0); }
};
struct Key { BCRYPT_KEY_HANDLE value = nullptr; ~Key() { if (value) BCryptDestroyKey(value); } };
bool signatureValid(const QByteArray& payload, const QByteArray& signature, const QByteArray& blob) {
    // BCRYPT_RSAPUBLIC_BLOB: six little endian ULONGs, followed by exponent and modulus (big endian).
    if (blob.size() < 24) return false;
    const auto* p = reinterpret_cast<const uchar*>(blob.constData());
    const quint32 magic = qFromLittleEndian<quint32>(p), bits = qFromLittleEndian<quint32>(p + 4);
    const quint32 exponent = qFromLittleEndian<quint32>(p + 8), modulus = qFromLittleEndian<quint32>(p + 12);
    if (magic != BCRYPT_RSAPUBLIC_MAGIC || bits < 2048 || bits > 4096 || bits % 8 || modulus != bits / 8
        || exponent < 1 || exponent > 8 || qFromLittleEndian<quint32>(p + 16) || qFromLittleEndian<quint32>(p + 20)
        || blob.size() != 24 + exponent + modulus || signature.size() != modulus) return false;
    Algorithm algorithm; Key key;
    if (!algorithm.value || BCryptImportKeyPair(algorithm.value, nullptr, BCRYPT_RSAPUBLIC_BLOB, &key.value,
        reinterpret_cast<PUCHAR>(const_cast<char*>(blob.constData())), ULONG(blob.size()), 0) < 0) return false;
    QByteArray hash = QCryptographicHash::hash(payload, QCryptographicHash::Sha256);
    BCRYPT_PKCS1_PADDING_INFO padding{BCRYPT_SHA256_ALGORITHM};
    return BCryptVerifySignature(key.value, &padding, reinterpret_cast<PUCHAR>(hash.data()), ULONG(hash.size()),
        reinterpret_cast<PUCHAR>(const_cast<char*>(signature.constData())), ULONG(signature.size()), BCRYPT_PAD_PKCS1) >= 0;
}
bool noReparse(const QString& path) {
    QString current = QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
    while (!current.isEmpty()) {
        const DWORD attributes = GetFileAttributesW(reinterpret_cast<LPCWSTR>(current.utf16()));
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) return false;
        const QString parent = QFileInfo(current).dir().absolutePath();
        if (QDir::cleanPath(parent) == QDir::cleanPath(current)) break;
        current = QDir::toNativeSeparators(parent);
    }
    return true;
}
QString processPath(HANDLE handle) {
    wchar_t path[32768]; DWORD size = 32768;
    if (!QueryFullProcessImageNameW(handle, 0, path, &size)) return {};
    return QFileInfo(QString::fromWCharArray(path, int(size))).canonicalFilePath();
}
QString processStamp(HANDLE handle) {
    FILETIME creation, exit, kernel, user;
    if (!GetProcessTimes(handle, &creation, &exit, &kernel, &user)) return {};
    return QString::number((quint64(creation.dwHighDateTime) << 32) | creation.dwLowDateTime);
}
bool samePath(const QString& a, const QString& b) {
    return !a.isEmpty() && !b.isEmpty() && QString::compare(QDir::cleanPath(a), QDir::cleanPath(b), Qt::CaseInsensitive) == 0;
}
QString fileHash(const QString& path) {
    QFile f(path); QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!f.open(QIODevice::ReadOnly) || !hash.addData(&f)) return {};
    return QString::fromLatin1(hash.result().toHex());
}
bool moveReplace(const QString& from, const QString& to) {
    return MoveFileExW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(from).utf16()),
        reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(to).utf16()), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
}
struct Process {
    HANDLE handle = nullptr;
    ~Process() { if (handle) CloseHandle(handle); }
};
// Uses CreateProcessW with an explicit executable and fixed, quoted arguments; no shell or commands.
bool launch(const QString& exe, const QStringList& arguments, Process* process, const QString& working) {
    QStringList quoted;
    for (const QString& arg : arguments) {
        if (arg.contains('"') || arg.endsWith('\\') || arg.contains('\n') || arg.contains('\r')) return false;
        quoted << ('"' + arg + '"');
    }
    QString command = '"' + QDir::toNativeSeparators(exe) + "\" " + quoted.join(' ');
    STARTUPINFOW start{}; start.cb = sizeof(start); PROCESS_INFORMATION info{};
    if (!CreateProcessW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(exe).utf16()),
        reinterpret_cast<LPWSTR>(command.data()), nullptr, nullptr, FALSE, 0, nullptr,
        reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(working).utf16()), &start, &info)) return false;
    CloseHandle(info.hThread); process->handle = info.hProcess; return true;
}
int helper(const QString& directory) {
    // Every transaction has a unique direct child of the OS temp directory; no links or relative paths.
    const QString stage = QFileInfo(directory).canonicalFilePath();
    const QString temp = QFileInfo(QDir::tempPath()).canonicalFilePath();
    const QRegularExpression stageName(QStringLiteral("^SteelTally-update-[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$"));
    if (stage.isEmpty() || !samePath(directory, stage) || !samePath(QFileInfo(stage).dir().canonicalPath(), temp)
        || !stageName.match(QFileInfo(stage).fileName()).hasMatch() || !noReparse(stage)
        || !samePath(QCoreApplication::applicationFilePath(), QDir(stage).filePath("helper.exe"))) return 21;
    auto reject = [&](const QString& message) { log(stage, "FAILED " + message + " win32=" + QString::number(GetLastError())); return 22; };
    const auto document = QJsonDocument::fromJson(readBounded(QDir(stage).filePath("transaction.json"), 16384));
    const auto ticket = document.object();
    if (!exactKeys(ticket, {"target","pid","creation","nonce","arguments"}) || !ticket["target"].isString()
        || !ticket["pid"].isString() || !ticket["creation"].isString() || !ticket["nonce"].isString() || !ticket["arguments"].isArray()) return reject("invalid transaction");
    const QString target = ticket["target"].toString(), nonce = ticket["nonce"].toString();
    if (!QRegularExpression("^[0-9a-f-]{36}$").match(nonce).hasMatch() || !QFileInfo(target).isAbsolute()
        || !samePath(target, QFileInfo(target).canonicalFilePath()) || !target.endsWith(".exe", Qt::CaseInsensitive)
        || !noReparse(target) || fileHash(target) != fileHash(QCoreApplication::applicationFilePath())) return reject("target/identity mismatch");
    bool pidOk = false; const quint64 pid = ticket["pid"].toString().toULongLong(&pidOk);
    if (!pidOk || pid == 0 || pid > MAXDWORD || pid == GetCurrentProcessId()) return reject("invalid pid");
    Process parent; parent.handle = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, DWORD(pid));
    if (!parent.handle || !samePath(processPath(parent.handle), target) || processStamp(parent.handle) != ticket["creation"].toString()) return reject("parent process mismatch");
    // Preserve only the explicitly supported user data override, never propagate internal/test arguments.
    QStringList restart;
    const QJsonArray args = ticket["arguments"].toArray();
    if (!args.isEmpty()) {
        if (args.size() != 2 || args[0].toString() != "--data-root" || !args[1].isString()
            || !QFileInfo(args[1].toString()).isAbsolute()) return reject("invalid restart arguments");
        restart << "--data-root" << args[1].toString();
    }
    Release release; QString error;
    const QByteArray envelope = readBounded(QDir(stage).filePath("manifest.json"), MaxEnvelope);
    const QString downloaded = QDir(stage).filePath("download.exe");
    if (!verifyEnvelope(envelope, trustedKey(), &release, &error) || compareVersions(release.version, QCoreApplication::applicationVersion()) <= 0
        || !noReparse(downloaded) || !verifyFile(downloaded, release, &error)) return reject("reverification: " + error);
    if (!writeBytes(QDir(stage).filePath("ready"), nonce.toUtf8())) return reject("cannot signal ready");
    log(stage, "helper ready; waiting for exact parent process");
    if (WaitForSingleObject(parent.handle, 120000) != WAIT_OBJECT_0) return reject("parent did not exit (no files changed)");
    // Check again after waiting, including the old executable identity.
    if (!noReparse(target) || fileHash(target) != fileHash(QCoreApplication::applicationFilePath())
        || !verifyFile(downloaded, release, &error)) return reject("files changed while waiting");
    const QString suffix = QFileInfo(stage).fileName().mid(StagePrefix.size());
    const QString candidate = target + ".update-new-" + suffix + ".exe";
    const QString backup = target + ".update-backup-" + suffix + ".exe";
    if (QFileInfo::exists(candidate) || QFileInfo::exists(backup) || !QFile::copy(downloaded, candidate)) return reject("cannot create sibling candidate");
    if (!verifyFile(candidate, release, &error)) return reject("candidate verification: " + error);
    if (!ReplaceFileW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(target).utf16()),
        reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(candidate).utf16()),
        reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(backup).utf16()), 0, nullptr, nullptr)) {
        // ReplaceFile can partially move the old file on failure. Restore when a backup was created.
        if (QFileInfo::exists(backup)) moveReplace(backup, target);
        return reject("ReplaceFile failed; restore attempted if needed");
    }
    log(stage, "replacement complete; backup retained at " + backup);
    const QString health = QDir(stage).filePath("health");
    QStringList startArgs = restart; startArgs << "--update-health" << stage << nonce;
    Process child;
    bool healthy = false;
    if (launch(target, startArgs, &child, QFileInfo(target).absolutePath())) {
        for (int i = 0; i < 450; ++i) {
            if (WaitForSingleObject(child.handle, 100) == WAIT_OBJECT_0) break;
            if (readBounded(health, 128) == nonce.toUtf8()) {
                healthy = WaitForSingleObject(child.handle, 5000) == WAIT_TIMEOUT; break;
            }
        }
    }
    if (healthy) { log(stage, "SUCCESS startup health acknowledged; old backup retained"); writeBytes(QDir(stage).filePath("result.json"), "{\"ok\":true}\n"); return 0; }
    if (child.handle && WaitForSingleObject(child.handle, 0) == WAIT_TIMEOUT) {
        TerminateProcess(child.handle, 25); WaitForSingleObject(child.handle, 10000);
    }
    // Preserve the failed candidate too; never delete a previous executable or user data.
    const QString failed = target + ".update-failed-" + suffix + ".exe";
    const bool archived = moveReplace(target, failed);
    const bool restored = moveReplace(backup, target);
    log(stage, QString("ROLLBACK archived=%1 restored=%2").arg(archived).arg(restored));
    writeBytes(QDir(stage).filePath("result.json"), QJsonDocument(QJsonObject{{"ok",false},{"rollback",restored},{"error","startup health failed"}}).toJson());
    if (restored) { Process old; launch(target, restart, &old, QFileInfo(target).absolutePath()); }
    return restored ? 25 : 26;
}
#else
bool signatureValid(const QByteArray&, const QByteArray&, const QByteArray&) { return false; }
#endif
}

bool parseVersion(const QString& value, int parts[3]) {
    if (!QRegularExpression(QStringLiteral("^(0|[1-9][0-9]{0,5})\\.(0|[1-9][0-9]{0,5})\\.(0|[1-9][0-9]{0,5})$")).match(value).hasMatch()) return false;
    const auto split = value.split('.');
    for (int i = 0; i < 3; ++i) parts[i] = split[i].toInt();
    return true;
}
int compareVersions(const QString& a, const QString& b) {
    int aa[3], bb[3]; if (!parseVersion(a, aa) || !parseVersion(b, bb)) return -2;
    for (int i = 0; i < 3; ++i) if (aa[i] != bb[i]) return aa[i] > bb[i] ? 1 : -1;
    return 0;
}
bool verifyEnvelope(const QByteArray& envelope, const QByteArray& publicBlob, Release* release, QString* error) {
    if (envelope.isEmpty() || envelope.size() > MaxEnvelope || !uniqueJsonKeys(envelope)) return fail(error, "manifest size/duplicate keys invalid");
    QJsonParseError parse;
    const auto outer = QJsonDocument::fromJson(envelope, &parse);
    if (parse.error != QJsonParseError::NoError || !outer.isObject()
        || !exactKeys(outer.object(), {"payload","signature"})) return fail(error, "invalid envelope schema");
    QByteArray payload, signature;
    if (!decode(outer["payload"], &payload, 32768) || !decode(outer["signature"], &signature, 512)
        || !signatureValid(payload, signature, publicBlob)) return fail(error, "untrusted manifest signature");
    if (!uniqueJsonKeys(payload)) return fail(error, "duplicate payload keys");
    const auto doc = QJsonDocument::fromJson(payload, &parse);
    const auto o = doc.object();
    if (parse.error != QJsonParseError::NoError || !doc.isObject() || !exactKeys(o,
        {"schema","version","url","size","sha256","notes","catalog_version","rules_version","project_schema"})) return fail(error, "invalid payload schema");
    for (const auto& k : {"schema","version","url","sha256","notes","catalog_version","rules_version","project_schema"})
        if (!o[k].isString()) return fail(error, "invalid field type");
    int v[3];
    if (o["schema"].toString() != "steeltally.update/1" || !parseVersion(o["version"].toString(), v)
        || !https(QUrl(o["url"].toString(), QUrl::StrictMode)) || o["url"].toString().size() > 2048
        || !QRegularExpression("^[0-9a-f]{64}$").match(o["sha256"].toString()).hasMatch()
        || o["notes"].toString().size() > 8192) return fail(error, "invalid payload values");
    for (const auto& k : {"catalog_version","rules_version","project_schema"})
        if (o[k].toString().isEmpty() || o[k].toString().size() > 128) return fail(error, "invalid component version");
    const double size = o["size"].toDouble(-1);
    if (!o["size"].isDouble() || !std::isfinite(size) || std::floor(size) != size || size < 1 || size > MaxDownload)
        return fail(error, "invalid executable size");
    if (release) *release = Release{o["version"].toString(),o["sha256"].toString(),o["notes"].toString(),
        o["catalog_version"].toString(),o["rules_version"].toString(),o["project_schema"].toString(), QUrl(o["url"].toString()), qint64(size)};
    return true;
}
bool verifyFile(const QString& path, const Release& release, QString* error) {
    QFile f(path); QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!f.open(QIODevice::ReadOnly) || f.size() != release.size || !hash.addData(&f)
        || hash.result().toHex() != release.sha256.toLatin1()) return fail(error, "executable size/SHA256 mismatch");
    f.seek(0); if (f.read(2) != "MZ") return fail(error, "not a Windows executable");
    return true;
}

namespace {
// A fresh manager plus a deny-all jar also prevents Qt's redirect path from adding cookies.
class NoCookies : public QNetworkCookieJar {
public:
    QList<QNetworkCookie> cookiesForUrl(const QUrl&) const override { return {}; }
    bool setCookiesFromUrl(const QList<QNetworkCookie>&, const QUrl&) override { return false; }
};
bool allowRedirect(const QUrl& url, int count) { return https(url) && count <= 5; }
bool acceptsBody(int status) { return status == 200; }
// One shared network/data path for Controller and the download-only diagnostic.
class Transfer : public QObject {
public:
    QNetworkAccessManager network;
    QNetworkReply* reply = nullptr;
    QTimer deadline, inactivity;
    QString failure;
    qint64 received = 0, limit = 0;
    int redirects = 0, httpStatus = 0, networkError = 0;
    QElapsedTimer elapsed;
    qint64 lastDataMs = 0, progressBytes = 0;
    int readyEvents = 0;
    std::function<bool(const QByteArray&)> sink;
    std::function<void(bool, const QString&)> done;
    Transfer(QObject* parent = nullptr) : QObject(parent), network(this) {
        network.setCookieJar(new NoCookies);
        inactivity.setSingleShot(true);
        connect(&inactivity, &QTimer::timeout, this, [this] { cancel(QStringLiteral("更新请求15秒无传输进展")); });
        deadline.setSingleShot(true);
        connect(&deadline, &QTimer::timeout, this, [this] { cancel(QStringLiteral("更新请求总时限已到")); });
    }
    void cancel(const QString& reason) { failure = reason; if (reply) reply->abort(); }
    void drain() {
        // Qt can deliver 3xx data before redirected(); discard it before the sink.
        consume(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), reply->read(64 * 1024),
                reply->header(QNetworkRequest::ContentLengthHeader).toLongLong());
    }
    void consume(int status, const QByteArray& data, qint64 contentLength) {
        if (!acceptsBody(status)) return;
        if (contentLength > limit || received + data.size() > limit) {
            cancel(QStringLiteral("响应超过大小限制")); return;
        }
        if (!data.isEmpty()) { inactivity.start(15000); lastDataMs = elapsed.elapsed(); }
        received += data.size();
        if (!data.isEmpty() && !sink(data)) cancel(QStringLiteral("响应写入失败"));
    }
    void start(const QUrl& url, qint64 maximum, int milliseconds) {
        if (!elapsed.isValid()) { elapsed.start(); deadline.start(milliseconds); inactivity.start(15000); }
        limit = maximum;
        if (!https(url)) { done(false, QStringLiteral("拒绝不安全 HTTPS 地址")); return; }
        QNetworkRequest req(url);
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
        req.setMaximumRedirectsAllowed(5);
        req.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
        req.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
        req.setAttribute(QNetworkRequest::AuthenticationReuseAttribute, QNetworkRequest::Manual);
        req.setRawHeader("Accept-Encoding", "identity");
        // Both time limits belong to the complete transaction, not each redirect hop.
        reply = network.get(req);
        reply->setReadBufferSize(128 * 1024);
        connect(reply, &QNetworkReply::sslErrors, this, [this](const QList<QSslError>&) { cancel(QStringLiteral("HTTPS 证书校验失败")); });
        connect(reply, &QNetworkReply::downloadProgress, this, [this](qint64 value, qint64) { progressBytes = value; });
        connect(reply, &QNetworkReply::readyRead, this, [this] { ++readyEvents; while (reply && reply->bytesAvailable() > 0 && failure.isEmpty()) drain(); });
        connect(reply, &QNetworkReply::finished, this, [this] {
            if (reply->isOpen()) drain();
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (failure.isEmpty() && (status == 301 || status == 302 || status == 303 || status == 307 || status == 308)) {
                const QUrl target = reply->url().resolved(reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl());
                reply->deleteLater(); reply = nullptr;
                if (allowRedirect(target, ++redirects)) { start(target, limit, 0); return; }
                failure = QStringLiteral("拒绝不安全或过多 HTTPS 跳转");
                deadline.stop(); inactivity.stop(); done(false, failure); return;
            }
            deadline.stop(); inactivity.stop();
            const auto code = reply->error();
            httpStatus = status; networkError = int(code);
            const bool ok = failure.isEmpty() && code == QNetworkReply::NoError && status == 200;
            // errorString can contain signed query strings: numeric diagnostics only.
            if (!ok && failure.isEmpty()) failure = QStringLiteral("网络请求失败（Qt %1，HTTP %2）").arg(int(code)).arg(status);
            reply->deleteLater(); reply = nullptr;
            done(ok, failure);
        });
    }
};
class Controller : public QObject {
    QMainWindow* window;
    QLabel* label;
    QAction* checkAction;
    Transfer* transfer = nullptr;
    QTimer interval;
    QProgressDialog* progress = nullptr;
    QFile output;
    QByteArray bytes, envelope;
    Release release;
    QString stage, nonce, failure;
    bool manual = false, downloading = false;
    void status(const QString& message) { label->setText(QStringLiteral("v%1 · %2").arg(QCoreApplication::applicationVersion(), message)); }
    void report(const QString& message) {
        status(message);
        if (!stage.isEmpty()) log(stage, message);
        else {
            const QString diagnostics = QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).filePath("updater-logs");
            if (QDir().mkpath(diagnostics)) log(diagnostics, message);
        }
        if (manual) QMessageBox::information(window, QStringLiteral("在线更新"), message);
    }
    void abort(const QString& reason) { if (transfer) transfer->cancel(reason); }
    void request(const QUrl& url) {
        failure.clear(); bytes.clear();
        transfer = new Transfer(this);
        transfer->sink = [this](const QByteArray& data) {
            if (downloading) {
                if (output.write(data) != data.size()) return false;
                if (progress) progress->setValue(int(output.size() * 100 / release.size));
            } else bytes += data;
            return true;
        };
        transfer->done = [this](bool ok, const QString& errorMessage) {
            failure = errorMessage;
            transfer->deleteLater(); transfer = nullptr;
            checkAction->setEnabled(true);
            if (progress) { progress->hide(); progress->deleteLater(); progress = nullptr; }
            if (downloading) output.close();
            if (!ok) { report(failure); return; }
            QString error;
            if (downloading) {
                if (!verifyEnvelope(envelope, trustedKey(), &release, &error) || !verifyFile(output.fileName(), release, &error)) { report(QStringLiteral("下载校验失败：") + error); return; }
                status(QStringLiteral("更新已验证，等待确认"));
                const auto answer = QMessageBox::question(window, QStringLiteral("保存后安装更新"),
                    QStringLiteral("新版本 %1 已下载并完成签名、大小和 SHA256 校验。\n请确认当前工程已保存。选择“是”将退出程序并安装，随后重新启动；选择“否”继续编辑。\n更新只替换 EXE，保留工程、自定义数据和设置。\n\n现在退出并安装？").arg(release.version), QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
                if (answer == QMessageBox::Yes) apply();
                return;
            }
            envelope = bytes;
            if (!verifyEnvelope(envelope, trustedKey(), &release, &error)) { report(QStringLiteral("更新清单被拒绝：") + error); return; }
            const int comparison = compareVersions(release.version, QCoreApplication::applicationVersion());
            if (comparison <= 0) { report(comparison == 0 ? QStringLiteral("已是最新版本（签名已验证）") : QStringLiteral("更新源版本较旧，已拒绝降级")); return; }
            status(QStringLiteral("发现新版本 %1").arg(release.version));
            // Background checks only announce; downloading always requires the user's click/confirmation.
            if (!manual) return;
            QMessageBox box(QMessageBox::Question, QStringLiteral("发现更新"), QStringLiteral("版本 %1\n\n%2\n\n是否下载完整 EXE？下载期间可以继续编辑。").arg(release.version, release.notes), QMessageBox::Yes | QMessageBox::No, window);
            box.setTextFormat(Qt::PlainText); box.setDefaultButton(QMessageBox::No);
            if (box.exec() == QMessageBox::Yes) download();
        };
        transfer->start(url, downloading ? release.size : MaxEnvelope, downloading ? 600000 : 30000);
    }
    void download() {
        stage = QDir(QDir::tempPath()).filePath(StagePrefix + uniqueId()); nonce = uniqueId();
        if (!QDir().mkdir(stage) || !writeBytes(QDir(stage).filePath("manifest.json"), envelope)) { report(QStringLiteral("不能创建更新暂存目录")); return; }
        output.setFileName(QDir(stage).filePath("download.exe"));
        if (!output.open(QIODevice::WriteOnly | QIODevice::NewOnly)) { report(QStringLiteral("不能写入更新暂存文件")); return; }
        downloading = true; checkAction->setEnabled(false); status(QStringLiteral("正在下载完整更新"));
        progress = new QProgressDialog(QStringLiteral("下载并验证完整 EXE；不会自动替换正在编辑的程序。"), QStringLiteral("取消"), 0, 100, window);
        progress->setWindowModality(Qt::NonModal); progress->setMinimumDuration(0); progress->setAutoClose(false);
        connect(progress, &QProgressDialog::canceled, this, [this]() { abort(QStringLiteral("下载已取消")); });
        request(release.url);
    }
    void apply() {
#ifdef Q_OS_WIN
        QString error;
        if (!verifyEnvelope(envelope, trustedKey(), &release, &error) || !verifyFile(output.fileName(), release, &error)) { report(QStringLiteral("安装前验证失败：") + error); return; }
        const QString target = QFileInfo(QCoreApplication::applicationFilePath()).canonicalFilePath();
        const QString helperPath = QDir(stage).filePath("helper.exe");
        QStringList preserved;
        const auto args = QCoreApplication::arguments(); const int data = args.indexOf("--data-root");
        if (data >= 0 && data + 1 < args.size()) preserved << "--data-root" << QFileInfo(args[data + 1]).absoluteFilePath();
        const QJsonObject ticket{{"target",target},{"pid",QString::number(GetCurrentProcessId())},
            {"creation",processStamp(GetCurrentProcess())},{"nonce",nonce},{"arguments",QJsonArray::fromStringList(preserved)}};
        if (!QFile::copy(target, helperPath) || !writeBytes(QDir(stage).filePath("transaction.json"), QJsonDocument(ticket).toJson())) { report(QStringLiteral("辅助更新器准备失败；程序继续运行")); return; }
        Process helperProcess;
        if (!launch(helperPath, {"--update-helper", stage}, &helperProcess, stage)) { report(QStringLiteral("辅助更新器启动失败；程序继续运行")); return; }
        // Async readiness handshake: never exit before the helper owns the exact parent process handle.
        HANDLE handle = helperProcess.handle; helperProcess.handle = nullptr;
        auto* timer = new QTimer(this); auto* elapsed = new int(0);
        checkAction->setEnabled(false);
        connect(timer, &QTimer::timeout, this, [this,timer,elapsed,handle]() {
            *elapsed += 100;
            if (readBounded(QDir(stage).filePath("ready"), 128) == nonce.toUtf8()) {
                timer->stop(); CloseHandle(handle); delete elapsed; timer->deleteLater(); QApplication::quit(); return;
            }
            if (*elapsed >= 15000 || WaitForSingleObject(handle, 0) == WAIT_OBJECT_0) {
                timer->stop();
                if (WaitForSingleObject(handle, 0) == WAIT_TIMEOUT) { TerminateProcess(handle, 27); WaitForSingleObject(handle, 5000); }
                CloseHandle(handle); delete elapsed; timer->deleteLater(); checkAction->setEnabled(true);
                report(QStringLiteral("辅助更新器未就绪；未退出、未替换。诊断：") + QDir(stage).filePath("update.log"));
            }
        });
        timer->start(100);
#else
        report(QStringLiteral("仅支持 Windows 更新"));
#endif
    }
public:
    Controller(QMainWindow* w, QLabel* l, QAction* a) : QObject(w), window(w), label(l), checkAction(a) {
        connect(a, &QAction::triggered, this, [this]() { check(true); });
        connect(&interval, &QTimer::timeout, this, [this]() { check(false); });
        interval.start(6 * 60 * 60 * 1000);
        status(configured() ? QStringLiteral("等待检查更新") : QStringLiteral("未配置更新源"));
        const auto args = QCoreApplication::arguments();
        if (!args.contains("--selftest") && !args.contains("--calculation-probe")) QTimer::singleShot(10000, this, [this]() { check(false); });
    }
    bool configured() const { return https(QUrl(QString::fromUtf8(STEELTALLY_UPDATE_URL), QUrl::StrictMode)) && !trustedKey().isEmpty(); }
    void check(bool explicitCheck) {
        if (transfer) { if (explicitCheck) abort(QStringLiteral("检查已取消")); return; }
        manual = explicitCheck; downloading = false;
        if (!configured()) { if (explicitCheck) report(QStringLiteral("未配置更新源。需要构建时配置 HTTPS 地址和受信 RSA 公钥，当前可离线使用。")); return; }
        status(QStringLiteral("正在检查更新（再次点击可取消）"));
        request(QUrl(QString::fromUtf8(STEELTALLY_UPDATE_URL)));
    }
};
}
void attach(QMainWindow* window, QLabel* label, QAction* action) { new Controller(window, label, action); }
bool configured() { return https(QUrl(QString::fromUtf8(STEELTALLY_UPDATE_URL), QUrl::StrictMode)) && !trustedKey().isEmpty(); }
void acknowledgeHealth(const QStringList& args) {
#ifdef Q_OS_WIN
    const int index = args.indexOf("--update-health");
    if (index < 0 || index + 3 != args.size()) return;
    const QString stage = args[index + 1], nonce = args[index + 2];
    if (!QRegularExpression("^[0-9a-f-]{36}$").match(nonce).hasMatch() || !noReparse(stage)
        || !samePath(QFileInfo(stage).dir().canonicalPath(), QFileInfo(QDir::tempPath()).canonicalFilePath())
        || !QFileInfo(stage).fileName().startsWith(StagePrefix)) return;
    const auto ticket = QJsonDocument::fromJson(readBounded(QDir(stage).filePath("transaction.json"), 16384)).object();
    if (ticket["nonce"].toString() != nonce || !samePath(ticket["target"].toString(), QCoreApplication::applicationFilePath())) return;
    QTimer::singleShot(3000, qApp, [stage,nonce]() { writeBytes(QDir(stage).filePath("health"), nonce.toUtf8()); });
#else
    Q_UNUSED(args)
#endif
}

// Pure verifier tests generate a transient test key using Windows CNG. It is never used by Controller/helper.
static int selftest(const QString& path) {
    QJsonArray checks;
    auto test = [&](const QString& name, bool ok) { checks.append(QJsonObject{{"name",name},{"ok",ok}}); };
    test("redirect-reject-http", !allowRedirect(QUrl("http://example.com/a"),1));
    test("redirect-reject-credentials", !allowRedirect(QUrl("https://user@example.com/a"),1));
    test("redirect-reject-port", !allowRedirect(QUrl("https://example.com:444/a"),1));
    test("redirect-reject-malformed", !allowRedirect(QUrl("https:///"),1));
    test("redirect-reject-fragment", !allowRedirect(QUrl("https://example.com/a#x"),1));
    test("redirect-limit", allowRedirect(QUrl("https://other.example/a?token=x"),5) && !allowRedirect(QUrl("https://other.example/a"),6));
    QByteArray collected;
    Transfer synthetic;
    synthetic.limit = 5;
    synthetic.sink = [&](const QByteArray& data) { collected += data; return true; };
    synthetic.consume(302,"redirect-body",999999);
    synthetic.consume(307,"another-body",999999);
    synthetic.consume(200,"final",5);
    test("redirect-body-excluded", collected == "final" && synthetic.received == 5 && synthetic.failure.isEmpty());
    synthetic.consume(200,"overflow",8);
    test("transfer-size-limit", !synthetic.failure.isEmpty() && collected == "final");
    synthetic.cancel("cancelled");
    test("transfer-cancellation", synthetic.failure == "cancelled");
    int parts[3];
    test("numeric-version", compareVersions("1.10.0", "1.9.99") == 1 && compareVersions("1.0.3", "1.0.3") == 0);
    test("invalid-versions", !parseVersion("01.2.3",parts) && !parseVersion("1.2",parts) && !parseVersion("1.2.3-beta",parts) && !parseVersion("9999999.0.0",parts));
#ifdef Q_OS_WIN
    Algorithm algorithm; Key key;
    if (!algorithm.value || BCryptGenerateKeyPair(algorithm.value, &key.value, 2048, 0) < 0 || BCryptFinalizeKeyPair(key.value, 0) < 0) return 3;
    ULONG count = 0;
    BCryptExportKey(key.value, nullptr, BCRYPT_RSAPUBLIC_BLOB, nullptr, 0, &count, 0);
    QByteArray publicBlob(int(count), '\0');
    if (BCryptExportKey(key.value, nullptr, BCRYPT_RSAPUBLIC_BLOB, reinterpret_cast<PUCHAR>(publicBlob.data()), count, &count, 0) < 0) return 3;
    const QByteArray executable("MZsynthetic-only");
    QJsonObject payload{{"schema","steeltally.update/1"},{"version","1.0.4"},{"url","https://updates.example.invalid/app.exe"},
        {"size",executable.size()},{"sha256",QString::fromLatin1(QCryptographicHash::hash(executable,QCryptographicHash::Sha256).toHex())},
        {"notes","synthetic"},{"catalog_version","1.0.3"},{"rules_version","1.0.3"},{"project_schema","1"}};
    auto sign = [&](const QByteArray& raw) {
        QByteArray hash = QCryptographicHash::hash(raw, QCryptographicHash::Sha256);
        BCRYPT_PKCS1_PADDING_INFO padding{BCRYPT_SHA256_ALGORITHM}; ULONG size = 0;
        BCryptSignHash(key.value,&padding,reinterpret_cast<PUCHAR>(hash.data()),ULONG(hash.size()),nullptr,0,&size,BCRYPT_PAD_PKCS1);
        QByteArray signature(int(size),'\0');
        if (BCryptSignHash(key.value,&padding,reinterpret_cast<PUCHAR>(hash.data()),ULONG(hash.size()),reinterpret_cast<PUCHAR>(signature.data()),size,&size,BCRYPT_PAD_PKCS1) < 0) return QByteArray();
        return QJsonDocument(QJsonObject{{"payload",QString::fromLatin1(raw.toBase64())},{"signature",QString::fromLatin1(signature.toBase64())}}).toJson(QJsonDocument::Compact);
    };
    auto signedObject = [&](const QJsonObject& o) { return sign(QJsonDocument(o).toJson(QJsonDocument::Compact)); };
    Release release; QString error;
    const QByteArray valid = signedObject(payload);
    test("valid-cng-signature", verifyEnvelope(valid,publicBlob,&release,&error));
    auto outer = QJsonDocument::fromJson(valid).object();
    QByteArray signature = QByteArray::fromBase64(outer["signature"].toString().toLatin1()); signature[0] ^= 1;
    outer["signature"] = QString::fromLatin1(signature.toBase64());
    test("signature-tamper", !verifyEnvelope(QJsonDocument(outer).toJson(),publicBlob,nullptr,&error));
    outer = QJsonDocument::fromJson(valid).object(); outer["payload"] = QString::fromLatin1((QJsonDocument(payload).toJson(QJsonDocument::Compact) + " ").toBase64());
    test("exact-payload-bytes", !verifyEnvelope(QJsonDocument(outer).toJson(),publicBlob,nullptr,&error));
    test("wrong-key", !verifyEnvelope(valid,QByteArray(300,'x'),nullptr,&error));
    test("missing-key", !verifyEnvelope(valid,{},nullptr,&error));
    auto invalid = payload; invalid["url"] = "http://example.invalid/a.exe";
    test("reject-http", !verifyEnvelope(signedObject(invalid),publicBlob,nullptr,&error));
    invalid = payload; invalid["url"] = "https://user:secret@example.invalid/a.exe";
    test("reject-url-credentials", !verifyEnvelope(signedObject(invalid),publicBlob,nullptr,&error));
    invalid = payload; invalid["size"] = 1.5;
    test("reject-fractional-size", !verifyEnvelope(signedObject(invalid),publicBlob,nullptr,&error));
    invalid["size"] = double(MaxDownload + 1);
    test("reject-huge-size", !verifyEnvelope(signedObject(invalid),publicBlob,nullptr,&error));
    invalid = payload; invalid["unknown"] = true;
    test("reject-extra-field", !verifyEnvelope(signedObject(invalid),publicBlob,nullptr,&error));
    invalid = payload; invalid.remove("version");
    test("reject-missing-field", !verifyEnvelope(signedObject(invalid),publicBlob,nullptr,&error));
    invalid = payload; invalid["version"] = "1.0.4-beta";
    test("reject-prerelease", !verifyEnvelope(signedObject(invalid),publicBlob,nullptr,&error));
    test("reject-large-envelope", !verifyEnvelope(QByteArray(int(MaxEnvelope + 1),' '),publicBlob,nullptr,&error));
    test("reject-invalid-json", !verifyEnvelope("{",publicBlob,nullptr,&error));
    test("reject-signed-invalid-json", !verifyEnvelope(sign("{"),publicBlob,nullptr,&error));
    const QByteArray rawPayload = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    test("reject-duplicate-payload-key", !verifyEnvelope(sign("{\"version\":\"9.0.0\"," + rawPayload.mid(1)),publicBlob,nullptr,&error));
    test("reject-escaped-duplicate-key", !verifyEnvelope(sign("{\"ver\\u0073ion\":\"9.0.0\"," + rawPayload.mid(1)),publicBlob,nullptr,&error));
    test("reject-duplicate-envelope-key", !verifyEnvelope("{\"payload\":\"\"," + valid.mid(1),publicBlob,nullptr,&error));
    if (!QDir().mkpath(path)) return 3;
    writeBytes(QDir(path).filePath("synthetic.exe"),executable);
    test("valid-size-hash", verifyFile(QDir(path).filePath("synthetic.exe"), release,&error));
    writeBytes(QDir(path).filePath("synthetic.exe"), "MZwrong-size");
    test("reject-file-size", !verifyFile(QDir(path).filePath("synthetic.exe"),release,&error));
    QByteArray wrong = executable; wrong[3] ^= 1; writeBytes(QDir(path).filePath("synthetic.exe"),wrong);
    test("reject-file-hash", !verifyFile(QDir(path).filePath("synthetic.exe"),release,&error));
    writeBytes(QDir(path).filePath("public-key.b64"),publicBlob.toBase64());
    writeBytes(QDir(path).filePath("manifest.json"),valid);
#else
    test("windows-required", false);
#endif
    bool ok = true; for (const auto& check : checks) ok = ok && check.toObject()["ok"].toBool();
    if (!writeBytes(QDir(path).filePath("result.json"), QJsonDocument(QJsonObject{{"ok",ok},{"checks",checks}}).toJson())) return 3;
    std::printf("updater-selftest: %s checks=%d\n",ok?"ok":"FAILED",int(checks.size()));
    return ok ? 0 : 1;
}
static int networkTest(const QString& path) {
    if (QFileInfo::exists(path) || !QDir().mkpath(path)) return 2;
    QJsonArray transfers;
    QString error;
    auto fetch = [&](const QUrl& url, qint64 maximum, QFile* file, QByteArray* memory) {
        Transfer transfer;
        QEventLoop loop;
        bool ok = false;
        transfer.sink = [&](const QByteArray& data) { if (file) return file->write(data) == data.size(); *memory += data; return true; };
        transfer.done = [&](bool success, const QString& message) { ok = success; error = message; loop.quit(); };
        transfer.start(url, maximum, file ? 600000 : 30000);
        if (transfer.reply) loop.exec();
        transfers.append(QJsonObject{{"ok",ok},{"redirects",transfer.redirects},{"bytes",double(transfer.received)},{"elapsed_ms",double(transfer.elapsed.elapsed())},
            {"last_data_ms",double(transfer.lastDataMs)},{"ready_events",transfer.readyEvents},{"download_progress",double(transfer.progressBytes)},{"http_status",transfer.httpStatus},{"qt_error",transfer.networkError},{"abort_reason",transfer.failure}});
        return ok;
    };
    QByteArray envelope; Release release;
    bool ok = configured() && fetch(QUrl(QString::fromUtf8(STEELTALLY_UPDATE_URL)), MaxEnvelope, nullptr, &envelope);
    if (ok) { writeBytes(QDir(path).filePath("manifest.json"),envelope); ok = verifyEnvelope(envelope,trustedKey(),&release,&error); }
    QFile file(QDir(path).filePath("download.exe"));
    if (ok) { ok = file.open(QIODevice::WriteOnly | QIODevice::NewOnly); if (!ok) error = "Cannot create download"; }
    if (ok) ok = fetch(release.url,release.size,&file,nullptr);
    file.close();
    if (ok) ok = verifyFile(file.fileName(),release,&error);
    const QJsonObject result{{"ok",ok},{"error",error},{"version",release.version},{"size",double(release.size)},
        {"sha256",release.sha256},{"transfers",transfers},{"installed",false},{"trust","embedded production public key"}};
    writeBytes(QDir(path).filePath("result.json"),QJsonDocument(result).toJson());
    return ok ? 0 : 1;
}
int commandMode(const QStringList& args) {
    if (args.contains("--update-network-test")) return args.size() == 3 && args[1] == "--update-network-test" ? networkTest(args[2]) : 2;
    if (args.contains("--update-helper")) {
#ifdef Q_OS_WIN
        return args.size() == 3 && args[1] == "--update-helper" ? helper(args[2]) : 20;
#else
        return 20;
#endif
    }
    if (args.contains("--update-selftest")) return args.size() == 3 && args[1] == "--update-selftest" ? selftest(args[2]) : 2;
    // Verifies local synthetic fixtures only; does not install, store or change the embedded trust root.
    if (args.contains("--update-verify-test")) {
        if (args.size() != 5 || args[1] != "--update-verify-test") return 2;
        Release release; QString error;
        const bool ok = verifyEnvelope(readBounded(args[2],MaxEnvelope),QByteArray::fromBase64(readBounded(args[3],8192).trimmed()),&release,&error);
        writeBytes(args[4],QJsonDocument(QJsonObject{{"ok",ok},{"error",error},{"version",release.version}}).toJson());
        return ok ? 0 : 1;
    }
    return -1;
}
}
