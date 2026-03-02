#pragma once
#include <QObject>
#include <QAtomicInt>
#include <QMutex>
#include <QWaitCondition>
#include <QHash>
#include <QDir>
#include <QFileInfo>
#include <QSet>
#include "models/integrationgroup.h"
#include "logparser/calibrationlogparser.h"
#include "masterfilecache.h"

// ── FrameResolveWorker ────────────────────────────────────────────────────
//
// Runs on a background thread. For each AcquisitionFrame in each
// IntegrationGroup:
//   1. Reads the XISF header (DATE-LOC, GAIN, SET-TEMP, FILTER, OBJECT,
//      AMBTEMP, XBINNING).
//   2. Resolves the calibration chain: registered path → calibrated basename
//      → Light calibration block → master dark/flat paths → frame counts.
//      Master flat → flat integration block → master bias path → bias count.
//
// Missing registered frames and missing master calibration files are handled
// by emitting request signals that block the worker thread until the main
// thread calls supplyDirectory().
// ─────────────────────────────────────────────────────────────────────────
class FrameResolveWorker : public QObject {
    Q_OBJECT
public:
    static constexpr int kMaxDepth = 4;

    // Set by MainWindow before starting the thread.
    QList<IntegrationGroup>        *groups{nullptr};
    QAtomicInt                     *cancelFlag{nullptr};
    MasterFileCache                *masterCache{nullptr};

    // Calibration lookup structures built by MainWindow from all loaded logs
    // before the worker starts.  Read-only from the worker's perspective.
    QHash<QString, int>             basenameToBlock;  // lower-case _c.xisf basename → block index
    QList<CalibrationBlock>         calBlocks;
    QHash<QString, QString>         flatToBias;       // lower-case flat path → bias path
    QHash<QString, QString>         logToMasterDir;   // log path → ../master dir
    QHash<QString, QString>         logToCalibratedDir; // log path → ../calibrated dir

    // Written by the main thread after a directory prompt, then
    // supplyDirectory() is called to unblock the worker.
    QString suppliedDir;

    void supplyDirectory(const QString &dir);

signals:
    void progress(int framesProcessed, int framesTotal);
    void requestRegisteredDirectory(const QString &missingPath,
                                    const QString &startDir);
    void requestMasterDirectory(const QString &missingPath,
                                const QString &startDir);
    void finished();

public slots:
    void run();

    static QString findRecursive(const QString &root,
                                 const QString &fileName,
                                 QAtomicInt    *cancel,
                                 int            depth = 0);

    // Exposed as public static so MainWindow can use it for back-filling
    // calibration data on already-loaded frames after a supplementary log
    // is added.
    static QString calibratedBasenameStatic(const QString &registeredPath);

private:
    QMutex         m_mutex;
    QWaitCondition m_cond;

    // Frame count cache — keyed by absolute master file path.
    QHash<QString, int> m_masterCountCache;

    // Registered frame path remapping cache.
    QSet<QString>  m_regPrimaryCache;   // exact dirs where reg frames were found
    QList<QString> m_regSecondaryCache; // user-supplied dirs (recursive search)
    bool           m_regSkipPrompts{false};

    // Resolve the XISF header for a single frame, searching for the file
    // if it is not at its original path.
    bool resolveHeader(AcquisitionFrame &frame,
                       const QString    &sourceLogFile);

    // Resolve the calibration chain for a single frame.
    void resolveCalibration(AcquisitionFrame &frame,
                            const QString    &sourceLogFile);

    // Read or return cached frame count for a master .xisf file.
    int masterFrameCount(const QString &path,
                         const QString &sourceLogFile);

    // Remap all registered paths in a group to a newly discovered directory.
    void remapGroup(IntegrationGroup    &grp,
                    const QString       &knownDir);

    // Derive the calibrated (_c.xisf) basename from a registered path.
    static QString calibratedBasename(const QString &registeredPath);
};
