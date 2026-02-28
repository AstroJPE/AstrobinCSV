#include "sirillogparser.h"
#include <QFile>
#include <QTextStream>

bool SirilLogParser::canParse(const QString &filePath) const
{
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
    return QTextStream(&f).readLine().contains("Siril", Qt::CaseInsensitive);
}

QList<AcquisitionGroup> SirilLogParser::parse(const QString &filePath)
{
    Q_UNUSED(filePath)
    m_error = QStringLiteral(
        "Siril log parsing is not yet implemented. "
        "Please use a PixInsight WBPP log for now.");
    return {};
}
