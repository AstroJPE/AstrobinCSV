#pragma once
#include <QDialog>
#include "settings/appsettings.h"
class QTableWidget;
class QComboBox;
class QLineEdit;
class QPushButton;
class QLabel;

class ManageFiltersDialog : public QDialog {
    Q_OBJECT
public:
    explicit ManageFiltersDialog(QWidget *parent = nullptr);
private slots:
    void onFetchFilters();
    void onAddMapping();
    void onRemoveMapping();
    void onSave();
    void onFilterListReady(const QList<AstrobinFilter> &filters);
    void onManufacturerChanged();
    void onSearchChanged(const QString &text);
private:
    void populateMappingTable();
    void populateManufacturerCombo();
    void applyFilterToCombo();
    QString displayName(const AstrobinFilter &f) const;

    QLabel       *m_fetchStatusLabel{nullptr};
    QPushButton  *m_fetchBtn{nullptr};
    QTableWidget *m_mappingTable{nullptr};
    QLineEdit    *m_localNameEdit{nullptr};
    QComboBox    *m_manufacturerCombo{nullptr};
    QLineEdit    *m_searchEdit{nullptr};
    QComboBox    *m_astrobinFilterCombo{nullptr};

    QList<AstrobinFilter> m_astrobinFilters;
    QList<FilterMapping>  m_mappings;
};
