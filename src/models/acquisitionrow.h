#pragma once
#include <QDate>
#include <QString>
#include <QVariant>

struct AcquisitionRow {
    int     number{0};
    double  duration{0};

    QDate   date;
    bool    hasDate{false};

    int     filterAstrobinId{-1};
    bool    hasFilter{false};

    int     binning{-1};
    bool    hasBinning{false};

    int     gain{-1};
    bool    hasGain{false};

    int     sensorCooling{0};
    bool    hasSensorCooling{false};

    int     iso{-1};
    bool    hasIso{false};

    double  fNumber{-1};
    bool    hasFNumber{false};

    int     darks{-1};
    bool    hasDarks{false};

    int     flats{-1};
    bool    hasFlats{false};

    int     flatDarks{-1};
    bool    hasFlatDarks{false};

    int     bias{-1};
    bool    hasBias{false};

    int     bortle{-1};
    bool    hasBortle{false};

    double  meanSqm{-1};
    bool    hasMeanSqm{false};

    double  meanFwhm{-1};
    bool    hasMeanFwhm{false};

    double  temperature{-1e9};
    bool    hasTemperature{false};

    QString groupLabel;
};
