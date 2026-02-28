#include "pixinsightlogparser.h"
#include "settings/appsettings.h"
#include "debuglogger.h"
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>
#include <QFileInfo>

static QString stripTimestamp(const QString &line)
{
    static const QRegularExpression ts(R"(\[\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\] )");
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

QList<AcquisitionGroup> PixInsightLogParser::parse(const QString &filePath)
{
    auto &dbg = DebugLogger::instance();
    dbg.logSection(QStringLiteral("PixInsightLogParser"));
    dbg.logFileOpened(filePath);

    m_error.clear();
    QList<AcquisitionGroup> groups;

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
    dbg.logResult(QStringLiteral("totalLines"), QString::number(allLines.size()));

    static const QRegularExpression beginRe(
        R"(\* Begin (?:fast )?integration of Light frames)");
    static const QRegularExpression endRe(
        R"(\* End (?:fast )?integration of Light frames)");

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
            dbg.logWarning(QStringLiteral("No matching End marker — block %1 skipped")
                               .arg(blockIndex));
            break;
        }
        dbg.logPattern(QStringLiteral("endRe"),
                       endRe.pattern(), true,
                       stripTimestamp(allLines[endLine]).trimmed().left(100));
        dbg.logDecision(QStringLiteral("Block %1: lines %2–%3")
                            .arg(blockIndex).arg(i).arg(endLine));

        QStringList block;
        for (int k = i; k <= endLine; ++k) block << allLines[k];

        AcquisitionGroup grp;
        grp.sourceLogFile = filePath;
        if (parseBlock(block, grp, blockIndex)) {
            dbg.logDecision(
                QStringLiteral("Block %1 accepted: target='%2' filter='%3' "
                               "exposure=%4s binning=%5 frames=%6")
                    .arg(blockIndex)
                    .arg(grp.target.isEmpty() ? QStringLiteral("(none)") : grp.target,
                         grp.filter.isEmpty() ? QStringLiteral("(none)") : grp.filter)
                    .arg(grp.exposureSec)
                    .arg(grp.binning)
                    .arg(grp.xisfPaths.size()));
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
        m_error = QStringLiteral("No Light integration blocks found in log.");
        dbg.logWarning(m_error);
    }

    dbg.logResult(QStringLiteral("groupsFound"), QString::number(groups.size()));
    return groups;
}

bool PixInsightLogParser::parseBlock(const QStringList &lines,
                                      AcquisitionGroup  &grp,
                                      int                blockIdx)
{
    auto &dbg = DebugLogger::instance();

    static const QRegularExpression filterRe(R"(Filter\s*:\s*(.+))");
    static const QRegularExpression exposureRe(R"(Exposure\s*:\s*([\d.]+)s)");
    static const QRegularExpression binningRe(R"(BINNING\s*:\s*(\d+))");
    static const QRegularExpression keywordsRe(R"(Keywords\s*:\s*\[(.+)\])");
    static const QRegularExpression imagesBeginRe(R"(II\.images\s*=\s*\[)");
    static const QRegularExpression targetsBeginRe(R"(FI\.targets\s*=\s*\[)");

    int imagesStart  = -1;
    bool isFastInteg = false;

    for (int i = 0; i < lines.size(); ++i) {
        QString s = stripTimestamp(lines[i]);

        if (auto m = filterRe.match(s); m.hasMatch()) {
            grp.filter = m.captured(1).trimmed();
            dbg.logPattern(QStringLiteral("filterRe"), filterRe.pattern(),
                           true, s.trimmed().left(100));
            dbg.logResult(QStringLiteral("block[%1].filter").arg(blockIdx),
                          grp.filter);
        }
        if (auto m = exposureRe.match(s); m.hasMatch()) {
            grp.exposureSec = m.captured(1).trimmed().toDouble();
            dbg.logPattern(QStringLiteral("exposureRe"), exposureRe.pattern(),
                           true, s.trimmed().left(100));
            dbg.logResult(QStringLiteral("block[%1].exposure").arg(blockIdx),
                          QString::number(grp.exposureSec));
        }
        if (auto m = binningRe.match(s); m.hasMatch()) {
            grp.binning = m.captured(1).trimmed().toInt();
            dbg.logPattern(QStringLiteral("binningRe"), binningRe.pattern(),
                           true, s.trimmed().left(100));
            dbg.logResult(QStringLiteral("block[%1].binning").arg(blockIdx),
                          QString::number(grp.binning));
        }
        if (auto m = keywordsRe.match(s); m.hasMatch()) {
            const QString extracted = extractTarget(s);
            dbg.logPattern(QStringLiteral("keywordsRe"), keywordsRe.pattern(),
                           true, s.trimmed().left(100));
            if (extracted.isEmpty()) {
                dbg.logDecision(
                    QStringLiteral("block[%1] keywords line matched but no "
                                   "target keyword found in: %2")
                        .arg(blockIdx).arg(s.trimmed().left(100)));
            } else {
                grp.target       = extracted;
                grp.targetFromLog = true;
                dbg.logResult(QStringLiteral("block[%1].target").arg(blockIdx),
                              grp.target);
                dbg.logDecision(
                    QStringLiteral("block[%1] target set from WBPP log keyword "
                                   "— OBJECT header will not override it")
                        .arg(blockIdx));
            }
        }

        if (imagesStart < 0) {
            if (imagesBeginRe.match(s).hasMatch()) {
                imagesStart = i;
                isFastInteg = false;
                dbg.logPattern(QStringLiteral("imagesBeginRe"),
                               imagesBeginRe.pattern(), true,
                               s.trimmed().left(80));
                dbg.logDecision(
                    QStringLiteral("block[%1] image list starts at index %2 "
                                   "(standard integration)")
                        .arg(blockIdx).arg(i));
            } else if (targetsBeginRe.match(s).hasMatch()) {
                imagesStart = i;
                isFastInteg = true;
                dbg.logPattern(QStringLiteral("targetsBeginRe"),
                               targetsBeginRe.pattern(), true,
                               s.trimmed().left(80));
                dbg.logDecision(
                    QStringLiteral("block[%1] image list starts at index %2 "
                                   "(fast integration)")
                        .arg(blockIdx).arg(i));
            }
        }
    }

    if (imagesStart >= 0)
        grp.xisfPaths = extractXisfPaths(lines, imagesStart, isFastInteg);

    if (grp.xisfPaths.isEmpty())
        dbg.logWarning(
            QStringLiteral("block[%1] no .xisf paths extracted").arg(blockIdx));
    else
        dbg.logResult(QStringLiteral("block[%1].xisfCount").arg(blockIdx),
                      QString::number(grp.xisfPaths.size()));

    grp.frameDates.resize(grp.xisfPaths.size());
    grp.frameGains.fill(-1, grp.xisfPaths.size());
    grp.frameSensorTemps.fill(0, grp.xisfPaths.size());
    grp.frameHasSensorTemp.fill(false, grp.xisfPaths.size());
    grp.frameAmbTemps.fill(0, grp.xisfPaths.size());
    grp.frameHasAmbTemp.fill(false, grp.xisfPaths.size());
    grp.frameResolved.fill(false, grp.xisfPaths.size());

    return !grp.xisfPaths.isEmpty();
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
    QRegularExpression kvRe(pattern, QRegularExpression::CaseInsensitiveOption);
    auto m = kvRe.match(line);
    return m.hasMatch() ? m.captured(1).trimmed() : QString();
}
