#include "managelocations.h"
#include <QListWidget>
#include <QLineEdit>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QDialogButtonBox>
#include <QLabel>

ManageLocationsDialog::ManageLocationsDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Manage Locations"));
    setMinimumWidth(480);
    m_locations = AppSettings::instance().locations();

    auto *outerLay = new QVBoxLayout(this);
    auto *mainLay  = new QHBoxLayout;

    auto *leftLay = new QVBoxLayout;
    m_list = new QListWidget;
    leftLay->addWidget(m_list);
    auto *btnRow = new QHBoxLayout;
    auto *addBtn = new QPushButton(tr("+ Add"));
    auto *delBtn = new QPushButton(tr("− Remove"));
    btnRow->addWidget(addBtn);
    btnRow->addWidget(delBtn);
    leftLay->addLayout(btnRow);
    mainLay->addLayout(leftLay, 1);

    auto *editorBox = new QGroupBox(tr("Location Details"));
    auto *form      = new QFormLayout(editorBox);

    m_nameEdit = new QLineEdit;
    form->addRow(tr("Name:"), m_nameEdit);

    m_bortleCheck = new QCheckBox(tr("Include Bortle"));
    m_bortleSpin  = new QSpinBox;
    m_bortleSpin->setRange(1, 9);
    auto *bortleRow = new QHBoxLayout;
    bortleRow->addWidget(m_bortleCheck);
    bortleRow->addWidget(m_bortleSpin);
    form->addRow(tr("Bortle:"), bortleRow);

    m_sqmCheck = new QCheckBox(tr("Include Mean SQM"));
    m_sqmSpin  = new QDoubleSpinBox;
    m_sqmSpin->setRange(0, 30);
    m_sqmSpin->setDecimals(2);
    auto *sqmRow = new QHBoxLayout;
    sqmRow->addWidget(m_sqmCheck);
    sqmRow->addWidget(m_sqmSpin);
    form->addRow(tr("Mean SQM:"), sqmRow);

    mainLay->addWidget(editorBox, 2);
    outerLay->addLayout(mainLay, 1);

    auto *bbox = new QDialogButtonBox(
        QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    outerLay->addWidget(bbox);

    connect(addBtn, &QPushButton::clicked,
            this, &ManageLocationsDialog::onAdd);
    connect(delBtn, &QPushButton::clicked,
            this, &ManageLocationsDialog::onRemove);
    connect(m_list, &QListWidget::currentRowChanged,
            this,   &ManageLocationsDialog::onSelectionChanged);
    connect(bbox, &QDialogButtonBox::accepted,
            this, &ManageLocationsDialog::onSave);
    connect(bbox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_bortleCheck, &QCheckBox::toggled,
            m_bortleSpin, &QSpinBox::setEnabled);
    connect(m_sqmCheck,    &QCheckBox::toggled,
            m_sqmSpin,    &QDoubleSpinBox::setEnabled);

    populateList();
}

void ManageLocationsDialog::populateList() {
    m_list->clear();
    for (const Location &loc : std::as_const(m_locations))
        m_list->addItem(loc.name.isEmpty() ? tr("(unnamed)") : loc.name);
}

void ManageLocationsDialog::onAdd() {
    int prev = m_list->currentRow();
    if (prev >= 0) saveFromEditor(prev);
    Location loc;
    m_locations << loc;
    m_list->addItem(tr("(unnamed)"));
    m_list->setCurrentRow(m_locations.size() - 1);
    m_nameEdit->setFocus();
    m_nameEdit->selectAll();
}

void ManageLocationsDialog::onRemove() {
    int row = m_list->currentRow();
    if (row < 0 || row >= m_locations.size()) return;
    m_locations.removeAt(row);
    m_list->takeItem(row);
    m_lastRow = -1;
}

void ManageLocationsDialog::onSave() {
    QString editedName = m_nameEdit->text().trimmed();
    if (m_locations.isEmpty() && !editedName.isEmpty()) {
        Location loc;
        m_locations << loc;
        m_list->addItem(editedName);
        m_lastRow = 0;
        m_list->setCurrentRow(0);
    }
    int current = m_list->currentRow();
    if (current >= 0) saveFromEditor(current);
    AppSettings::instance().setLocations(m_locations);
    accept();
}

void ManageLocationsDialog::onSelectionChanged() {
    if (m_lastRow >= 0 && m_lastRow < m_locations.size())
        saveFromEditor(m_lastRow);
    int row = m_list->currentRow();
    m_lastRow = row;
    if (row < 0 || row >= m_locations.size()) return;
    loadToEditor(row);
}

void ManageLocationsDialog::loadToEditor(int idx) {
    const Location &loc = m_locations[idx];
    m_nameEdit->setText(loc.name);
    m_bortleCheck->setChecked(loc.hasBortle);
    m_bortleSpin->setValue(loc.hasBortle ? loc.bortle : 4);
    m_bortleSpin->setEnabled(loc.hasBortle);
    m_sqmCheck->setChecked(loc.hasMeanSqm);
    m_sqmSpin->setValue(loc.hasMeanSqm ? loc.meanSqm : 20.0);
    m_sqmSpin->setEnabled(loc.hasMeanSqm);
}

void ManageLocationsDialog::saveFromEditor(int idx) {
    if (idx < 0 || idx >= m_locations.size()) return;
    Location &loc  = m_locations[idx];
    loc.name       = m_nameEdit->text().trimmed();
    loc.hasBortle  = m_bortleCheck->isChecked();
    loc.bortle     = m_bortleSpin->value();
    loc.hasMeanSqm = m_sqmCheck->isChecked();
    loc.meanSqm    = m_sqmSpin->value();
    m_list->item(idx)->setText(
        loc.name.isEmpty() ? tr("(unnamed)") : loc.name);
}
