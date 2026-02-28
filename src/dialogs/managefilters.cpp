#include "managefilters.h"
#include "filterwebscraper.h"
#include <QTableWidget>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QSet>
#include <QFontMetrics>

ManageFiltersDialog::ManageFiltersDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Manage Filters"));
    setMinimumSize(700, 520);

    m_astrobinFilters = AppSettings::instance().cachedAstrobinFilters();
    m_mappings        = AppSettings::instance().filterMappings();

    auto *lay = new QVBoxLayout(this);

    auto *fetchBox = new QGroupBox(tr("Astrobin Filter Database"));
    auto *fetchLay = new QHBoxLayout(fetchBox);
    m_fetchBtn     = new QPushButton(tr("Refresh Filter List from Astrobin"));
    m_fetchStatusLabel = new QLabel(
        m_astrobinFilters.isEmpty()
            ? tr("No filters cached yet.")
            : tr("%1 filters cached.").arg(m_astrobinFilters.size()));
    m_fetchStatusLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    fetchLay->addWidget(m_fetchBtn);
    fetchLay->addWidget(m_fetchStatusLabel, 1);
    lay->addWidget(fetchBox);

    auto *mapBox = new QGroupBox(tr("Filter Name Mappings"));
    auto *mapLay = new QVBoxLayout(mapBox);

    m_mappingTable = new QTableWidget;
    m_mappingTable->setColumnCount(3);
    m_mappingTable->setHorizontalHeaderLabels(
        {tr("Your Name"), tr("Astrobin ID"), tr("Astrobin Name")});
    m_mappingTable->horizontalHeader()->setStretchLastSection(true);
    m_mappingTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_mappingTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    mapLay->addWidget(m_mappingTable);

    auto *selectionBox = new QGroupBox(tr("Select Astrobin Filter"));
    auto *selLay       = new QVBoxLayout(selectionBox);

    auto *filterRow = new QHBoxLayout;
    filterRow->addWidget(new QLabel(tr("Manufacturer:")));
    m_manufacturerCombo = new QComboBox;
    m_manufacturerCombo->setMinimumWidth(200);
    filterRow->addWidget(m_manufacturerCombo);
    filterRow->addSpacing(12);
    filterRow->addWidget(new QLabel(tr("Search:")));
    m_searchEdit = new QLineEdit;
    m_searchEdit->setPlaceholderText(tr("Type to filter by name…"));
    m_searchEdit->setClearButtonEnabled(true);
    filterRow->addWidget(m_searchEdit, 1);
    selLay->addLayout(filterRow);

    m_astrobinFilterCombo = new QComboBox;
    m_astrobinFilterCombo->setMinimumWidth(400);
    selLay->addWidget(m_astrobinFilterCombo);

    auto *addRow = new QHBoxLayout;
    m_localNameEdit = new QLineEdit;
    m_localNameEdit->setPlaceholderText(tr("Your filter name, e.g. H, L, R, O3…"));

    // Size the field to comfortably display the full placeholder text.
    {
        const QFontMetrics fm(m_localNameEdit->font());
        const int textWidth = fm.horizontalAdvance(
            m_localNameEdit->placeholderText());
        // Add padding for the field's internal margins on both sides.
        m_localNameEdit->setMinimumWidth(textWidth + 24);
    }

    auto *addBtn = new QPushButton(tr("Add Mapping"));
    auto *delBtn = new QPushButton(tr("Remove Selected"));
    addRow->addWidget(new QLabel(tr("Your name:")));
    addRow->addWidget(m_localNameEdit);
    addRow->addWidget(addBtn);
    addRow->addWidget(delBtn);
    addRow->addStretch();
    selLay->addLayout(addRow);

    mapLay->addWidget(selectionBox);
    lay->addWidget(mapBox, 1);

    auto *bbox = new QDialogButtonBox(
        QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    lay->addWidget(bbox);

    connect(m_fetchBtn, &QPushButton::clicked,
            this, &ManageFiltersDialog::onFetchFilters);
    connect(addBtn, &QPushButton::clicked,
            this, &ManageFiltersDialog::onAddMapping);
    connect(delBtn, &QPushButton::clicked,
            this, &ManageFiltersDialog::onRemoveMapping);
    connect(bbox, &QDialogButtonBox::accepted,
            this, &ManageFiltersDialog::onSave);
    connect(bbox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_manufacturerCombo,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ManageFiltersDialog::onManufacturerChanged);
    connect(m_searchEdit, &QLineEdit::textChanged,
            this, &ManageFiltersDialog::onSearchChanged);

    populateMappingTable();
    populateManufacturerCombo();
    applyFilterToCombo();
}

QString ManageFiltersDialog::displayName(const AstrobinFilter &f) const
{
    QString full = f.brandName.isEmpty()
                       ? f.name
                       : f.brandName + QStringLiteral(" ") + f.name;
    return full + QStringLiteral(" [%1]").arg(f.id);
}

void ManageFiltersDialog::populateMappingTable() {
    m_mappingTable->setRowCount(m_mappings.size());
    for (int i = 0; i < m_mappings.size(); ++i) {
        const auto &fm = m_mappings[i];
        m_mappingTable->setItem(
            i, 0, new QTableWidgetItem(fm.localName));
        m_mappingTable->setItem(
            i, 1, new QTableWidgetItem(QString::number(fm.astrobinId)));
        m_mappingTable->setItem(
            i, 2, new QTableWidgetItem(fm.astrobinName));
    }
}

void ManageFiltersDialog::populateManufacturerCombo() {
    m_manufacturerCombo->blockSignals(true);
    m_manufacturerCombo->clear();
    m_manufacturerCombo->addItem(tr("(All manufacturers)"), QString());
    QSet<QString> brands;
    for (const auto &f : m_astrobinFilters)
        if (!f.brandName.isEmpty())
            brands.insert(f.brandName);
    QStringList sorted = brands.values();
    std::sort(sorted.begin(), sorted.end(),
              [](const QString &a, const QString &b) {
                  return a.compare(b, Qt::CaseInsensitive) < 0;
              });
    for (const auto &b : sorted)
        m_manufacturerCombo->addItem(b, b);
    m_manufacturerCombo->blockSignals(false);
}

void ManageFiltersDialog::applyFilterToCombo() {
    QString selectedBrand = m_manufacturerCombo->currentData().toString();
    QString search        = m_searchEdit->text().trimmed();
    m_astrobinFilterCombo->clear();
    if (m_astrobinFilters.isEmpty()) {
        m_astrobinFilterCombo->addItem(
            tr("(no filters cached — click Refresh)"));
        return;
    }
    int count = 0;
    for (const auto &f : m_astrobinFilters) {
        if (!selectedBrand.isEmpty() &&
            f.brandName.compare(selectedBrand, Qt::CaseInsensitive) != 0)
            continue;
        if (!search.isEmpty()) {
            QString combined = f.brandName + QStringLiteral(" ") + f.name;
            if (!combined.contains(search, Qt::CaseInsensitive))
                continue;
        }
        m_astrobinFilterCombo->addItem(displayName(f), f.id);
        ++count;
    }
    if (count == 0)
        m_astrobinFilterCombo->addItem(tr("(no matches)"));
}

void ManageFiltersDialog::onManufacturerChanged() { applyFilterToCombo(); }
void ManageFiltersDialog::onSearchChanged(const QString &) { applyFilterToCombo(); }

void ManageFiltersDialog::onFetchFilters() {
    m_fetchBtn->setEnabled(false);
    m_fetchStatusLabel->setText(tr("Fetching… this may take a minute."));
    auto *scraper = new FilterWebScraper(this);
    connect(scraper, &FilterWebScraper::finished,
            this, &ManageFiltersDialog::onFilterListReady);
    connect(scraper, &FilterWebScraper::statusUpdate,
            this, [this](const QString &msg) {
                const QString elided =
                    m_fetchStatusLabel->fontMetrics().elidedText(
                        msg, Qt::ElideMiddle,
                        m_fetchStatusLabel->width());
                m_fetchStatusLabel->setText(elided);
                m_fetchStatusLabel->setToolTip(msg);
            });
    scraper->start();
}

void ManageFiltersDialog::onFilterListReady(
    const QList<AstrobinFilter> &filters) {
    m_fetchBtn->setEnabled(true);
    if (filters.isEmpty()) {
        m_fetchStatusLabel->setText(
            tr("Fetch failed or returned no results."));
        return;
    }
    m_astrobinFilters = filters;
    AppSettings::instance().setCachedAstrobinFilters(filters);
    m_fetchStatusLabel->setText(
        tr("%1 filters fetched and cached.").arg(filters.size()));
    populateManufacturerCombo();
    applyFilterToCombo();
}

void ManageFiltersDialog::onAddMapping() {
    QString local = m_localNameEdit->text().trimmed();
    if (local.isEmpty()) return;
    int id = m_astrobinFilterCombo->currentData().toInt();
    if (id <= 0) return;
    QString storedName;
    for (const auto &f : m_astrobinFilters) {
        if (f.id == id) {
            storedName = f.brandName.isEmpty()
                             ? f.name
                             : f.brandName + QStringLiteral(" ") + f.name;
            break;
        }
    }
    for (auto &fm : m_mappings) {
        if (fm.localName.compare(local, Qt::CaseInsensitive) == 0) {
            fm.astrobinId = id; fm.astrobinName = storedName;
            populateMappingTable(); return;
        }
    }
    FilterMapping fm;
    fm.localName = local; fm.astrobinId = id; fm.astrobinName = storedName;
    m_mappings << fm;
    populateMappingTable();
}

void ManageFiltersDialog::onRemoveMapping() {
    int row = m_mappingTable->currentRow();
    if (row < 0 || row >= m_mappings.size()) return;
    m_mappings.removeAt(row);
    populateMappingTable();
}

void ManageFiltersDialog::onSave() {
    AppSettings::instance().setFilterMappings(m_mappings);
    accept();
}
