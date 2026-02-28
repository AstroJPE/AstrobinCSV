#include "calibrationlogparser.h"
#include "debuglogger.h"
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>

static QString stripTs(const QString &line)
{
    static const QRegularExpression ts(R"(\[\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\] )");
    return QString(line).remove(ts);
}

// ---------------------------------------------------------------------------
QList<CalibrationBlock> CalibrationLogParser::parse(const QString &filePath)
{
    auto &dbg = DebugLogger::instance();
    dbg.logSection(QStringLiteral("CalibrationLogParser::parse (Light frames)"));
    dbg.logFileOpened(filePath);

    m_error.clear();
    QList<CalibrationBlock> blocks;

    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_error = QStringLiteral("Cannot open file: ") + filePath;
        dbg.logFileOpened(filePath, false);
        dbg.logError(m_error);
        return blocks;
    }

    QStringList all;
    QTextStream in(&f);
    while (!in.atEnd()) all << in.readLine();
    dbg.logResult(QStringLiteral("totalLines"), QString::number(all.size()));

    static const QRegularExpression beginRe(
        R"(\* Begin calibration of Light frames)");
    static const QRegularExpression endRe(
        R"(\* End calibration of Light frames)");
    static const QRegularExpression calFrameRe(
        R"(Calibration frame \d+:\s*.+\s*--->\s*(.+\.xisf))");

    int n = all.size();
    for (int i = 0; i < n; ++i) {
        const QString s = stripTs(all[i]);
        if (!s.contains(beginRe)) continue;

        dbg.logPattern(QStringLiteral("beginRe (Light cal)"),
                       beginRe.pattern(), true, s.trimmed().left(100));

        int endLine = -1;
        for (int j = i + 1; j < n; ++j) {
            if (stripTs(all[j]).contains(endRe)) { endLine = j; break; }
        }
        if (endLine < 0) {
            dbg.logPattern(QStringLiteral("endRe (Light cal)"),
                           endRe.pattern(), false);
            dbg.logWarning(QStringLiteral("No End marker found for Light cal block starting at line %1").arg(i));
            break;
        }
        dbg.logDecision(QStringLiteral("Light cal block: lines %1–%2").arg(i).arg(endLine));

        QStringList block;
        block.reserve(endLine - i + 1);
        for (int k = i; k <= endLine; ++k) block << all[k];

        CalibrationBlock blk = parseBlock(block);

        dbg.logResult(QStringLiteral("masterDark"), blk.masterDarkPath.isEmpty()
                          ? QStringLiteral("(none)") : blk.masterDarkPath);
        dbg.logResult(QStringLiteral("masterFlat"), blk.masterFlatPath.isEmpty()
                          ? QStringLiteral("(none)") : blk.masterFlatPath);
        dbg.logResult(QStringLiteral("masterBias"), blk.masterBiasPath.isEmpty()
                          ? QStringLiteral("(none)") : blk.masterBiasPath);

        int calPathsBefore = blk.calibratedPaths.size();
        for (int j = endLine + 1; j < n; ++j) {
            const QString cs = stripTs(all[j]);
            if (cs.contains(beginRe) || cs.contains(endRe)) break;
            if (auto m = calFrameRe.match(cs); m.hasMatch())
                blk.calibratedPaths << m.captured(1).trimmed();
        }
        int calPathsAdded = blk.calibratedPaths.size() - calPathsBefore;
        dbg.logResult(QStringLiteral("calibratedOutputPaths"),
                      QString::number(blk.calibratedPaths.size()));
        if (calPathsAdded > 0)
            dbg.logDecision(
                QStringLiteral("Found %1 'Calibration frame N: … ---> …' entries after End marker")
                    .arg(calPathsAdded));
        else
            dbg.logDecision(
                QStringLiteral("No 'Calibration frame N:' summary lines found after End marker"));

        blocks << blk;
        i = endLine;
    }

    dbg.logResult(QStringLiteral("lightCalBlocksFound"), QString::number(blocks.size()));
    return blocks;
}

// ---------------------------------------------------------------------------
CalibrationBlock CalibrationLogParser::parseBlock(const QStringList &lines)
{
    CalibrationBlock blk;

    bool darkEnabled = false;
    bool flatEnabled = false;
    bool biasEnabled = false;

    static const QRegularExpression darkEnabledRe(
        R"(IC\.masterDarkEnabled\s*=\s*(true|false))");
    static const QRegularExpression flatEnabledRe(
        R"(IC\.masterFlatEnabled\s*=\s*(true|false))");
    static const QRegularExpression biasEnabledRe(
        R"(IC\.masterBiasEnabled\s*=\s*(true|false))");
    static const QRegularExpression darkPathRe(
        R"(IC\.masterDarkPath\s*=\s*\"([^\"]+)\")");
    static const QRegularExpression flatPathRe(
        R"(IC\.masterFlatPath\s*=\s*\"([^\"]+)\")");
    static const QRegularExpression biasPathRe(
        R"(IC\.masterBiasPath\s*=\s*\"([^\"]+)\")");
    static const QRegularExpression biasSummaryRe(
        R"(Master bias:\s*(.+\.xisf))");

    auto &dbg = DebugLogger::instance();

    for (const QString &raw : lines) {
        const QString s = stripTs(raw);

        if (auto m = darkEnabledRe.match(s); m.hasMatch()) {
            darkEnabled = (m.captured(1) == QLatin1String("true"));
            dbg.logPattern(QStringLiteral("darkEnabledRe"),
                           darkEnabledRe.pattern(), true, s.trimmed().left(80));
            dbg.logDecision(QStringLiteral("masterDarkEnabled = %1")
                                .arg(darkEnabled ? "true" : "false"));
        }
        if (auto m = flatEnabledRe.match(s); m.hasMatch()) {
            flatEnabled = (m.captured(1) == QLatin1String("true"));
            dbg.logPattern(QStringLiteral("flatEnabledRe"),
                           flatEnabledRe.pattern(), true, s.trimmed().left(80));
            dbg.logDecision(QStringLiteral("masterFlatEnabled = %1")
                                .arg(flatEnabled ? "true" : "false"));
        }
        if (auto m = biasEnabledRe.match(s); m.hasMatch()) {
            biasEnabled = (m.captured(1) == QLatin1String("true"));
            dbg.logPattern(QStringLiteral("biasEnabledRe"),
                           biasEnabledRe.pattern(), true, s.trimmed().left(80));
            dbg.logDecision(QStringLiteral("masterBiasEnabled = %1")
                                .arg(biasEnabled ? "true" : "false"));
        }

        if (darkEnabled && blk.masterDarkPath.isEmpty()) {
            if (auto m = darkPathRe.match(s); m.hasMatch()) {
                blk.masterDarkPath = m.captured(1).trimmed();
                dbg.logPattern(QStringLiteral("darkPathRe"),
                               darkPathRe.pattern(), true, s.trimmed().left(100));
                dbg.logResult(QStringLiteral("masterDarkPath"), blk.masterDarkPath);
            }
        }
        if (flatEnabled && blk.masterFlatPath.isEmpty()) {
            if (auto m = flatPathRe.match(s); m.hasMatch()) {
                blk.masterFlatPath = m.captured(1).trimmed();
                dbg.logPattern(QStringLiteral("flatPathRe"),
                               flatPathRe.pattern(), true, s.trimmed().left(100));
                dbg.logResult(QStringLiteral("masterFlatPath"), blk.masterFlatPath);
            }
        }
        if (blk.masterBiasPath.isEmpty()) {
            if (auto m = biasPathRe.match(s); m.hasMatch()) {
                blk.masterBiasPath = m.captured(1).trimmed();
                dbg.logPattern(QStringLiteral("biasPathRe"),
                               biasPathRe.pattern(), true, s.trimmed().left(100));
                dbg.logResult(QStringLiteral("masterBiasPath (script)"),
                              blk.masterBiasPath);
            } else if (auto m = biasSummaryRe.match(s); m.hasMatch()) {
                QString candidate = m.captured(1).trimmed();
                if (!candidate.isEmpty() &&
                        candidate.compare(QLatin1String("none"),
                                          Qt::CaseInsensitive) != 0) {
                    blk.masterBiasPath = candidate;
                    dbg.logPattern(QStringLiteral("biasSummaryRe"),
                                   biasSummaryRe.pattern(), true,
                                   s.trimmed().left(100));
                    dbg.logResult(QStringLiteral("masterBiasPath (summary)"),
                                  blk.masterBiasPath);
                }
            }
        }
    }

    if (!darkEnabled && !blk.masterDarkPath.isEmpty()) {
        dbg.logDecision(QStringLiteral("darkEnabled=false → clearing masterDarkPath"));
        blk.masterDarkPath.clear();
    }
    if (!flatEnabled && !blk.masterFlatPath.isEmpty()) {
        dbg.logDecision(QStringLiteral("flatEnabled=false → clearing masterFlatPath"));
        blk.masterFlatPath.clear();
    }

    return blk;
}

// ---------------------------------------------------------------------------
QString CalibrationLogParser::parseFlatCalibrationBlock(const QStringList &lines)
{
    static const QRegularExpression biasSummaryRe(
        R"(Master bias:\s*(.+\.xisf))");
    static const QRegularExpression biasPathRe(
        R"(IC\.masterBiasPath\s*=\s*\"([^\"]+)\")");

    auto &dbg = DebugLogger::instance();

    for (const QString &raw : lines) {
        const QString s = stripTs(raw);
        if (auto m = biasPathRe.match(s); m.hasMatch()) {
            dbg.logPattern(QStringLiteral("biasPathRe (flat cal)"),
                           biasPathRe.pattern(), true, s.trimmed().left(100));
            return m.captured(1).trimmed();
        }
        if (auto m = biasSummaryRe.match(s); m.hasMatch()) {
            QString v = m.captured(1).trimmed();
            if (!v.isEmpty() &&
                    v.compare(QLatin1String("none"), Qt::CaseInsensitive) != 0) {
                dbg.logPattern(QStringLiteral("biasSummaryRe (flat cal)"),
                               biasSummaryRe.pattern(), true,
                               s.trimmed().left(100));
                return v;
            }
        }
    }
    dbg.logDecision(QStringLiteral("Flat-cal bias: no path found in block"));
    return {};
}

// ---------------------------------------------------------------------------
QString CalibrationLogParser::parseFlatIntegrationBlock(const QStringList &lines)
{
    static const QRegularExpression addMasterRe(
        R"(Add the master file:\s*(.+\.xisf))");

    auto &dbg = DebugLogger::instance();
    bool nextLineIsPath = false;

    for (const QString &raw : lines) {
        const QString s = stripTs(raw).trimmed();
        if (nextLineIsPath) {
            if (!s.isEmpty() && s.endsWith(QLatin1String(".xisf"))) {
                dbg.logDecision(
                    QStringLiteral("Flat integration master: path on next line = %1").arg(s));
                return s;
            }
            nextLineIsPath = false;
        }
        if (s.contains(QLatin1String("Writing master Flat frame"))) {
            dbg.logPattern(QStringLiteral("writingMasterFlat"),
                           QStringLiteral("Writing master Flat frame"), true,
                           s.left(100));
            nextLineIsPath = true;
            continue;
        }
        if (auto m = addMasterRe.match(s); m.hasMatch()) {
            dbg.logPattern(QStringLiteral("addMasterRe"),
                           addMasterRe.pattern(), true, s.left(100));
            return m.captured(1).trimmed();
        }
    }
    dbg.logDecision(QStringLiteral("Flat integration master: no path found in block"));
    return {};
}

// ---------------------------------------------------------------------------
QList<FlatBlock> CalibrationLogParser::parseFlatBlocks(const QString &filePath)
{
    auto &dbg = DebugLogger::instance();
    dbg.logSection(QStringLiteral("CalibrationLogParser::parseFlatBlocks"));
    dbg.logFileOpened(filePath);

    m_error.clear();
    QList<FlatBlock> result;

    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_error = QStringLiteral("Cannot open file: ") + filePath;
        dbg.logFileOpened(filePath, false);
        dbg.logError(m_error);
        return result;
    }

    QStringList all;
    QTextStream in(&f);
    while (!in.atEnd()) all << in.readLine();

    static const QRegularExpression calBeginRe(
        R"(\* Begin calibration of Flat frames)");
    static const QRegularExpression calEndRe(
        R"(\* End calibration of Flat frames)");
    static const QRegularExpression intBeginRe(
        R"(\* Begin integration of Flat frames)");
    static const QRegularExpression intEndRe(
        R"(\* End integration of Flat frames)");

    int n = all.size();
    int i = 0;
    int flatBlockIdx = 0;
    while (i < n) {
        const QString s = stripTs(all[i]);
        if (!s.contains(calBeginRe)) { ++i; continue; }

        dbg.logPattern(QStringLiteral("calBeginRe (Flat)"),
                       calBeginRe.pattern(), true, s.trimmed().left(100));

        int calEnd = -1;
        for (int j = i + 1; j < n; ++j) {
            if (stripTs(all[j]).contains(calEndRe)) { calEnd = j; break; }
        }
        if (calEnd < 0) {
            dbg.logWarning(QStringLiteral("Flat-cal block %1: no End marker found").arg(flatBlockIdx));
            break;
        }
        dbg.logDecision(QStringLiteral("Flat-cal block %1: lines %2–%3")
                            .arg(flatBlockIdx).arg(i).arg(calEnd));

        QStringList calLines;
        calLines.reserve(calEnd - i + 1);
        for (int k = i; k <= calEnd; ++k) calLines << all[k];

        FlatBlock blk;
        blk.masterBiasPath = parseFlatCalibrationBlock(calLines);
        dbg.logResult(QStringLiteral("flatBlock[%1].masterBias").arg(flatBlockIdx),
                      blk.masterBiasPath.isEmpty()
                          ? QStringLiteral("(none)") : blk.masterBiasPath);

        int intBegin = -1;
        for (int j = calEnd + 1; j < n; ++j) {
            const QString ss = stripTs(all[j]);
            if (ss.contains(calBeginRe)) break;
            if (ss.contains(intBeginRe)) { intBegin = j; break; }
        }

        if (intBegin >= 0) {
            dbg.logDecision(
                QStringLiteral("Flat-cal block %1: integration block starts at line %2")
                    .arg(flatBlockIdx).arg(intBegin));

            int intEnd = -1;
            for (int j = intBegin + 1; j < n; ++j) {
                if (stripTs(all[j]).contains(intEndRe)) { intEnd = j; break; }
            }
            if (intEnd >= 0) {
                QStringList intLines;
                intLines.reserve(intEnd - intBegin + 1);
                for (int k = intBegin; k <= intEnd; ++k) intLines << all[k];
                for (int k = intEnd + 1; k < n && k < intEnd + 10; ++k) {
                    const QString cs = stripTs(all[k]);
                    if (cs.contains(calBeginRe) || cs.contains(intBeginRe)) break;
                    intLines << all[k];
                }
                blk.masterFlatPath = parseFlatIntegrationBlock(intLines);
                i = intEnd + 1;
            } else {
                dbg.logWarning(
                    QStringLiteral("Flat-cal block %1: integration End marker not found")
                        .arg(flatBlockIdx));
                i = intBegin + 1;
            }
        } else {
            dbg.logWarning(
                QStringLiteral("Flat-cal block %1: no integration block follows")
                    .arg(flatBlockIdx));
            i = calEnd + 1;
        }

        dbg.logResult(QStringLiteral("flatBlock[%1].masterFlat").arg(flatBlockIdx),
                      blk.masterFlatPath.isEmpty()
                          ? QStringLiteral("(none)") : blk.masterFlatPath);

        if (!blk.masterFlatPath.isEmpty() || !blk.masterBiasPath.isEmpty())
            result << blk;
        else
            dbg.logDecision(
                QStringLiteral("Flat-cal block %1 discarded (both paths empty)")
                    .arg(flatBlockIdx));

        ++flatBlockIdx;
    }

    dbg.logResult(QStringLiteral("flatBlocksFound"), QString::number(result.size()));
    return result;
}
