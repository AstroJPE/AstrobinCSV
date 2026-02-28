#pragma once
#include "logparserbase.h"

class SirilLogParser : public ILogParser {
public:
    QList<AcquisitionGroup> parse(const QString &filePath) override;
    QString errorString() const override { return m_error; }
    bool canParse(const QString &filePath) const override;
private:
    QString m_error;
};
