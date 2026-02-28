#pragma once
#include "models/acquisitiongroup.h"
#include <QString>
#include <QList>

class ILogParser {
public:
    virtual ~ILogParser() = default;
    virtual QList<AcquisitionGroup> parse(const QString &filePath) = 0;
    virtual QString errorString() const = 0;
    virtual bool canParse(const QString &filePath) const = 0;
};
