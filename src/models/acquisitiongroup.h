#pragma once
#include <QString>
#include <QStringList>
#include <QDate>

struct AcquisitionGroup {
    QString     filter;
    QString     filterFromXisf;   // set once from first resolved frame
    double      exposureSec{0};
    int         binning{1};
    QString     target;
    QString     targetFromXisf;   // set once from first resolved OBJECT keyword
    bool        targetFromLog{false}; // true when target was matched from a
                                      // user-defined Target Keyword in the log;
                                      // XisfResolveWorker must not overwrite
                                      // target with OBJECT when this is true.
    QString     sourceLogFile;
    QStringList xisfPaths;

    QList<QDate> frameDates;
    QList<int>   frameGains;
    QList<int>   frameSensorTemps;
    QList<bool>  frameHasSensorTemp;
    QList<double> frameAmbTemps;
    QList<bool>   frameHasAmbTemp;
    QList<bool>  frameResolved;

    // Calibration frame counts, populated after matching a calibration block.
    // -1 means "not found / not applicable".
    int darks{-1};
    int flats{-1};
    int bias{-1};

    int frameCount() const { return xisfPaths.size(); }
};
