#pragma once
#include "acquisitionrow.h"
#include "settings/appsettings.h"
#include <QAbstractTableModel>
#include <QList>

class CsvTableModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Col {
        ColGroup = 0, ColDate, ColFilter, ColFilterName, ColNumber, ColDuration,
        ColBinning, ColGain, ColSensorCooling, ColIso, ColFNumber,
        ColDarks, ColFlats, ColFlatDarks, ColBias,
        ColBortle, ColMeanSqm, ColMeanFwhm, ColTemperature,
        ColCount
    };

    explicit CsvTableModel(QObject *parent = nullptr);

    void setRows(const QList<AcquisitionRow> &rows);
    const QList<AcquisitionRow> &rows() const { return m_rows; }

    int rowCount(const QModelIndex &p = {}) const override;
    int columnCount(const QModelIndex &p = {}) const override;
    QVariant data(const QModelIndex &idx, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex &idx, const QVariant &val, int role = Qt::EditRole) override;
    QVariant headerData(int section, Qt::Orientation o, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &idx) const override;
    QString toCsv(const QString &targetFilter = {}, const QSet<int> &hiddenCols = {}) const;
    QString toCsv(const QString &targetFilter = {}) const;
    QString integrationSummary() const;

    // Editable fields that the user may have changed manually.
    // Keyed by groupLabel so they survive a full model reset.
    struct UserEdits {
        // Each optional field: has_* == false means "not edited"
        bool   hasDate{false};          QDate   date;
        bool   hasGain{false};          int     gain{-1};
        bool   hasSensorCooling{false}; int     sensorCooling{0};
        bool   hasTemperature{false};   double  temperature{-1e9};
        bool   hasBortle{false};        int     bortle{-1};
        bool   hasMeanSqm{false};       double  meanSqm{-1};
        bool   hasMeanFwhm{false};      double  meanFwhm{-1};
        bool   hasDarks{false};         int     darks{-1};
        bool   hasFlats{false};         int     flats{-1};
        bool   hasFlatDarks{false};     int     flatDarks{-1};
        bool   hasBias{false};          int     bias{-1};
        bool   hasIso{false};           int     iso{-1};
        bool   hasFNumber{false};       double  fNumber{-1};
        bool   hasBinning{false};       int     binning{1};
        bool   hasFilter{false};        int     filterAstrobinId{-1};
        bool   hasNumber{false};        int     number{0};
        bool   hasDuration{false};      double  duration{0};
    };

    QMap<QString, UserEdits> snapshotEdits() const;
    void applyEdits(const QMap<QString, UserEdits> &edits);
    QStringList groupLabels() const;
    QStringList targetNames() const;

private:
    QList<AcquisitionRow> m_rows;
    static const char *kColumns[ColCount];
    QVariant cellDisplay(const AcquisitionRow &r, int col) const;
    bool setCellValue(AcquisitionRow &r, int col, const QVariant &val);
};
