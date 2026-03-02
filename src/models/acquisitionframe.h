#pragma once
#include "framecalibration.h"
#include <QString>
#include <QDate>

// One registered .xisf light frame with all per-frame metadata and its
// resolved calibration chain.
struct AcquisitionFrame {
    // ── From the WBPP log ─────────────────────────────────────────────────
    QString          registeredPath;    // full path to the registered .xisf
    double           exposureSec{0};    // from log "Exposure :" line

    // Target from WBPP grouping keyword (if a Target Keyword matched).
    // If targetFromLog is false the target comes from the OBJECT header.
    QString          logTarget;
    bool             targetFromLog{false};

    // ── From the XISF header (set by FrameResolveWorker) ─────────────────
    bool             resolved{false};   // true once the header has been read

    QDate            date;              // DATE-LOC minus 12 h (observing night)
    int              gain{-1};          // GAIN
    int              sensorTemp{0};     // SET-TEMP
    bool             hasSensorTemp{false};
    double           ambTemp{0.0};      // AMBTEMP
    bool             hasAmbTemp{false};
    QString          filter;            // FILTER keyword value
    QString          object;            // OBJECT keyword value
    int              binning{1};        // XBINNING

    // ── Calibration chain (set by FrameResolveWorker) ─────────────────────
    FrameCalibration calibration;
};
