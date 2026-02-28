#include "xisfresolveworker.h"
#include <QFile>
#include <QDir>
#include <QMutexLocker>

QString XisfResolveWorker::probeRegisteredSibling(const QString &logFilePath,
                                                   const QString &fileName,
                                                   QAtomicInt    *cancel)
{
    QDir logDir = QFileInfo(logFilePath).absoluteDir();
    QDir parent = logDir;
    if (!parent.cdUp())
        return {};

    QDir regDir(parent.filePath(QStringLiteral("registered")));
    if (!regDir.exists())
        return {};

    return findRecursive(regDir.absolutePath(), fileName, cancel);
}

void XisfResolveWorker::supplyDirectory(const QString &dir)
{
    QMutexLocker lk(&m_mutex);
    suppliedDir = dir;
    m_cond.wakeOne();
}

void XisfResolveWorker::run()
{
    QSet<QString>  primaryCache;
    QList<QString> secondaryCache;

    int done = 0;
    for (auto &grp : *groups) {
        bool groupSkipped = false;

        for (int i = 0; i < grp.xisfPaths.size(); ++i) {
            if (cancelFlag->loadAcquire()) { emit progress(++done); continue; }
            if (groupSkipped)              { emit progress(++done); continue; }

            QString path   = grp.xisfPaths[i];
            auto    result = XisfHeaderReader::read(path);

            // ── Primary cache: exact-directory fast lookup ────────────────
            if (!result && !QFile::exists(path)) {
                const QString fn = QFileInfo(path).fileName();
                for (const QString &dir : std::as_const(primaryCache)) {
                    QString candidate = QDir(dir).filePath(fn);
                    if (QFile::exists(candidate)) {
                        remapGroup(grp, dir, primaryCache, secondaryCache);
                        path   = grp.xisfPaths[i];
                        result = XisfHeaderReader::read(path);
                        break;
                    }
                }
            }

            // ── Secondary cache: depth-limited recursive search ───────────
            if (!result && !QFile::exists(path)) {
                const QString fn = QFileInfo(path).fileName();
                for (const QString &dir : std::as_const(secondaryCache)) {
                    if (cancelFlag->loadAcquire()) break;
                    QString found = findRecursive(dir, fn, cancelFlag);
                    if (!found.isEmpty()) {
                        QString foundDir = QFileInfo(found).absolutePath();
                        primaryCache.insert(foundDir);
                        remapGroup(grp, foundDir, primaryCache, secondaryCache);
                        path   = grp.xisfPaths[i];
                        result = XisfHeaderReader::read(path);
                        break;
                    }
                }
            }

            // ── Auto-probe: ../registered/ sibling of the log file ────────
            if (!result && !QFile::exists(path)
                    && !cancelFlag->loadAcquire()) {
                const QString fn    = QFileInfo(path).fileName();
                QString       found = XisfResolveWorker::probeRegisteredSibling(
                                          grp.sourceLogFile, fn, cancelFlag);
                if (!found.isEmpty()) {
                    QString foundDir = QFileInfo(found).absolutePath();
                    primaryCache.insert(foundDir);
                    secondaryCache.append(foundDir);
                    remapGroup(grp, foundDir, primaryCache, secondaryCache);
                    path   = grp.xisfPaths[i];
                    result = XisfHeaderReader::read(path);
                }
            }

            // ── Still missing — ask the main thread ───────────────────────
            if (!result && !QFile::exists(path)
                    && !cancelFlag->loadAcquire()) {
                QMutexLocker lk(&m_mutex);
                suppliedDir.clear();
                emit requestDirectory(path,
                    QFileInfo(grp.sourceLogFile).absolutePath());
                m_cond.wait(&m_mutex);

                if (suppliedDir.isEmpty()) {
                    groupSkipped = true;
                } else {
                    secondaryCache.append(suppliedDir);
                    const QString fn2   = QFileInfo(path).fileName();
                    QString       found = findRecursive(suppliedDir, fn2,
                                                        cancelFlag);
                    if (!found.isEmpty()) {
                        QString foundDir = QFileInfo(found).absolutePath();
                        primaryCache.insert(foundDir);
                        remapGroup(grp, foundDir,
                                   primaryCache, secondaryCache);
                        path   = grp.xisfPaths[i];
                        result = XisfHeaderReader::read(path);
                    }
                }
            }

            grp.frameResolved[i] = result.has_value();
            if (result) {
                grp.frameDates[i]         = result->date;
                grp.frameGains[i]         = result->gain;
                grp.frameSensorTemps[i]   = result->sensorTemp;
                grp.frameHasSensorTemp[i] = result->hasSensorTemp;
                grp.frameAmbTemps[i]      = result->ambTemp;
                grp.frameHasAmbTemp[i]    = result->hasAmbTemp;

                if (!result->filter.isEmpty() && grp.filterFromXisf.isEmpty()) {
                    grp.filter         = result->filter;
                    grp.filterFromXisf = result->filter;
                }

                // Only promote the OBJECT keyword to grp.target when the
                // target was NOT already matched from a user-defined Target
                // Keyword in the WBPP log. When targetFromLog is true the
                // log-derived value takes priority and must be preserved so
                // that separate WBPP integration blocks remain distinct.
                if (!result->object.isEmpty()
                        && grp.targetFromXisf.isEmpty()
                        && !grp.targetFromLog) {
                    grp.target         = result->object;
                    grp.targetFromXisf = result->object;
                }
            }
            emit progress(++done);
        }
    }
    emit finished();
}

QString XisfResolveWorker::findRecursive(const QString &root,
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

void XisfResolveWorker::remapGroup(AcquisitionGroup     &grp,
                                    const QString        &knownDir,
                                    const QSet<QString>  &primary,
                                    const QList<QString> &secondary)
{
    for (int j = 0; j < grp.xisfPaths.size(); ++j) {
        const QString fn = QFileInfo(grp.xisfPaths[j]).fileName();

        QString candidate = QDir(knownDir).filePath(fn);
        if (QFile::exists(candidate)) {
            grp.xisfPaths[j] = candidate;
            continue;
        }
        for (const QString &d : primary) {
            candidate = QDir(d).filePath(fn);
            if (QFile::exists(candidate)) {
                grp.xisfPaths[j] = candidate;
                goto next;
            }
        }
        for (const QString &d : secondary) {
            QString hit = findRecursive(d, fn, nullptr);
            if (!hit.isEmpty()) {
                grp.xisfPaths[j] = hit;
                goto next;
            }
        }
        next:;
    }
}
