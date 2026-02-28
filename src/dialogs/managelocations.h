#pragma once
#include <QDialog>
#include "settings/appsettings.h"
class QListWidget;
class QLineEdit;
class QSpinBox;
class QDoubleSpinBox;
class QCheckBox;
class QPushButton;

class ManageLocationsDialog : public QDialog {
    Q_OBJECT
public:
    explicit ManageLocationsDialog(QWidget *parent = nullptr);
private slots:
    void onAdd();
    void onRemove();
    void onSave();
    void onSelectionChanged();
private:
    void populateList();
    void loadToEditor(int idx);
    void saveFromEditor(int idx);

    QListWidget    *m_list{nullptr};
    QLineEdit      *m_nameEdit{nullptr};
    QCheckBox      *m_bortleCheck{nullptr};
    QSpinBox       *m_bortleSpin{nullptr};
    QCheckBox      *m_sqmCheck{nullptr};
    QDoubleSpinBox *m_sqmSpin{nullptr};

    QList<Location> m_locations;
    int             m_lastRow{-1};
};
