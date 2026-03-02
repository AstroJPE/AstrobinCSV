#pragma once
#include <QString>

// Calibration chain resolved for a single registered light frame.
// Paths are the original paths from the WBPP log; counts are read from
// the master .xisf file headers by FrameResolveWorker.
// A count of -1 means the master file was not found or not applicable.
struct FrameCalibration {
    QString masterDarkPath;
    QString masterFlatPath;
    QString masterBiasPath;
    int     darks{-1};
    int     flats{-1};
    int     bias{-1};
};
