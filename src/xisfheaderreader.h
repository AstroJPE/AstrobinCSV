#pragma once
#include <QString>
#include <QDate>
#include <optional>

struct XisfFrameData {
    QDate   date;
    int     gain{-1};
    int     sensorTemp{0};
    bool    hasSensorTemp{false};
    double  ambTemp{0.0};
    bool    hasAmbTemp{false};
    int     binning{1};        // from XBINNING keyword; defaults to 1
    QString filter;            // FILTER keyword value, empty if absent
    QString object;            // OBJECT keyword value, empty if absent
};

class XisfHeaderReader {
public:
    static std::optional<XisfFrameData> read(const QString &path);
};
