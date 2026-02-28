#include "xisfheaderreader.h"
#include "debuglogger.h"
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QXmlStreamReader>
#include <cmath>

std::optional<XisfFrameData> XisfHeaderReader::read(const QString &path)
{
    auto &dbg = DebugLogger::instance();
    const bool logging = dbg.isSessionActive();

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (logging)
            dbg.logWarning(QStringLiteral("XISF: cannot open '%1'")
                               .arg(QFileInfo(path).fileName()));
        return std::nullopt;
    }

    const QByteArray magic = f.read(8);
    if (!magic.startsWith("XISF0100")) {
        if (logging)
            dbg.logWarning(QStringLiteral("XISF: bad magic bytes in '%1'")
                               .arg(QFileInfo(path).fileName()));
        return std::nullopt;
    }

    const QByteArray lenBytes = f.read(4);
    if (lenBytes.size() < 4)
        return std::nullopt;

    const quint32 xmlLen =
        static_cast<quint8>(lenBytes[0])         |
        (static_cast<quint8>(lenBytes[1]) <<  8) |
        (static_cast<quint8>(lenBytes[2]) << 16) |
        (static_cast<quint8>(lenBytes[3]) << 24);

    f.read(4);  // skip reserved bytes

    if (xmlLen == 0 || xmlLen > 10 * 1024 * 1024)
        return std::nullopt;

    const QByteArray xmlData = f.read(xmlLen);
    if (static_cast<quint32>(xmlData.size()) < xmlLen)
        return std::nullopt;

    // Keywords we look for — logged once per file so the reader is
    // self-documenting in the debug output.
    static const QString kDateLoc   = QStringLiteral("DATE-LOC");
    static const QString kGain      = QStringLiteral("GAIN");
    static const QString kSetTemp   = QStringLiteral("SET-TEMP");
    static const QString kFilter    = QStringLiteral("FILTER");
    static const QString kObject    = QStringLiteral("OBJECT");
    static const QString kAmbTemp   = QStringLiteral("AMBTEMP");

    if (logging)
        dbg.logDecision(
            QStringLiteral("XISF '%1': scanning for keywords [%2]")
                .arg(QFileInfo(path).fileName(),
                     QStringList{kDateLoc, kGain, kSetTemp,
                                 kFilter, kObject, kAmbTemp}.join(", ")));

    QString dateLoc, gainRaw, setTempRaw, filterRaw, objectRaw, ambTempRaw;

    QXmlStreamReader xml(xmlData);
    while (!xml.atEnd() && !xml.hasError()) {
        if (xml.readNext() == QXmlStreamReader::StartElement
            && xml.name() == QLatin1String("FITSKeyword")) {

            const QString name = xml.attributes().value(
                QLatin1String("name")).toString().trimmed().toUpper();
            const QString value = xml.attributes().value(
                QLatin1String("value")).toString().trimmed();

            if      (name == kDateLoc && dateLoc.isEmpty())
                dateLoc = value;
            else if (name == kGain    && gainRaw.isEmpty())
                gainRaw = value;
            else if (name == kSetTemp && setTempRaw.isEmpty())
                setTempRaw = value;
            else if (name == kFilter  && filterRaw.isEmpty())
                filterRaw = value;
            else if (name == kObject  && objectRaw.isEmpty())
                objectRaw = value;
            else if (name == kAmbTemp && ambTempRaw.isEmpty())
                ambTempRaw = value;

            if (!dateLoc.isEmpty() && !gainRaw.isEmpty() && !setTempRaw.isEmpty()
                && !filterRaw.isEmpty() && !objectRaw.isEmpty()
                && !ambTempRaw.isEmpty())
                break;
        }
    }

    // Log what was found / not found for each keyword.
    if (logging) {
        auto report = [&](const QString &kw, const QString &raw) {
            if (raw.isEmpty())
                dbg.logPattern(kw, QStringLiteral("FITSKeyword name=\"%1\"").arg(kw),
                               false);
            else
                dbg.logPattern(kw, QStringLiteral("FITSKeyword name=\"%1\"").arg(kw),
                               true, raw.left(80));
        };
        report(kDateLoc, dateLoc);
        report(kGain,    gainRaw);
        report(kSetTemp, setTempRaw);
        report(kFilter,  filterRaw);
        report(kObject,  objectRaw);
        report(kAmbTemp, ambTempRaw);
    }

    if (dateLoc.isEmpty()) {
        if (logging)
            dbg.logWarning(
                QStringLiteral("XISF '%1': DATE-LOC absent — frame skipped")
                    .arg(QFileInfo(path).fileName()));
        return std::nullopt;
    }

    XisfFrameData result;

    QString ds = dateLoc;
    if (ds.startsWith('\'')) ds = ds.mid(1);
    if (ds.endsWith('\''))   ds.chop(1);
    ds = ds.trimmed();

    QDateTime dt = QDateTime::fromString(ds, Qt::ISODateWithMs);
    if (!dt.isValid()) dt = QDateTime::fromString(ds, Qt::ISODate);
    if (dt.isValid()) {
        dt = dt.addSecs(-12 * 3600);
        result.date = dt.date();
        if (logging)
            dbg.logResult(QStringLiteral("XISF '%1' date")
                              .arg(QFileInfo(path).fileName()),
                          result.date.toString(Qt::ISODate));
    } else {
        if (logging)
            dbg.logWarning(
                QStringLiteral("XISF '%1': could not parse DATE-LOC value '%2'")
                    .arg(QFileInfo(path).fileName(), ds));
    }

    if (!gainRaw.isEmpty()) {
        QString gs = gainRaw;
        if (gs.startsWith('\'')) gs = gs.mid(1);
        if (gs.endsWith('\''))   gs.chop(1);
        bool ok = false;
        double v = gs.trimmed().toDouble(&ok);
        if (ok) {
            result.gain = static_cast<int>(std::round(v));
            if (logging)
                dbg.logResult(QStringLiteral("XISF '%1' gain")
                                  .arg(QFileInfo(path).fileName()),
                              QString::number(result.gain));
        }
    }

    if (!setTempRaw.isEmpty()) {
        QString s = setTempRaw;
        if (s.startsWith('\'')) s = s.mid(1);
        if (s.endsWith('\''))   s.chop(1);
        bool ok = false;
        double v = s.trimmed().toDouble(&ok);
        if (ok) {
            result.sensorTemp    = static_cast<int>(std::round(v));
            result.hasSensorTemp = true;
            if (logging)
                dbg.logResult(QStringLiteral("XISF '%1' SET-TEMP")
                                  .arg(QFileInfo(path).fileName()),
                              QString::number(result.sensorTemp));
        }
    }

    auto stripQuotes = [](const QString &s) -> QString {
        QString t = s.trimmed();
        if (t.startsWith('\'')) t = t.mid(1);
        if (t.endsWith('\''))   t.chop(1);
        return t.trimmed();
    };

    if (!filterRaw.isEmpty()) {
        result.filter = stripQuotes(filterRaw);
        if (logging)
            dbg.logResult(QStringLiteral("XISF '%1' FILTER")
                              .arg(QFileInfo(path).fileName()),
                          result.filter);
    }

    if (!objectRaw.isEmpty()) {
        result.object = stripQuotes(objectRaw);
        if (logging)
            dbg.logResult(QStringLiteral("XISF '%1' OBJECT")
                              .arg(QFileInfo(path).fileName()),
                          result.object);
    }

    if (!ambTempRaw.isEmpty()) {
        QString s = stripQuotes(ambTempRaw);
        bool ok = false;
        double v = s.toDouble(&ok);
        if (ok) {
            result.ambTemp    = v;
            result.hasAmbTemp = true;
            if (logging)
                dbg.logResult(QStringLiteral("XISF '%1' AMBTEMP")
                                  .arg(QFileInfo(path).fileName()),
                              QString::number(result.ambTemp, 'f', 2));
        }
    }

    return result;
}
