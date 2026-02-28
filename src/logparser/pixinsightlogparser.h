#pragma once
#include "logparserbase.h"

class PixInsightLogParser : public ILogParser {
public:
    QList<AcquisitionGroup> parse(const QString &filePath) override;
    QString errorString() const override { return m_error; }
    bool canParse(const QString &filePath) const override;

private:
    QString m_error;
    bool parseBlock(const QStringList &lines, AcquisitionGroup &grp, int blockIdx = 0);
    QStringList extractXisfPaths(const QStringList &lines, int startLine,
                                 bool isFastInteg);
    static QString extractTarget(const QString &line);
};
