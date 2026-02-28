#pragma once
#include <QString>
#include <QJsonArray>
#include <QJsonObject>
#include <QDateTime>
#include <QFile>
#include <QTextStream>

// ---------------------------------------------------------------------------
// DebugLogger — session-only (non-persistent) structured debug log.
//
// Two output files are produced in the platform temp directory:
//   AstrobinCSV_debug_<timestamp>.log   — human-readable
//   AstrobinCSV_debug_<timestamp>.json  — machine-parseable
//
// Usage:
//   DebugLogger::instance().setEnabled(true);
//   DebugLogger::instance().beginSession("MyLog.log");
//   DebugLogger::instance().logFileOpened("/path/to/file.log");
//   DebugLogger::instance().logPattern("beginRe", R"(\* Begin…)", true);
//   DebugLogger::instance().logDecision("Matched block, parsing fields");
//   DebugLogger::instance().endSession();
// ---------------------------------------------------------------------------

class DebugLogger {
public:
    static DebugLogger &instance();

    // Enable/disable; does not persist across application runs.
    void setEnabled(bool on);
    bool isEnabled() const { return m_enabled; }

    // Open new output files for a fresh import session.
    // Safe to call when disabled — becomes a no-op.
    void beginSession();

    // Flush and close output files.
    void endSession();

    // Whether a session is currently open.
    bool isSessionActive() const { return m_sessionActive; }

    // The directory where debug logs are written.
    // Returns the path whether or not a session is active.
    static QString debugLogDirectory();

    // Returns the list of existing debug log files (*.log and *.json)
    // found in debugLogDirectory().  Used at startup to offer cleanup.
    static QStringList existingDebugLogFiles();

    // Deletes all files returned by existingDebugLogFiles().
    // Returns the number of files deleted.
    static int removeOldDebugLogs();

    // Paths to the files produced by the most recent session.
    QString humanLogPath() const { return m_humanPath; }
    QString jsonLogPath()  const { return m_jsonPath;  }

    // ── Logging primitives ────────────────────────────────────────────────

    // Top-level section header (e.g. "=== PixInsight Log Parser ===").
    void logSection(const QString &title);

    // A file that was opened (or attempted).
    void logFileOpened(const QString &path, bool success = true);

    // A regex pattern match attempt.
    // patternName  — short symbolic name, e.g. "beginRe"
    // patternText  — the actual regex source
    // found        — whether it matched
    // context      — optional: the line / value it matched against
    void logPattern(const QString &patternName,
                    const QString &patternText,
                    bool           found,
                    const QString &context = {});

    // A decision or note derived from parsing results.
    void logDecision(const QString &message);

    // A key=value result (e.g. filter extracted, date parsed).
    void logResult(const QString &key, const QString &value);

    // A warning that does not prevent completion.
    void logWarning(const QString &message);

    // An error.
    void logError(const QString &message);

private:
    DebugLogger() = default;
    ~DebugLogger();

    void writeHuman(const QString &line);
    void appendJsonEntry(const QJsonObject &obj);
    QString timestamp() const;

    bool    m_enabled      {false};
    bool    m_sessionActive{false};
    QString m_humanPath;
    QString m_jsonPath;

    QFile        m_humanFile;
    QTextStream  m_humanStream;
    QJsonArray   m_jsonEntries;  // accumulated; flushed on endSession()
};
