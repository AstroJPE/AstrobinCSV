#pragma once
#include "models/integrationgroup.h"
#include <QString>
#include <QList>

class PixInsightLogParser {
public:
    // Returns one IntegrationGroup per non-LN-Reference Light integration
    // block found in the log file.
    QList<IntegrationGroup> parse(const QString &filePath);

    QString errorString() const { return m_error; }
    bool    canParse(const QString &filePath) const;

private:
    QString m_error;

    // Parses a single integration block and populates the group's frame list.
    // Returns false if no .xisf paths were found.
    bool parseBlock(const QStringList &lines,
                    IntegrationGroup  &grp,
                    int                blockIdx);

    static QStringList extractXisfPaths(const QStringList &lines,
                                        int startLine,
                                        bool isFastInteg);

    static QString extractTarget(const QString &line);
};
