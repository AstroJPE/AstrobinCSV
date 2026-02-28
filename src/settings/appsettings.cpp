#include "appsettings.h"
#include <QSettings>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QCoreApplication>
#include <QStandardPaths>
#include <QDir>

AppSettings &AppSettings::instance()
{
    static AppSettings s;
    return s;
}

static QSettings qs()
{
#ifdef Q_OS_WIN
    // On Windows, store settings in an INI file in AppLocalDataLocation
    // (C:\Users\<user>\AppData\Local\AstrobinCSV\AstrobinCSV\) rather than
    // in the registry.
    const QString dataDir =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(dataDir);
    return QSettings(dataDir + QStringLiteral("/AstrobinCSV.ini"),
                     QSettings::IniFormat);
#else
    return QSettings(QStringLiteral("AstrobinCSV"),
                     QStringLiteral("AstrobinCSV"));
#endif
}

QList<Location> AppSettings::locations() const
{
    QSettings s = qs();
    QByteArray data = s.value(QStringLiteral("locations")).toByteArray();
    QList<Location> result;
    auto arr = QJsonDocument::fromJson(data).array();
    for (const auto &v : arr) {
        auto o = v.toObject();
        Location loc;
        loc.name       = o[QStringLiteral("name")].toString();
        loc.hasBortle  = o.contains(QStringLiteral("bortle"));
        loc.bortle     = loc.hasBortle ? o[QStringLiteral("bortle")].toInt() : -1;
        loc.hasMeanSqm = o.contains(QStringLiteral("meanSqm"));
        loc.meanSqm    = loc.hasMeanSqm
                             ? o[QStringLiteral("meanSqm")].toDouble() : -1;
        result << loc;
    }
    return result;
}

void AppSettings::setLocations(const QList<Location> &locs)
{
    QJsonArray arr;
    for (const auto &loc : locs) {
        QJsonObject o;
        o[QStringLiteral("name")] = loc.name;
        if (loc.hasBortle)  o[QStringLiteral("bortle")]  = loc.bortle;
        if (loc.hasMeanSqm) o[QStringLiteral("meanSqm")] = loc.meanSqm;
        arr.append(o);
    }
    QSettings s = qs();
    s.setValue(QStringLiteral("locations"),
               QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

QList<FilterMapping> AppSettings::filterMappings() const
{
    QSettings s = qs();
    QByteArray data = s.value(QStringLiteral("filterMappings")).toByteArray();
    QList<FilterMapping> result;
    auto arr = QJsonDocument::fromJson(data).array();
    for (const auto &v : arr) {
        auto o = v.toObject();
        FilterMapping fm;
        fm.localName    = o[QStringLiteral("localName")].toString();
        fm.astrobinId   = o[QStringLiteral("astrobinId")].toInt(-1);
        fm.astrobinName = o[QStringLiteral("astrobinName")].toString();
        result << fm;
    }
    return result;
}

void AppSettings::setFilterMappings(const QList<FilterMapping> &mappings)
{
    QJsonArray arr;
    for (const auto &fm : mappings) {
        QJsonObject o;
        o[QStringLiteral("localName")]    = fm.localName;
        o[QStringLiteral("astrobinId")]   = fm.astrobinId;
        o[QStringLiteral("astrobinName")] = fm.astrobinName;
        arr.append(o);
    }
    QSettings s = qs();
    s.setValue(QStringLiteral("filterMappings"),
               QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

int AppSettings::astrobinFilterId(const QString &localName) const
{
    for (const auto &fm : filterMappings())
        if (fm.localName.compare(localName, Qt::CaseInsensitive) == 0)
            return fm.astrobinId;
    return -1;
}

QList<AstrobinFilter> AppSettings::cachedAstrobinFilters() const
{
    QSettings s = qs();
    QByteArray data = s.value(QStringLiteral("astrobinFilters")).toByteArray();
    QList<AstrobinFilter> result;
    auto arr = QJsonDocument::fromJson(data).array();
    for (const auto &v : arr) {
        auto o = v.toObject();
        AstrobinFilter f;
        f.id        = o[QStringLiteral("id")].toInt(-1);
        f.brandName = o[QStringLiteral("brandName")].toString();
        f.name      = o[QStringLiteral("name")].toString();
        result << f;
    }
    return result;
}

void AppSettings::setCachedAstrobinFilters(const QList<AstrobinFilter> &filters)
{
    QJsonArray arr;
    for (const auto &f : filters) {
        QJsonObject o;
        o[QStringLiteral("id")]        = f.id;
        o[QStringLiteral("brandName")] = f.brandName;
        o[QStringLiteral("name")]      = f.name;
        arr.append(o);
    }
    QSettings s = qs();
    s.setValue(QStringLiteral("astrobinFilters"),
               QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

QString AppSettings::astrobinTargetName(const QString &logTarget) const
{
    for (const auto &tg : targetGroups())
        for (const auto &member : tg.memberTargets)
            if (member.compare(logTarget, Qt::CaseInsensitive) == 0)
                return tg.astrobinName;
    return logTarget;
}

QList<TargetGroup> AppSettings::targetGroups() const
{
    QSettings s = qs();
    QByteArray data = s.value(QStringLiteral("targetGroups")).toByteArray();
    QList<TargetGroup> result;
    auto arr = QJsonDocument::fromJson(data).array();
    for (const auto &v : arr) {
        auto o = v.toObject();
        TargetGroup tg;
        tg.astrobinName = o[QStringLiteral("astrobinName")].toString();
        for (const auto &m : o[QStringLiteral("members")].toArray())
            tg.memberTargets << m.toString();
        result << tg;
    }
    return result;
}

void AppSettings::setTargetGroups(const QList<TargetGroup> &groups)
{
    QJsonArray arr;
    for (const auto &tg : groups) {
        QJsonObject o;
        o[QStringLiteral("astrobinName")] = tg.astrobinName;
        QJsonArray members;
        for (const auto &m : tg.memberTargets) members.append(m);
        o[QStringLiteral("members")] = members;
        arr.append(o);
    }
    QSettings s = qs();
    s.setValue(QStringLiteral("targetGroups"),
               QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

QStringList AppSettings::targetKeywords() const
{
    QSettings s = qs();

    // Return empty list if the key has never been written, making the
    // default behaviour to use the OBJECT FITS/XISF header directly.
    if (!s.contains(QStringLiteral("targetKeywords")))
        return {};

    QByteArray data = s.value(QStringLiteral("targetKeywords")).toByteArray();
    QStringList result;
    for (const auto &v : QJsonDocument::fromJson(data).array())
        result << v.toString();
    return result;
}

void AppSettings::setTargetKeywords(const QStringList &keywords)
{
    QJsonArray arr;
    for (const auto &kw : keywords) arr.append(kw);
    QSettings s = qs();
    s.setValue(QStringLiteral("targetKeywords"),
               QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

QSet<int> AppSettings::hiddenColumns() const
{
    QSettings s = qs();
    QByteArray data = s.value(QStringLiteral("hiddenColumns")).toByteArray();
    QSet<int> result;
    auto arr = QJsonDocument::fromJson(data).array();
    for (const auto &v : arr)
        result.insert(v.toInt());
    return result;
}

void AppSettings::setHiddenColumns(const QSet<int> &cols)
{
    QJsonArray arr;
    for (int c : cols) arr.append(c);
    QSettings s = qs();
    s.setValue(QStringLiteral("hiddenColumns"),
               QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

QString AppSettings::theme() const
{
    return qs().value(QStringLiteral("theme"),
                      QStringLiteral("light")).toString();
}
void AppSettings::setTheme(const QString &t)
{
    QSettings s = qs(); s.setValue(QStringLiteral("theme"), t);
}

int AppSettings::groupingStrategy() const
{
    return qs().value(QStringLiteral("groupingStrategy"), 1).toInt();
}
void AppSettings::setGroupingStrategy(int strategy)
{
    QSettings s = qs();
    s.setValue(QStringLiteral("groupingStrategy"), strategy);
}

QString AppSettings::lastOpenDirectory() const
{
    return qs().value(QStringLiteral("lastOpenDir")).toString();
}
void AppSettings::setLastOpenDirectory(const QString &d)
{
    QSettings s = qs(); s.setValue(QStringLiteral("lastOpenDir"), d);
}

QString AppSettings::lastExportDirectory() const
{
    return qs().value(QStringLiteral("lastExportDir")).toString();
}
void AppSettings::setLastExportDirectory(const QString &d)
{
    QSettings s = qs(); s.setValue(QStringLiteral("lastExportDir"), d);
}

QByteArray AppSettings::windowGeometry() const
{
    return qs().value(QStringLiteral("windowGeometry")).toByteArray();
}
void AppSettings::setWindowGeometry(const QByteArray &g)
{
    QSettings s = qs(); s.setValue(QStringLiteral("windowGeometry"), g);
}

QByteArray AppSettings::splitterState() const
{
    return qs().value(QStringLiteral("splitterState")).toByteArray();
}
void AppSettings::setSplitterState(const QByteArray &s)
{
    QSettings st = qs(); st.setValue(QStringLiteral("splitterState"), s);
}

int AppSettings::fontSize() const
{
    // -1 means "not set" — caller should use the system default.
    return qs().value(QStringLiteral("fontSize"), -1).toInt();
}
void AppSettings::setFontSize(int pt)
{
    QSettings s = qs(); s.setValue(QStringLiteral("fontSize"), pt);
}
