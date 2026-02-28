#pragma once
#include <QString>
#include <QStringList>
#include <QList>

// One parsed "Begin/End calibration of Light frames" block.
struct CalibrationBlock {
    QString      masterDarkPath;   // empty if disabled or absent
    QString      masterFlatPath;   // empty if disabled or absent
    QString      masterBiasPath;   // empty if disabled or absent
    QStringList  calibratedPaths;  // output _c.xisf paths from "Calibration frame N: ... ---> ..."
};

// One parsed flat calibration+integration pair.
// Links the master flat output back to the master bias used to calibrate the flats.
struct FlatBlock {
    QString masterFlatPath;   // output of "* Writing master Flat frame:" / "Add the master file:"
    QString masterBiasPath;   // from "Master bias:" inside the flat-calibration sub-block
};

class CalibrationLogParser {
public:
    // Parses all Light calibration blocks in the given log file.
    // Returns an empty list (and sets errorString) on file-open failure.
    QList<CalibrationBlock> parse(const QString &filePath);

    // Parses all Flat calibration+integration block pairs.
    QList<FlatBlock> parseFlatBlocks(const QString &filePath);

    QString errorString() const { return m_error; }

private:
    QString m_error;

    // Parses a single Light calibration block (lines from Begin to End inclusive).
    static CalibrationBlock parseBlock(const QStringList &lines);

    // Parses a single Flat calibration block; returns the master bias path.
    static QString parseFlatCalibrationBlock(const QStringList &lines);

    // Extracts the master flat output path from a Flat integration block.
    static QString parseFlatIntegrationBlock(const QStringList &lines);
};
