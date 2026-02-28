#include "csvtablemodel.h"
#include <QColor>
#include <QSet>
#include <cmath>

const char *CsvTableModel::kColumns[CsvTableModel::ColCount] = {
    "Group",
    "date", "filter", "filterName [*]", "number", "duration",
    "binning", "gain", "sensorCooling",
    "iso", "fNumber",
    "darks", "flats", "flatDarks", "bias",
    "bortle", "meanSqm", "meanFwhm", "temperature"
};

CsvTableModel::CsvTableModel(QObject *parent)
    : QAbstractTableModel(parent) {}

void CsvTableModel::setRows(const QList<AcquisitionRow> &rows)
{
    beginResetModel();
    m_rows = rows;
    endResetModel();
}

int CsvTableModel::rowCount(const QModelIndex &) const { return m_rows.size(); }
int CsvTableModel::columnCount(const QModelIndex &) const { return ColCount; }

QVariant CsvTableModel::cellDisplay(const AcquisitionRow &r, int col) const
{
    switch (col) {
    case ColGroup:    return r.groupLabel;
    case ColDate:     return r.hasDate ? r.date.toString(Qt::ISODate) : QVariant{};
    case ColFilter:   return (r.hasFilter && r.filterAstrobinId >= 0)
                                 ? QVariant(r.filterAstrobinId) : QVariant{};
    case ColFilterName: {
        if (!r.hasFilter) return {};
        if (r.filterAstrobinId >= 0) {
            const auto &mappings = AppSettings::instance().filterMappings();
            for (const auto &fm : mappings) {
                if (fm.astrobinId == r.filterAstrobinId) {
                    return fm.astrobinName.isEmpty()
                               ? fm.localName : fm.astrobinName;
                }
            }
        }
        return QStringLiteral("(unmapped)");
    }
    case ColNumber:       return r.number > 0       ? QVariant(r.number)                               : QVariant{};
    case ColDuration:     return r.duration > 0     ? QVariant(r.duration)                             : QVariant{};
    case ColBinning:      return r.hasBinning       ? QVariant(r.binning)                              : QVariant{};
    case ColGain:         return r.hasGain          ? QVariant(r.gain)                                 : QVariant{};
    case ColSensorCooling:return r.hasSensorCooling ? QVariant(r.sensorCooling)                        : QVariant{};
    case ColIso:          return r.hasIso           ? QVariant(r.iso)                                  : QVariant{};
    case ColFNumber:      return r.hasFNumber       ? QVariant(r.fNumber)                              : QVariant{};
    case ColDarks:        return r.hasDarks         ? QVariant(r.darks)                                : QVariant{};
    case ColFlats:        return r.hasFlats         ? QVariant(r.flats)                                : QVariant{};
    case ColFlatDarks:    return r.hasFlatDarks     ? QVariant(r.flatDarks)                            : QVariant{};
    case ColBias:         return r.hasBias          ? QVariant(r.bias)                                 : QVariant{};
    case ColBortle:       return r.hasBortle        ? QVariant(r.bortle)                               : QVariant{};
    case ColMeanSqm:      return r.hasMeanSqm       ? QVariant(r.meanSqm)                              : QVariant{};
    case ColMeanFwhm:     return r.hasMeanFwhm      ? QVariant(r.meanFwhm)                             : QVariant{};
    case ColTemperature:  return r.hasTemperature   ? QVariant(QString::number(r.temperature, 'f', 2)) : QVariant{};
    default: return {};
    }
}

QVariant CsvTableModel::data(const QModelIndex &idx, int role) const
{
    if (!idx.isValid() || idx.row() >= m_rows.size()) return {};
    const AcquisitionRow &r = m_rows[idx.row()];
    int col = idx.column();

    if (role == Qt::DisplayRole || role == Qt::EditRole)
        return cellDisplay(r, col);

    if (role == Qt::BackgroundRole) {
        bool required = (col == ColNumber || col == ColDuration);
        if (required) {
            QVariant v = cellDisplay(r, col);
            if (!v.isValid() || v.isNull())
                return QColor(0xff, 0xcc, 0xcc);
        }
        if ((col == ColFilter || col == ColFilterName)
                && r.hasFilter && r.filterAstrobinId < 0)
            return QColor(0xff, 0xe0, 0x80);
    }

    if (role == Qt::ToolTipRole && col == ColFilter
            && r.hasFilter && r.filterAstrobinId < 0)
        return QStringLiteral("Filter not mapped to an Astrobin ID. "
                               "Use Manage Filters to set up a mapping.");
    return {};
}

bool CsvTableModel::setData(const QModelIndex &idx, const QVariant &val, int role)
{
    if (role != Qt::EditRole || !idx.isValid() || idx.row() >= m_rows.size())
        return false;
    if (setCellValue(m_rows[idx.row()], idx.column(), val)) {
        emit dataChanged(idx, idx, {Qt::DisplayRole, Qt::EditRole});
        return true;
    }
    return false;
}

bool CsvTableModel::setCellValue(AcquisitionRow &r, int col, const QVariant &val)
{
    QString s = val.toString().trimmed();
    bool ok = false;
    switch (col) {
    case ColDate:
        r.date    = QDate::fromString(s, Qt::ISODate);
        r.hasDate = r.date.isValid();
        return true;
    case ColFilter: {
        int id = s.toInt(&ok);
        r.filterAstrobinId = ok ? id : -1;
        r.hasFilter = ok;
        return true;
    }
    case ColNumber:    r.number   = s.toInt(&ok);    return ok;
    case ColDuration:  r.duration = s.toDouble(&ok); return ok;
    case ColBinning:   r.binning  = s.toInt(&ok);    r.hasBinning  = ok; return true;
    case ColGain:      r.gain     = s.toInt(&ok);    r.hasGain     = ok; return true;
    case ColSensorCooling: r.sensorCooling = s.toInt(&ok); r.hasSensorCooling = ok; return true;
    case ColIso:       r.iso      = s.toInt(&ok);    r.hasIso      = ok; return true;
    case ColFNumber:   r.fNumber  = s.toDouble(&ok); r.hasFNumber  = ok; return true;
    case ColDarks:     r.darks    = s.toInt(&ok);    r.hasDarks    = ok; return true;
    case ColFlats:     r.flats    = s.toInt(&ok);    r.hasFlats    = ok; return true;
    case ColFlatDarks: r.flatDarks= s.toInt(&ok);    r.hasFlatDarks= ok; return true;
    case ColBias:      r.bias     = s.toInt(&ok);    r.hasBias     = ok; return true;
    case ColBortle:    r.bortle   = s.toInt(&ok);    r.hasBortle   = ok; return true;
    case ColMeanSqm:   r.meanSqm  = s.toDouble(&ok); r.hasMeanSqm  = ok; return true;
    case ColMeanFwhm:  r.meanFwhm = s.toDouble(&ok); r.hasMeanFwhm = ok; return true;
    case ColTemperature: r.temperature = s.toDouble(&ok); r.hasTemperature = ok; return true;
    default: return false;
    }
}

QVariant CsvTableModel::headerData(int section, Qt::Orientation o, int role) const
{
    if (role != Qt::DisplayRole) return {};
    if (o == Qt::Horizontal && section < ColCount)
        return QString::fromLatin1(kColumns[section]);
    if (o == Qt::Vertical)
        return section + 1;
    return {};
}

Qt::ItemFlags CsvTableModel::flags(const QModelIndex &idx) const
{
    if (!idx.isValid()) return Qt::NoItemFlags;
    Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (idx.column() != ColGroup && idx.column() != ColFilterName)
        f |= Qt::ItemIsEditable;
    return f;
}

QStringList CsvTableModel::groupLabels() const
{
    QStringList labels;
    QSet<QString> seen;
    for (const auto &r : m_rows) {
        if (!seen.contains(r.groupLabel)) {
            seen.insert(r.groupLabel);
            labels << r.groupLabel;
        }
    }
    return labels;
}

QStringList CsvTableModel::targetNames() const
{
    QStringList names;
    QSet<QString> seen;
    for (const auto &r : m_rows) {
        int sep = r.groupLabel.indexOf(QStringLiteral(" / "));
        QString target = (sep >= 0) ? r.groupLabel.left(sep) : r.groupLabel;
        if (!seen.contains(target)) {
            seen.insert(target);
            names << target;
        }
    }
    return names;
}

QString CsvTableModel::toCsv(const QString &targetFilter,
                             const QSet<int> &hiddenCols) const
{
    auto rowMatches = [&](const AcquisitionRow &r) -> bool {
        if (targetFilter.isEmpty()) return true;
        return r.groupLabel == targetFilter
               || r.groupLabel.startsWith(targetFilter + QStringLiteral(" / "));
    };

    QList<int> activeCols;
    for (int c = ColDate; c < ColCount; ++c) {
        if (c == ColFilterName) continue;
        if (hiddenCols.contains(c)) continue;          // skip hidden columns
        for (const auto &r : m_rows) {
            if (rowMatches(r) && cellDisplay(r, c).isValid()) {
                activeCols << c;
                break;
            }
        }
    }

    QStringList lines;
    QStringList hdr;
    for (int c : activeCols) hdr << QString::fromLatin1(kColumns[c]);
    lines << hdr.join(',');

    for (const auto &r : m_rows) {
        if (!rowMatches(r)) continue;
        QStringList fields;
        for (int c : activeCols) {
            QVariant v = cellDisplay(r, c);
            fields << (v.isValid() ? v.toString() : QString{});
        }
        lines << fields.join(',');
    }

    return lines.join('\n') + '\n';
}

QString CsvTableModel::integrationSummary() const
{
    struct FilterStats {
        double totalSec{0};
        int    totalFrames{0};
    };

    QList<QString>                            targetOrder;
    QMap<QString, QList<QString>>             filterOrder;
    QMap<QString, QMap<QString, FilterStats>> stats;

    for (const AcquisitionRow &r : m_rows) {
        const QString &lbl = r.groupLabel;
        int sep1 = lbl.indexOf(QStringLiteral(" / "));
        if (sep1 < 0) continue;
        QString target = lbl.left(sep1);

        int sep2 = lbl.indexOf(QStringLiteral(" / "), sep1 + 3);
        QString filter = (sep2 >= 0)
            ? lbl.mid(sep1 + 3, sep2 - sep1 - 3)
            : lbl.mid(sep1 + 3);

        if (!stats.contains(target))
            targetOrder << target;
        if (!stats[target].contains(filter))
            filterOrder[target] << filter;

        FilterStats &fs = stats[target][filter];
        fs.totalFrames += r.number;
        fs.totalSec    += r.number * r.duration;
    }

    if (targetOrder.isEmpty())
        return {};

    auto fmtTime = [](double sec) -> QString {
        int s = static_cast<int>(std::round(sec));
        int h = s / 3600;
        int m = (s % 3600) / 60;
        int rem = s % 60;
        if (h > 0)
            return QString::asprintf("%dh %02dm", h, m);
        if (m > 0)
            return QString::asprintf("%dm %02ds", m, rem);
        return QString::asprintf("%ds", rem);
    };

    QStringList lines;
    for (const QString &target : targetOrder) {
        lines << QStringLiteral("=== %1 ===").arg(target);

        const auto &fmap  = stats[target];
        const auto &flist = filterOrder[target];

        int longestFilter = 0;
        for (const QString &f : flist)
            longestFilter = qMax(longestFilter, f.length());

        double targetTotal = 0;
        for (const QString &filter : flist) {
            const FilterStats &fs = fmap[filter];
            targetTotal += fs.totalSec;
            QString padded = filter.leftJustified(longestFilter, QLatin1Char(' '));
            lines << QStringLiteral("  %1 : %2  (%3 frames)")
                         .arg(padded)
                         .arg(fmtTime(fs.totalSec))
                         .arg(fs.totalFrames);
        }

        QString pad = QString(longestFilter + 2, QLatin1Char(' '));
        lines << QStringLiteral("%1 Total: %2")
                     .arg(pad)
                     .arg(fmtTime(targetTotal));
        lines << QString{};  // blank line between targets
    }

    return lines.join(QLatin1Char('\n'));
}

QMap<QString, CsvTableModel::UserEdits> CsvTableModel::snapshotEdits() const
{
    // Snapshot every row that differs from its "unedited" default.
    // We treat a field as "edited" if it is set (has* == true) for fields
    // that are not populated automatically from .xisf headers or location
    // settings.  For simplicity we snapshot ALL set fields — applyEdits()
    // will only overwrite fields that the snapshot says were edited.
    QMap<QString, UserEdits> snap;
    for (const AcquisitionRow &r : m_rows) {
        UserEdits e;
        bool any = false;

        auto take = [&](bool has, auto &dst, const auto &src) {
            if (has) { dst = src; any = true; }
        };

        e.hasDate          = r.hasDate;          take(r.hasDate,          e.date,             r.date);
        e.hasGain          = r.hasGain;          take(r.hasGain,          e.gain,             r.gain);
        e.hasSensorCooling = r.hasSensorCooling; take(r.hasSensorCooling, e.sensorCooling,    r.sensorCooling);
        e.hasTemperature   = r.hasTemperature;   take(r.hasTemperature,   e.temperature,      r.temperature);
        e.hasBortle        = r.hasBortle;        take(r.hasBortle,        e.bortle,           r.bortle);
        e.hasMeanSqm       = r.hasMeanSqm;       take(r.hasMeanSqm,       e.meanSqm,          r.meanSqm);
        e.hasMeanFwhm      = r.hasMeanFwhm;      take(r.hasMeanFwhm,      e.meanFwhm,         r.meanFwhm);
        e.hasDarks         = r.hasDarks;         take(r.hasDarks,         e.darks,            r.darks);
        e.hasFlats         = r.hasFlats;         take(r.hasFlats,         e.flats,            r.flats);
        e.hasFlatDarks     = r.hasFlatDarks;     take(r.hasFlatDarks,     e.flatDarks,        r.flatDarks);
        e.hasBias          = r.hasBias;          take(r.hasBias,          e.bias,             r.bias);
        e.hasIso           = r.hasIso;           take(r.hasIso,           e.iso,              r.iso);
        e.hasFNumber       = r.hasFNumber;       take(r.hasFNumber,       e.fNumber,          r.fNumber);
        e.hasBinning       = r.hasBinning;       take(r.hasBinning,       e.binning,          r.binning);
//        e.hasFilter        = r.hasFilter;        take(r.hasFilter,        e.filterAstrobinId, r.filterAstrobinId);
        // NOTE: filter ID is intentionally excluded from the snapshot — it is
        // derived from AppSettings::filterMappings() at rebuildRows() time and
        // must not be frozen here, otherwise a newly added mapping would be
        // overwritten by the stale -1 value on the next rebuildRows() call.
        // Users who manually override the filter ID in the table will lose that
        // edit on the next rebuild, which is acceptable given that the normal
        // workflow is to set mappings via Manage Filters.
        e.hasNumber        = (r.number > 0);     take(r.number > 0,       e.number,           r.number);
        e.hasDuration      = (r.duration > 0);   take(r.duration > 0,     e.duration,         r.duration);

        if (any)
            snap.insert(r.groupLabel, e);
    }
    return snap;
}

void CsvTableModel::applyEdits(const QMap<QString, UserEdits> &edits)
{
    for (AcquisitionRow &r : m_rows) {
        auto it = edits.find(r.groupLabel);
        if (it == edits.end()) continue;
        const UserEdits &e = it.value();

        // Only overwrite with snapshot values for fields that were set.
        // Fields populated automatically (from .xisf / location) are also
        // overwritten — the user's manual edit takes priority.
        if (e.hasDate)          { r.date             = e.date;             r.hasDate          = true; }
        if (e.hasGain)          { r.gain             = e.gain;             r.hasGain          = true; }
        if (e.hasSensorCooling) { r.sensorCooling    = e.sensorCooling;    r.hasSensorCooling = true; }
        if (e.hasTemperature)   { r.temperature      = e.temperature;      r.hasTemperature   = true; }
        if (e.hasBortle)        { r.bortle           = e.bortle;           r.hasBortle        = true; }
        if (e.hasMeanSqm)       { r.meanSqm          = e.meanSqm;          r.hasMeanSqm       = true; }
        if (e.hasMeanFwhm)      { r.meanFwhm         = e.meanFwhm;         r.hasMeanFwhm      = true; }
        if (e.hasDarks)         { r.darks            = e.darks;            r.hasDarks         = true; }
        if (e.hasFlats)         { r.flats            = e.flats;            r.hasFlats         = true; }
        if (e.hasFlatDarks)     { r.flatDarks        = e.flatDarks;        r.hasFlatDarks     = true; }
        if (e.hasBias)          { r.bias             = e.bias;             r.hasBias          = true; }
        if (e.hasIso)           { r.iso              = e.iso;              r.hasIso           = true; }
        if (e.hasFNumber)       { r.fNumber          = e.fNumber;          r.hasFNumber       = true; }
        if (e.hasBinning)       { r.binning          = e.binning;          r.hasBinning       = true; }
        if (e.hasFilter)        { r.filterAstrobinId = e.filterAstrobinId; r.hasFilter        = true; }
        if (e.hasNumber)        { r.number           = e.number; }
        if (e.hasDuration)      { r.duration         = e.duration; }
    }
    // Notify views that all data changed
    if (!m_rows.isEmpty())
        emit dataChanged(index(0, 0),
                         index(m_rows.size() - 1, ColCount - 1),
                         {Qt::DisplayRole, Qt::EditRole});
}
