#include "debuglogger.h"
#include <QDir>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QFileInfo>

// ---------------------------------------------------------------------------
DebugLogger &DebugLogger::instance()
{
    static DebugLogger s;
    return s;
}

DebugLogger::~DebugLogger()
{
    if (m_sessionActive)
        endSession();
}

// ---------------------------------------------------------------------------
QString DebugLogger::debugLogDirectory()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
}

QStringList DebugLogger::existingDebugLogFiles()
{
    QDir dir(debugLogDirectory());
    if (!dir.exists()) return {};

    const QStringList nameFilters = {
        QStringLiteral("AstrobinCSV_debug_*.log"),
        QStringLiteral("AstrobinCSV_debug_*.json")
    };
    QStringList found;
    const auto entries = dir.entryInfoList(nameFilters, QDir::Files, QDir::Name);
    found.reserve(entries.size());
    for (const QFileInfo &fi : entries)
        found << fi.absoluteFilePath();
    return found;
}

int DebugLogger::removeOldDebugLogs()
{
    int removed = 0;
    for (const QString &path : existingDebugLogFiles()) {
        if (QFile::remove(path))
            ++removed;
    }
    return removed;
}

// ---------------------------------------------------------------------------
void DebugLogger::setEnabled(bool on)
{
    m_enabled = on;
}

// ---------------------------------------------------------------------------
void DebugLogger::beginSession()
{
    if (!m_enabled) return;
    if (m_sessionActive) endSession();

    const QString ts   = QDateTime::currentDateTime()
                             .toString(QStringLiteral("yyyyMMdd_HHmmss"));
    const QString base = QStringLiteral("AstrobinCSV_debug_") + ts;

    const QString tmpDir = debugLogDirectory();
    QDir().mkpath(tmpDir);  // create if it doesn't exist yet
    m_humanPath = QDir(tmpDir).filePath(base + QStringLiteral(".log"));
    m_jsonPath  = QDir(tmpDir).filePath(base + QStringLiteral(".json"));

    m_humanFile.setFileName(m_humanPath);
    if (!m_humanFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        m_enabled = false;  // can't write — silently disable
        return;
    }
    m_humanStream.setDevice(&m_humanFile);

    m_jsonEntries = QJsonArray{};
    m_sessionActive = true;

    writeHuman(QStringLiteral("AstrobinCSV Debug Log — session started %1")
                   .arg(QDateTime::currentDateTime()
                            .toString(Qt::ISODateWithMs)));
    writeHuman(QStringLiteral("Human log : ") + m_humanPath);
    writeHuman(QStringLiteral("JSON  log : ") + m_jsonPath);
    writeHuman(QString(72, QLatin1Char('=')));
}

// ---------------------------------------------------------------------------
void DebugLogger::endSession()
{
    if (!m_sessionActive) return;

    writeHuman(QString(72, QLatin1Char('=')));
    writeHuman(QStringLiteral("Session ended %1")
                   .arg(QDateTime::currentDateTime()
                            .toString(Qt::ISODateWithMs)));

    m_humanStream.flush();
    m_humanFile.close();

    // Write JSON file
    QFile jf(m_jsonPath);
    if (jf.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QJsonObject root;
        root[QStringLiteral("application")] = QStringLiteral("AstrobinCSV");
        root[QStringLiteral("sessionStart")] =
            QFileInfo(m_humanPath).completeBaseName()
                .section(QLatin1Char('_'), 2);   // crude but avoids storing a member
        root[QStringLiteral("entries")] = m_jsonEntries;
        jf.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    }

    m_sessionActive = false;
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------
QString DebugLogger::timestamp() const
{
    return QDateTime::currentDateTime().toString(Qt::ISODateWithMs);
}

void DebugLogger::writeHuman(const QString &line)
{
    if (!m_sessionActive) return;
    m_humanStream << line << '\n';
    m_humanStream.flush();
}

void DebugLogger::appendJsonEntry(const QJsonObject &obj)
{
    if (!m_sessionActive) return;
    QJsonObject e = obj;
    e[QStringLiteral("ts")] = timestamp();
    m_jsonEntries.append(e);
}

// ---------------------------------------------------------------------------
// Public logging API
// ---------------------------------------------------------------------------
void DebugLogger::logSection(const QString &title)
{
    if (!m_sessionActive) return;
    const QString bar(72, QLatin1Char('-'));
    writeHuman(bar);
    writeHuman(QStringLiteral("[%1] === %2 ===").arg(timestamp(), title));
    writeHuman(bar);

    QJsonObject o;
    o[QStringLiteral("type")]  = QStringLiteral("section");
    o[QStringLiteral("title")] = title;
    appendJsonEntry(o);
}

void DebugLogger::logFileOpened(const QString &path, bool success)
{
    if (!m_sessionActive) return;
    writeHuman(QStringLiteral("[%1] FILE %2  %3")
                   .arg(timestamp(),
                        success ? QStringLiteral("OPENED") : QStringLiteral("FAILED"),
                        path));

    QJsonObject o;
    o[QStringLiteral("type")]    = QStringLiteral("file");
    o[QStringLiteral("path")]    = path;
    o[QStringLiteral("success")] = success;
    appendJsonEntry(o);
}

void DebugLogger::logPattern(const QString &patternName,
                              const QString &patternText,
                              bool           found,
                              const QString &context)
{
    if (!m_sessionActive) return;
    QString line = QStringLiteral("[%1] PATTERN %2  %-20s  %3")
                       .arg(timestamp(),
                            found ? QStringLiteral("MATCH  ") : QStringLiteral("NO-MATCH"),
                            patternName.leftJustified(22));
    if (!context.isEmpty())
        line += QStringLiteral("  context: ") + context.left(120);
    writeHuman(line);

    QJsonObject o;
    o[QStringLiteral("type")]    = QStringLiteral("pattern");
    o[QStringLiteral("name")]    = patternName;
    o[QStringLiteral("pattern")] = patternText;
    o[QStringLiteral("found")]   = found;
    if (!context.isEmpty()) o[QStringLiteral("context")] = context.left(200);
    appendJsonEntry(o);
}

void DebugLogger::logDecision(const QString &message)
{
    if (!m_sessionActive) return;
    writeHuman(QStringLiteral("[%1] DECISION  %2").arg(timestamp(), message));

    QJsonObject o;
    o[QStringLiteral("type")]    = QStringLiteral("decision");
    o[QStringLiteral("message")] = message;
    appendJsonEntry(o);
}

void DebugLogger::logResult(const QString &key, const QString &value)
{
    if (!m_sessionActive) return;
    writeHuman(QStringLiteral("[%1] RESULT    %2 = %3")
                   .arg(timestamp(), key.leftJustified(20), value));

    QJsonObject o;
    o[QStringLiteral("type")]  = QStringLiteral("result");
    o[QStringLiteral("key")]   = key;
    o[QStringLiteral("value")] = value;
    appendJsonEntry(o);
}

void DebugLogger::logWarning(const QString &message)
{
    if (!m_sessionActive) return;
    writeHuman(QStringLiteral("[%1] WARNING   %2").arg(timestamp(), message));

    QJsonObject o;
    o[QStringLiteral("type")]    = QStringLiteral("warning");
    o[QStringLiteral("message")] = message;
    appendJsonEntry(o);
}

void DebugLogger::logError(const QString &message)
{
    if (!m_sessionActive) return;
    writeHuman(QStringLiteral("[%1] ERROR     %2").arg(timestamp(), message));

    QJsonObject o;
    o[QStringLiteral("type")]    = QStringLiteral("error");
    o[QStringLiteral("message")] = message;
    appendJsonEntry(o);
}
