#include "frameresolverworker.h"
#include "xisfheaderreader.h"
#include "xisfmasterframereader.h"
#include "debuglogger.h"
#include <QFile>
#include <QDir>
#include <QMutexLocker>

// ── Public API ────────────────────────────────────────────────────────────

void FrameResolveWorker::supplyDirectory(const QString &dir)
{
    QMutexLocker lk(&m_mutex);
    suppliedDir = dir;
    m_cond.wakeOne();
}

// ── Main worker entry point ───────────────────────────────────────────────

void FrameResolveWorker::run()
{
    auto &dbg = DebugLogger::instance();

    // Count total frames for progress reporting.
    int total = 0;
    for (const auto &grp : *groups) total += grp.frames.size();
    int done = 0;

    if (dbg.isSessionActive()) {
        dbg.logSection(QStringLiteral("FrameResolveWorker"));
        dbg.logResult(QStringLiteral("totalFrames"),
                      QString::number(total));
    }

    for (auto &grp : *groups) {
        for (auto &frame : grp.frames) {
            if (cancelFlag->loadAcquire()) {
                emit progress(++done, total);
                continue;
            }

            // Stage 1: resolve XISF header.
            if (resolveHeader(frame, grp.sourceLogFile)) {
                // Apply target: log keyword takes priority over OBJECT header.
                if (!frame.targetFromLog && !frame.object.isEmpty())
                    frame.logTarget = frame.object;

                // Override log-derived filter with XISF FILTER keyword
                // if present.
                if (!frame.object.isEmpty() && frame.filter.isEmpty())
                    frame.filter = frame.object; // shouldn't happen, but guard

                // Stage 2: resolve calibration chain.
                resolveCalibration(frame, grp.sourceLogFile);
            }

            emit progress(++done, total);
        }
    }

    if (dbg.isSessionActive()) {
        int resolved = 0;
        for (const auto &grp : *groups)
            for (const auto &frame : grp.frames)
                if (frame.resolved) ++resolved;
        dbg.logResult(QStringLiteral("framesResolved"),
                      QString::number(resolved));
        dbg.logResult(QStringLiteral("framesUnresolved"),
                      QString::number(total - resolved));
    }

    emit finished();
}

// ── Header resolution ─────────────────────────────────────────────────────

bool FrameResolveWorker::resolveHeader(AcquisitionFrame &frame,
                                        const QString    &sourceLogFile)
{
    QString path = frame.registeredPath;
    auto result  = XisfHeaderReader::read(path);

    // ── Primary cache ─────────────────────────────────────────────────────
    if (!result && !QFile::exists(path)) {
        const QString fn = QFileInfo(path).fileName();
        for (const QString &dir : std::as_const(m_regPrimaryCache)) {
            QString candidate = QDir(dir).filePath(fn);
            if (QFile::exists(candidate)) {
                path = candidate;
                frame.registeredPath = path;
                result = XisfHeaderReader::read(path);
                break;
            }
        }
    }

    // ── Secondary cache (recursive) ───────────────────────────────────────
    if (!result && !QFile::exists(path)) {
        const QString fn = QFileInfo(path).fileName();
        for (const QString &dir : std::as_const(m_regSecondaryCache)) {
            if (cancelFlag->loadAcquire()) break;
            QString found = findRecursive(dir, fn, cancelFlag);
            if (!found.isEmpty()) {
                QString foundDir = QFileInfo(found).absolutePath();
                m_regPrimaryCache.insert(foundDir);
                path = found;
                frame.registeredPath = path;
                result = XisfHeaderReader::read(path);
                break;
            }
        }
    }

    // ── Auto-probe: ../registered/ sibling of the log file ────────────────
    if (!result && !QFile::exists(path) && !cancelFlag->loadAcquire()) {
        const QString fn = QFileInfo(path).fileName();
        QDir logDir = QFileInfo(sourceLogFile).absoluteDir();
        QDir parent = logDir;
        if (parent.cdUp()) {
            QDir regDir(parent.filePath(QStringLiteral("registered")));
            if (regDir.exists()) {
                QString found = findRecursive(regDir.absolutePath(),
                                              fn, cancelFlag);
                if (!found.isEmpty()) {
                    QString foundDir = QFileInfo(found).absolutePath();
                    m_regPrimaryCache.insert(foundDir);
                    m_regSecondaryCache.append(foundDir);
                    path = found;
                    frame.registeredPath = path;
                    result = XisfHeaderReader::read(path);
                }
            }
        }
    }

    // ── User prompt ───────────────────────────────────────────────────────
    if (!result && !QFile::exists(path) && !cancelFlag->loadAcquire()
            && !m_regSkipPrompts) {
        QMutexLocker lk(&m_mutex);
        suppliedDir.clear();
        emit requestRegisteredDirectory(
            path, QFileInfo(sourceLogFile).absolutePath());
        m_cond.wait(&m_mutex);

        if (suppliedDir.isEmpty()) {
            m_regSkipPrompts = true;
        } else {
            m_regSecondaryCache.append(suppliedDir);
            const QString fn2  = QFileInfo(path).fileName();
            QString       found = findRecursive(suppliedDir, fn2, cancelFlag);
            if (!found.isEmpty()) {
                QString foundDir = QFileInfo(found).absolutePath();
                m_regPrimaryCache.insert(foundDir);
                path = found;
                frame.registeredPath = path;
                result = XisfHeaderReader::read(path);
            }
        }
    }

    if (!result) return false;

    frame.resolved      = true;
    frame.date          = result->date;
    frame.gain          = result->gain;
    frame.sensorTemp    = result->sensorTemp;
    frame.hasSensorTemp = result->hasSensorTemp;
    frame.ambTemp       = result->ambTemp;
    frame.hasAmbTemp    = result->hasAmbTemp;
    frame.binning       = result->binning;

    // FILTER from XISF header overrides log-derived filter if present.
    if (!result->filter.isEmpty())
        frame.filter = result->filter;

    frame.object = result->object;
    return true;
}

// ── Calibration chain resolution ──────────────────────────────────────────

void FrameResolveWorker::resolveCalibration(AcquisitionFrame &frame,
                                             const QString    &sourceLogFile)
{
    auto &dbg = DebugLogger::instance();

    const QString calBase = calibratedBasename(frame.registeredPath);
    if (calBase.isEmpty()) {
        if (dbg.isSessionActive())
            dbg.logWarning(
                QStringLiteral("  calibratedBasename: no '_c' suffix in '%1'")
                    .arg(QFileInfo(frame.registeredPath).fileName()));
        return;
    }

    // Look up the calibration block for this frame.
    auto it = basenameToBlock.find(calBase.toLower());
    if (it == basenameToBlock.end()) {
        // Try searching the calibrated directory.
        const QString calibRoot = logToCalibratedDir.value(sourceLogFile);
        const QString found     = findRecursive(calibRoot, calBase, cancelFlag);
        if (!found.isEmpty()) {
            const QString foundBase = QFileInfo(found).fileName().toLower();
            it = basenameToBlock.find(foundBase);
        }
    }

    if (it == basenameToBlock.end()) {
        if (dbg.isSessionActive())
            dbg.logWarning(
                QStringLiteral("  no calibration block for '%1'")
                    .arg(calBase));
        return;
    }

    const CalibrationBlock &blk = calBlocks[it.value()];

    frame.calibration.masterDarkPath = blk.masterDarkPath;
    frame.calibration.masterFlatPath = blk.masterFlatPath;
    frame.calibration.masterBiasPath = blk.masterBiasPath;

    frame.calibration.darks = masterFrameCount(blk.masterDarkPath,
                                                sourceLogFile);
    frame.calibration.flats = masterFrameCount(blk.masterFlatPath,
                                                sourceLogFile);

    // Bias: prefer the flatToBias chain; fall back to direct bias path.
    if (!blk.masterFlatPath.isEmpty()) {
        auto biasIt = flatToBias.find(blk.masterFlatPath.toLower());
        if (biasIt != flatToBias.end()) {
            frame.calibration.masterBiasPath = biasIt.value();
            frame.calibration.bias =
                masterFrameCount(biasIt.value(), sourceLogFile);
        }
    }
    if (frame.calibration.bias < 0 && !blk.masterBiasPath.isEmpty())
        frame.calibration.bias = masterFrameCount(blk.masterBiasPath,
                                                   sourceLogFile);
}

// ── Master file frame count ───────────────────────────────────────────────

int FrameResolveWorker::masterFrameCount(const QString &path,
                                          const QString &sourceLogFile)
{
    if (path.isEmpty()) return -1;

    auto it = m_masterCountCache.find(path);
    if (it != m_masterCountCache.end()) return it.value();

    const QString fileName   = QFileInfo(path).fileName();
    const QString masterRoot = logToMasterDir.value(sourceLogFile);

    auto tryRead = [](const QString &p) -> std::optional<int> {
        if (p.isEmpty() || !QFile::exists(p)) return std::nullopt;
        return XisfMasterFrameReader::readFrameCount(p);
    };

    // Tier 1: original path.
    if (auto v = tryRead(path)) {
        m_masterCountCache.insert(path, v.value_or(-1));
        return v.value_or(-1);
    }

    // Tier 2: ../master/ sibling of the log file.
    {
        const QString found = findRecursive(masterRoot, fileName, cancelFlag);
        if (!found.isEmpty()) {
            const int val =
                XisfMasterFrameReader::readFrameCount(found).value_or(-1);
            masterCache->primaryDirs.insert(
                QFileInfo(found).absolutePath());
            m_masterCountCache.insert(found, val);
            m_masterCountCache.insert(path, val);
            return val;
        }
    }

    // Tier 3: primary cache.
    for (const QString &dir : std::as_const(masterCache->primaryDirs)) {
        const QString candidate = QDir(dir).filePath(fileName);
        if (auto v = tryRead(candidate)) {
            m_masterCountCache.insert(path, v.value_or(-1));
            return v.value_or(-1);
        }
    }

    // Tier 4: secondary cache (recursive).
    for (const QString &dir : std::as_const(masterCache->secondaryDirs)) {
        const QString found = findRecursive(dir, fileName, cancelFlag);
        if (!found.isEmpty()) {
            const int val =
                XisfMasterFrameReader::readFrameCount(found).value_or(-1);
            const QString foundDir = QFileInfo(found).absolutePath();
            masterCache->primaryDirs.insert(foundDir);
            m_masterCountCache.insert(found, val);
            m_masterCountCache.insert(path, val);
            return val;
        }
    }

    // Tier 5: user prompt.
    if (!masterCache->skipPrompts && !cancelFlag->loadAcquire()) {
        QMutexLocker lk(&m_mutex);
        suppliedDir.clear();
        emit requestMasterDirectory(
            path,
            masterRoot.isEmpty()
                ? QFileInfo(sourceLogFile).absolutePath()
                : masterRoot);
        m_cond.wait(&m_mutex);

        if (suppliedDir.isEmpty()) {
            masterCache->skipPrompts = true;
            m_masterCountCache.insert(path, -1);
            return -1;
        }

        masterCache->secondaryDirs.append(suppliedDir);
        const QString found = findRecursive(suppliedDir, fileName, cancelFlag);
        if (!found.isEmpty()) {
            const int val =
                XisfMasterFrameReader::readFrameCount(found).value_or(-1);
            const QString foundDir = QFileInfo(found).absolutePath();
            masterCache->primaryDirs.insert(foundDir);
            m_masterCountCache.insert(found, val);
            m_masterCountCache.insert(path, val);
            return val;
        }
    }

    m_masterCountCache.insert(path, -1);
    return -1;
}

// ── Static helpers ────────────────────────────────────────────────────────

QString FrameResolveWorker::findRecursive(const QString &root,
                                           const QString &fileName,
                                           QAtomicInt    *cancel,
                                           int            depth)
{
    if (depth > kMaxDepth) return {};
    if (cancel && cancel->loadAcquire()) return {};

    QDir dir(root);
    if (dir.exists(fileName))
        return dir.filePath(fileName);

    const auto subdirs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &sub : subdirs) {
        QString hit = findRecursive(dir.filePath(sub), fileName,
                                    cancel, depth + 1);
        if (!hit.isEmpty()) return hit;
    }
    return {};
}

QString FrameResolveWorker::calibratedBasenameStatic(
    const QString &registeredPath)
{
    QString stem = QFileInfo(registeredPath).completeBaseName();
    int pos = stem.lastIndexOf(QLatin1String("_c"));
    while (pos != -1) {
        int after = pos + 2;
        if (after == stem.size() || stem[after] == QLatin1Char('_'))
            return stem.left(pos + 2) + QStringLiteral(".xisf");
        pos = stem.lastIndexOf(QLatin1String("_c"), pos - 1);
    }
    return {};
}

QString FrameResolveWorker::calibratedBasename(const QString &registeredPath)
{
    return calibratedBasenameStatic(registeredPath);
}

void FrameResolveWorker::remapGroup(IntegrationGroup  &grp,
                                     const QString     &knownDir)
{
    for (auto &frame : grp.frames) {
        const QString fn        = QFileInfo(frame.registeredPath).fileName();
        const QString candidate = QDir(knownDir).filePath(fn);
        if (QFile::exists(candidate)) {
            frame.registeredPath = candidate;
            continue;
        }
        // Search primary cache.
        for (const QString &d : std::as_const(m_regPrimaryCache)) {
            const QString c = QDir(d).filePath(fn);
            if (QFile::exists(c)) {
                frame.registeredPath = c;
                break;
            }
        }
    }
}
