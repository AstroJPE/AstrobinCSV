#pragma once
#include "acquisitionframe.h"
#include <QString>
#include <QList>

// One WBPP "* Begin integration of Light frames" block.
// Local Normalization reference blocks (LN_Reference_) are excluded by
// the parser and never produce an IntegrationGroup.
struct IntegrationGroup {
    QString                  sourceLogFile;
    int                      sessionIndex{-1}; // 0-based position in the log;
                                               // used as a stable unique key
                                               // in rebuildRows() so that
                                               // separate WBPP blocks for the
                                               // same target/filter/night are
                                               // never merged accidentally.
    double                   exposureSec{0};   // from log "Exposure :" line
    QString                  logTarget;        // from WBPP grouping keyword
    bool                     targetFromLog{false};
    QList<AcquisitionFrame>  frames;

    int frameCount() const { return frames.size(); }
};
