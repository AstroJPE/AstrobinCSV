#include "pixinsightlogparser.h"
#include "settings/appsettings.h"
#include "debuglogger.h"
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>
#include <QFileInfo>

static QString stripTimestamp(const QString &line)
{
    static const QRegularExpression ts(
        R"(\[\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\] )");
    return QString(line).remove(ts);
}

bool PixInsightLogParser::canParse(const QString &filePath) const
{
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
    QTextStream in(&f);
    for (int i = 0; i < 10 && !in.atEnd(); ++i) {
        QString line = in.readLine();
        if (line.contains("PixInsight Core") ||
            line.contains("Weighted Batch Preprocessing") ||
            line.contains("fast integration"))
            return true;
    }
    return false;
}

QList<IntegrationGroup> PixInsightLogParser::parse(const QString &filePath)
{
    auto &dbg = DebugLogger::instance();
    dbg.logSection(QStringLiteral("PixInsightLogParser"));
    dbg.logFileOpened(filePath);

    m_error.clear();
    QList<IntegrationGroup> groups;

    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_error = QStringLiteral("Cannot open file: ") + filePath;
        dbg.logFileOpened(filePath, false);
        dbg.logError(m_error);
        return groups;
    }

    QStringList allLines;
    QTextStream in(&f);
    while (!in.atEnd()) allLines << in.readLine();
    dbg.logResult(QStringLiteral("totalLines"),
                  QString::number(allLines.size()));

    static const QRegularExpression beginRe(
        R"(\* Begin (?:fast )?integration of Light frames)");
    static const QRegularExpression endRe(
        R"(\* End (?:fast )?integration of Light frames)");
    static const QRegularExpression writingMasterRe(
        R"(\* Writing master Light frame:)");

    int blockIndex = 0;
    int n = allLines.size();
    for (int i = 0; i < n; ++i) {
        const QString stripped = stripTimestamp(allLines[i]);
        if (!stripped.contains(beginRe)) continue;

        dbg.logPattern(QStringLiteral("beginRe"),
                       beginRe.pattern(), true,
                       stripped.trimmed().left(100));

        int endLine = -1;
        for (int j = i + 1; j < n; ++j) {
            if (stripTimestamp(allLines[j]).contains(endRe)) {
                endLine = j; break;
            }
        }
        if (endLine < 0) {
            dbg.logPattern(QStringLiteral("endRe"), endRe.pattern(), false);
            dbg.logWarning(
                QStringLiteral("No matching End marker — block %1 skipped")
                    .arg(blockIndex));
            break;
        }
        dbg.logPattern(QStringLiteral("endRe"),
                       endRe.pattern(), true,
                       stripTimestamp(allLines[endLine]).trimmed().left(100));
        dbg.logDecision(QStringLiteral("Block %1: lines %2–%3")
                            .arg(blockIndex).arg(i).arg(endLine));

        // Skip Local Normalization reference integration blocks.
        // The "* Writing master Light frame:" line and its filename appear
        // inside the block before the End marker.
        bool isLnReference = false;
        for (int j = i + 1; j < endLine; ++j) {
            const QString s = stripTimestamp(allLines[j]).trimmed();
            if (s.contains(writingMasterRe)) {
                if (j + 1 <= endLine) {
                    const QString fname =
                        QFileInfo(stripTimestamp(allLines[j + 1]).trimmed())
                            .fileName();
                    if (fname.startsWith(QLatin1String("LN_Reference_"),
                                         Qt::CaseInsensitive))
                        isLnReference = true;
                }
                break;
            }
        }

        if (isLnReference) {
            dbg.logDecision(
                QStringLiteral("Block %1: skipped — produces LN_Reference_ "
                               "master (Local Normalization integration)")
                    .arg(blockIndex));
            ++blockIndex;
            i = endLine;
            continue;
        }

        QStringList block;
        block.reserve(endLine - i + 1);
        for (int k = i; k <= endLine; ++k) block << allLines[k];

        IntegrationGroup grp;
        grp.sourceLogFile = filePath;
        grp.sessionIndex  = blockIndex;

        if (parseBlock(block, grp, blockIndex)) {
            dbg.logDecision(
                QStringLiteral("Block %1 accepted: target='%2' filter='%3' "
                               "exposure=%4s frames=%5")
                    .arg(blockIndex)
                    .arg(grp.logTarget.isEmpty()
                             ? QStringLiteral("(none)") : grp.logTarget,
                         grp.frames.isEmpty()
                             ? QStringLiteral("(none)")
                             : grp.frames.first().filter.isEmpty()
                                   ? QStringLiteral("(none)")
                                   : grp.frames.first().filter)
                    .arg(grp.exposureSec)
                    .arg(grp.frames.size()));
            groups << grp;
        } else {
            dbg.logWarning(
                QStringLiteral("Block %1 rejected (no .xisf paths found)")
                    .arg(blockIndex));
        }

        ++blockIndex;
        i = endLine;
    }

    if (groups.isEmpty() && m_error.isEmpty()) {
        m_error =
            QStringLiteral("No Light integration blocks found in log.");
        dbg.logWarning(m_error);
    }

    dbg.logResult(QStringLiteral("groupsFound"),
                  QString::number(groups.size()));
    return groups;
}

bool PixInsightLogParser::parseBlock(const QStringList &lines,
                                      IntegrationGroup  &grp,
                                      int                blockIdx)
{
    auto &dbg = DebugLogger::instance();

    static const QRegularExpression filterRe(R"(Filter\s*:\s*(.+))");
    static const QRegularExpression exposureRe(R"(Exposure\s*:\s*([\d.]+)s)");
    static const QRegularExpression keywordsRe(R"(Keywords\s*:\s*\[(.+)\])");
    static const QRegularExpression imagesBeginRe(R"(II\.images\s*=\s*\[)");
    static const QRegularExpression targetsBeginRe(R"(FI\.targets\s*=\s*\[)");

    QString filterStr;
    int     imagesStart  = -1;
    bool    isFastInteg  = false;

    for (int i = 0; i < lines.size(); ++i) {
        QString s = stripTimestamp(lines[i]);

        if (auto m = filterRe.match(s); m.hasMatch()) {
            filterStr = m.captured(1).trimmed();
            dbg.logPattern(QStringLiteral("filterRe"),
                           filterRe.pattern(), true,
                           s.trimmed().left(100));
            dbg.logResult(
                QStringLiteral("block[%1].filter").arg(blockIdx), filterStr);
        }

        if (auto m = exposureRe.match(s); m.hasMatch()) {
            grp.exposureSec = m.captured(1).trimmed().toDouble();
            dbg.logPattern(QStringLiteral("exposureRe"),
                           exposureRe.pattern(), true,
                           s.trimmed().left(100));
            dbg.logResult(
                QStringLiteral("block[%1].exposure").arg(blockIdx),
                QString::number(grp.exposureSec));
        }

        if (auto m = keywordsRe.match(s); m.hasMatch()) {
            const QString extracted = extractTarget(s);
            dbg.logPattern(QStringLiteral("keywordsRe"),
                           keywordsRe.pattern(), true,
                           s.trimmed().left(100));
            if (extracted.isEmpty()) {
                dbg.logDecision(
                    QStringLiteral("block[%1] keywords line matched but no "
                                   "target keyword found in: %2")
                        .arg(blockIdx).arg(s.trimmed().left(100)));
            } else {
                grp.logTarget       = extracted;
                grp.targetFromLog   = true;
                dbg.logResult(
                    QStringLiteral("block[%1].target").arg(blockIdx),
                    grp.logTarget);
                dbg.logDecision(
                    QStringLiteral("block[%1] target set from WBPP log "
                                   "keyword — OBJECT header will not "
                                   "override it").arg(blockIdx));
            }
        }

        if (imagesStart < 0) {
            if (imagesBeginRe.match(s).hasMatch()) {
                imagesStart = i;
                isFastInteg = false;
                dbg.logPattern(QStringLiteral("imagesBeginRe"),
                               imagesBeginRe.pattern(), true,
                               s.trimmed().left(80));
            } else if (targetsBeginRe.match(s).hasMatch()) {
                imagesStart = i;
                isFastInteg = true;
                dbg.logPattern(QStringLiteral("targetsBeginRe"),
                               targetsBeginRe.pattern(), true,
                               s.trimmed().left(80));
            }
        }
    }

    if (imagesStart < 0) return false;

    const QStringList paths =
        extractXisfPaths(lines, imagesStart, isFastInteg);

    if (paths.isEmpty()) return false;

    dbg.logResult(QStringLiteral("block[%1].xisfCount").arg(blockIdx),
                  QString::number(paths.size()));

    // Create one AcquisitionFrame per registered path, pre-populated with
    // the data available from the log.
    grp.frames.reserve(paths.size());
    for (const QString &p : paths) {
        AcquisitionFrame frame;
        frame.registeredPath = p;
        frame.exposureSec    = grp.exposureSec;
        frame.logTarget      = grp.logTarget;
        frame.targetFromLog  = grp.targetFromLog;
        frame.filter         = filterStr;  // log-derived; may be overridden
                                           // by FILTER keyword from XISF header
        grp.frames << frame;
    }

    return true;
}

QStringList PixInsightLogParser::extractXisfPaths(const QStringList &lines,
                                                    int startLine,
                                                    bool isFastInteg)
{
    QStringList paths;
    static const QRegularExpression pathRe(
        R"(\[(?:true|false),\s*\"([^\"]+\.xisf)\")");

    Q_UNUSED(isFastInteg)

    bool inArray = false;
    for (int i = startLine; i < lines.size(); ++i) {
        QString s = stripTimestamp(lines[i]);
        if (!inArray) {
            if ((s.contains("II.images") || s.contains("FI.targets"))
                    && s.contains('['))
                inArray = true;
            continue;
        }
        if (s.trimmed().startsWith("];")) break;
        auto m = pathRe.match(s);
        if (m.hasMatch()) paths << m.captured(1);
    }
    return paths;
}

QString PixInsightLogParser::extractTarget(const QString &line)
{
    const QStringList keywords = AppSettings::instance().targetKeywords();
    if (keywords.isEmpty()) return {};

    QStringList escaped;
    for (const QString &kw : keywords)
        escaped << QRegularExpression::escape(kw);
    const QString pattern =
        QStringLiteral(R"((?:%1)\s*:\s*([^\],]+))").arg(escaped.join('|'));
    QRegularExpression kvRe(pattern,
                            QRegularExpression::CaseInsensitiveOption);
    auto m = kvRe.match(line);
    return m.hasMatch() ? m.captured(1).trimmed() : QString();
}
