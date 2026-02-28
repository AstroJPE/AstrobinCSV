#pragma once
#include <QString>
#include <QList>
#include <QSet>
#include "models/targetgroup.h"

struct Location {
    QString name;
    int     bortle{-1};
    bool    hasBortle{false};
    double  meanSqm{-1};
    bool    hasMeanSqm{false};
};

struct FilterMapping {
    QString localName;
    int     astrobinId{-1};
    QString astrobinName;
};

struct AstrobinFilter {
    int     id{-1};
    QString brandName;
    QString name;
};

class AppSettings {
public:
    static AppSettings &instance();

    QList<Location>      locations() const;
    void setLocations(const QList<Location> &locs);

    QList<FilterMapping> filterMappings() const;
    void setFilterMappings(const QList<FilterMapping> &mappings);
    int  astrobinFilterId(const QString &localName) const;

    QList<AstrobinFilter> cachedAstrobinFilters() const;
    void setCachedAstrobinFilters(const QList<AstrobinFilter> &filters);

    QString astrobinTargetName(const QString &logTarget) const;

    QList<TargetGroup> targetGroups() const;
    void setTargetGroups(const QList<TargetGroup> &groups);

    // FITS keyword names used to extract a target name from the WBPP log
    // (fallback only – the OBJECT tag in the .xisf header takes precedence).
    QStringList targetKeywords() const;
    void        setTargetKeywords(const QStringList &keywords);

    QSet<int> hiddenColumns() const;
    void      setHiddenColumns(const QSet<int> &cols);

    QString theme() const;
    void setTheme(const QString &t);

    int  groupingStrategy() const;
    void setGroupingStrategy(int s);

    QString lastOpenDirectory() const;
    void setLastOpenDirectory(const QString &d);

    QString lastExportDirectory() const;
    void setLastExportDirectory(const QString &d);

    QByteArray windowGeometry() const;
    void       setWindowGeometry(const QByteArray &g);

    QByteArray splitterState() const;
    void       setSplitterState(const QByteArray &s);

    int  fontSize() const;
    void setFontSize(int pt);

private:
    AppSettings() = default;
};
