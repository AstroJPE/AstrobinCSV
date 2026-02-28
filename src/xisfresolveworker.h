#pragma once
#include <QObject>
#include <QAtomicInt>
#include <QMutex>
#include <QWaitCondition>
#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QList>
#include "models/acquisitiongroup.h"
#include "xisfheaderreader.h"

// ── XisfResolveWorker ─────────────────────────────────────────────────────
//
// Runs on a background thread. Resolves .xisf frame paths using a two-level
// directory cache and depth-limited recursive search.
//
// Resolution order for each missing file:
//   1. Primary cache  : exact directories where a file was previously found.
//   2. Secondary cache: user-supplied directories searched recursively.
//   3. Auto-probe     : ../registered/ sibling of the log file (standard
//                       WBPP output layout).
//   4. User prompt    : requestDirectory() signal blocks until the main
//                       thread calls supplyDirectory().
// ─────────────────────────────────────────────────────────────────────────
class XisfResolveWorker : public QObject {
    Q_OBJECT
public:
    static constexpr int kMaxDepth = 4;

    QList<AcquisitionGroup> *groups{nullptr};
    QAtomicInt              *cancelFlag{nullptr};

    // Main thread writes here then calls supplyDirectory().
    QString suppliedDir;

    // Called by the main thread to unblock run() after a directory prompt.
    void supplyDirectory(const QString &dir);

signals:
    void progress(int value);
    void requestDirectory(const QString &missingPath, const QString &startDir);
    void finished();

public slots:
    void run();

    // Public so that mainwindow.cpp helpers can reuse the same depth-limited
    // recursive search without duplicating the implementation.
    static QString findRecursive(const QString &root,
                                 const QString &fileName,
                                 QAtomicInt    *cancel,
                                 int            depth = 0);

    // Probes the ../registered/ directory tree relative to the log file.
    // Returns the full path to fileName if found, or an empty string.
    static QString probeRegisteredSibling(const QString &logFilePath,
                                          const QString &fileName,
                                          QAtomicInt    *cancel);

private:
    QMutex         m_mutex;
    QWaitCondition m_cond;

    static void remapGroup(AcquisitionGroup     &grp,
                           const QString        &knownDir,
                           const QSet<QString>  &primary,
                           const QList<QString> &secondary);
};
