// FirmwareNetworkClient.cpp — network fetch and firmware version checking.
//
// Extracted from VideoHid (Phase 4 refactoring). All methods that were
// VideoHid::fetchBinFileToString, VideoHid::getLatestFirmwareFilenName,
// VideoHid::pickFirmwareFileNameFromIndex, and the network portion of
// VideoHid::isLatestFirmware live here.

#include "FirmwareNetworkClient.h"

// Pull in FirmwareResult enum (defined in videohid.h / statusevents area).
// We include videohid.h ONLY for the enum definition; this header does NOT
// create a dependency on the VideoHid instance.
#include "../videohid.h"

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QEventLoop>
#include <QTimer>
#include <QStringList>
#include <QVector>
#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(log_host_hid)

// ─────────────────────────────────────────────────────────────────────────────
//  check()
// ─────────────────────────────────────────────────────────────────────────────
FirmwareResult FirmwareNetworkClient::check(const std::string& currentVersion,
                                             VideoChipType chip,
                                             const QString& indexUrl,
                                             int timeoutMs)
{
    m_lastResult = FirmwareResult::Checking;

    QString filename = fetchFilename(chip, indexUrl, timeoutMs);
    if (filename.isEmpty()) {
        // m_lastResult already set by fetchFilename (Timeout or CheckFailed)
        return m_lastResult;
    }

    // Build binary URL: replace the last path segment with the chosen filename.
    QString binUrl = indexUrl;
    int lastSlash = binUrl.lastIndexOf('/');
    if (lastSlash >= 0) binUrl = binUrl.left(lastSlash + 1) + filename;

    fetchBinFile(binUrl, timeoutMs);
    if (m_lastResult == FirmwareResult::Timeout || m_lastResult == FirmwareResult::CheckFailed)
        return m_lastResult;

    // Compare versions.
    const std::string& latest = m_latestVersion;
    qCDebug(log_host_hid) << "FirmwareNetworkClient: device version:" << QString::fromStdString(currentVersion)
                          << "latest version:" << QString::fromStdString(latest);

    if (currentVersion == latest) {
        m_lastResult = FirmwareResult::Latest;
    } else {
        // safe integer comparison — treat non-numeric as 0
        auto safeInt = [](const std::string& s) -> int {
            try { return std::stoi(s); }
            catch (...) { return 0; }
        };
        m_lastResult = (safeInt(currentVersion) <= safeInt(latest))
            ? FirmwareResult::Upgradable
            : FirmwareResult::Latest;
    }
    return m_lastResult;
}

// ─────────────────────────────────────────────────────────────────────────────
//  fetchFilename()  (private)
// ─────────────────────────────────────────────────────────────────────────────
QString FirmwareNetworkClient::fetchFilename(VideoChipType chip,
                                              const QString& indexUrl,
                                              int timeoutMs)
{
    QNetworkAccessManager manager;
    QNetworkRequest request(indexUrl);
    request.setRawHeader("User-Agent", "OpenterfaceFirmwareChecker/1.0");

    QNetworkReply* reply = manager.get(request);
    if (!reply) {
        qCDebug(log_host_hid) << "FirmwareNetworkClient: failed to create network reply for" << indexUrl;
        m_lastResult = FirmwareResult::CheckFailed;
        return {};
    }

    m_lastResult = FirmwareResult::Checking;
    qCDebug(log_host_hid) << "FirmwareNetworkClient: fetching firmware index from" << indexUrl;

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);

    QObject::connect(reply, &QNetworkReply::finished, &loop, [&]() {
        qCDebug(log_host_hid) << "FirmwareNetworkClient: index reply finished";
        loop.quit();
    });
    QObject::connect(&timer, &QTimer::timeout, &loop, [&]() {
        qCDebug(log_host_hid) << "FirmwareNetworkClient: index request timed out";
        m_lastResult = FirmwareResult::Timeout;
        reply->abort();
        loop.quit();
    });

    timer.start(timeoutMs);
    loop.exec();
    if (timer.isActive()) timer.stop();

    if (m_lastResult == FirmwareResult::Timeout) {
        reply->deleteLater();
        return {};
    }

    if (reply->error() != QNetworkReply::NoError) {
        qCDebug(log_host_hid) << "FirmwareNetworkClient: index fetch failed:" << reply->errorString();
        m_lastResult = FirmwareResult::CheckFailed;
        reply->deleteLater();
        return {};
    }

    const QString indexContent = QString::fromUtf8(reply->readAll());
    reply->deleteLater();

    const QString fileName = pickFirmwareFileNameFromIndex(indexContent, chip);
    if (fileName.isEmpty()) {
        qCWarning(log_host_hid) << "FirmwareNetworkClient: no firmware filename selected for chip" << (int)chip;
        m_lastResult = FirmwareResult::CheckFailed;
        return {};
    }

    qCInfo(log_host_hid) << "FirmwareNetworkClient: selected firmware file for chip"
                         << (int)chip << ":" << fileName;
    m_lastResult = FirmwareResult::CheckSuccess;
    return fileName;
}

// ─────────────────────────────────────────────────────────────────────────────
//  fetchBinFile()  (private)
// ─────────────────────────────────────────────────────────────────────────────
void FirmwareNetworkClient::fetchBinFile(const QString& url, int timeoutMs)
{
    QNetworkAccessManager manager;
    QNetworkRequest request(url);
    QNetworkReply* reply = manager.get(request);

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);

    QObject::connect(reply, &QNetworkReply::finished, &loop, [&]() {
        qCDebug(log_host_hid) << "FirmwareNetworkClient: firmware binary download finished";
        loop.quit();
    });
    QObject::connect(&timer, &QTimer::timeout, &loop, [&]() {
        qCDebug(log_host_hid) << "FirmwareNetworkClient: firmware binary download timed out";
        m_lastResult = FirmwareResult::Timeout;
        reply->abort();
        loop.quit();
    });

    timer.start(timeoutMs > 0 ? timeoutMs : 5000);
    loop.exec();
    if (timer.isActive()) timer.stop();

    if (m_lastResult == FirmwareResult::Timeout) {
        reply->deleteLater();
        return;
    }

    if (reply->error() != QNetworkReply::NoError) {
        qCDebug(log_host_hid) << "FirmwareNetworkClient: firmware binary fetch failed:" << reply->errorString();
        m_lastResult = FirmwareResult::CheckFailed;
        reply->deleteLater();
        return;
    }

    const QByteArray data = reply->readAll();
    reply->deleteLater();

    m_networkFirmware.assign(data.begin(), data.end());
    qCDebug(log_host_hid) << "FirmwareNetworkClient: downloaded" << data.size() << "bytes";

    // Parse version from firmware binary header (bytes 12-15).
    auto byte_at = [&](int i) -> int {
        return (data.size() > i) ? static_cast<unsigned char>(data[i]) : 0;
    };
    m_latestVersion = QString("%1%2%3%4")
        .arg(byte_at(12), 2, 10, QChar('0'))
        .arg(byte_at(13), 2, 10, QChar('0'))
        .arg(byte_at(14), 2, 10, QChar('0'))
        .arg(byte_at(15), 2, 10, QChar('0'))
        .toStdString();
    qCDebug(log_host_hid) << "FirmwareNetworkClient: latest version parsed as:"
                          << QString::fromStdString(m_latestVersion);
}

// ─────────────────────────────────────────────────────────────────────────────
//  pickFirmwareFileNameFromIndex()  (static)
// ─────────────────────────────────────────────────────────────────────────────
// Parse an index file and pick the appropriate firmware filename for the given chip.
// Supports:
//  - legacy single-line: "filename.bin"
//  - CSV multi-line: "<version>,<filename>,<chipToken>" (one entry per line)
QString FirmwareNetworkClient::pickFirmwareFileNameFromIndex(const QString& indexContent,
                                                              VideoChipType chip)
{
    if (indexContent.trimmed().isEmpty()) return {};

    QStringList lines = indexContent.split('\n', Qt::SkipEmptyParts);
    struct Candidate { QString version; QString filename; QString chipToken; };
    QVector<Candidate> candidates;

    auto isUpgradeLike = [](const QString& text) -> bool {
        const QString t = text.toLower();
        return t.contains("upg") || t.contains("upgrade") || t.contains("boot") ||
               t.contains("loader") || t.contains("hidonly") || t.contains("tool") ||
               t.contains("ram");
    };

    for (const QString& rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (line.isEmpty() || line.startsWith('#')) continue;

        QStringList parts = line.split(',', Qt::KeepEmptyParts);
        for (QString& p : parts) p = p.trimmed();

        if (parts.size() == 1) {
            if (!parts[0].isEmpty()) {
                candidates.append({ QStringLiteral("0"), parts[0], {} });
            }
            continue;
        }
        if (parts.size() < 2) continue;
        candidates.append({
            parts[0],
            parts[1],
            parts.size() > 2 ? parts[2].toLower() : QString()
        });
    }

    auto tokenMatchesChip = [](const QString& token, VideoChipType chipType) -> bool {
        if (token.isEmpty()) return false;
        const QString t = token.toLower();
        switch (chipType) {
        case VideoChipType::MS2109:  return t.contains("2109") && !t.contains('s');
        case VideoChipType::MS2109S: return t.contains("2109s") || (t.contains("2109") && t.contains('s'));
        case VideoChipType::MS2130S: return t.contains("2130") || t.contains("2130s") || t.contains("2130_s");
        default: return false;
        }
    };

    QVector<Candidate> matched;
    for (const Candidate& c : candidates) {
        if (chip != VideoChipType::UNKNOWN && tokenMatchesChip(c.chipToken, chip))
            matched.append(c);
    }

    if (matched.isEmpty()) {
        for (const Candidate& c : candidates) {
            const QString fn = c.filename.toLower();
            if      (fn.contains("2130"))                             matched.append(c);
            else if (fn.contains("2109s") || fn.contains("2109_s"))  matched.append(c);
            else if (fn.contains("2109"))                             matched.append(c);
        }
    }

    // Prefer non-upgrader images to avoid flashing HID-only firmware.
    QVector<Candidate> nonUpgrade;
    for (const Candidate& c : matched)
        if (!isUpgradeLike(c.filename) && !isUpgradeLike(c.chipToken))
            nonUpgrade.append(c);
    if (!nonUpgrade.isEmpty()) matched = nonUpgrade;

    if (matched.isEmpty()) matched = candidates;
    if (matched.isEmpty()) return {};

    auto versionKey = [](const QString& v) -> qint64 {
        bool ok = false;
        qint64 n = v.toLongLong(&ok);
        if (ok) return n;
        QString digits;
        for (QChar ch : v) if (ch.isDigit()) digits.append(ch);
        return digits.isEmpty() ? 0 : digits.toLongLong();
    };

    Candidate best = matched.first();
    qint64 bestVer = versionKey(best.version);
    for (const Candidate& c : matched) {
        qint64 ver = versionKey(c.version);
        if (ver > bestVer) { best = c; bestVer = ver; }
    }

    if (isUpgradeLike(best.filename) || isUpgradeLike(best.chipToken)) {
        qCWarning(log_host_hid) << "FirmwareNetworkClient: refusing upgrader/bootloader candidate:"
                                << best.filename << "token:" << best.chipToken;
        return {};
    }
    return best.filename;
}
