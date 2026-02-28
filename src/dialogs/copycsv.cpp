#include "copycsv.h"
#include "models/csvtablemodel.h"
#include "settings/appsettings.h"

#include <QComboBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QApplication>
#include <QClipboard>
#include <QFontDatabase>
#include <QTimer>

CopyCsvDialog::CopyCsvDialog(const CsvTableModel *model,
                             const QSet<int>     &hiddenCols,
                             QWidget             *parent)
    : QDialog(parent)
    , m_model(model)
    , m_hiddenCols(hiddenCols)
{
    setWindowTitle(tr("Copy CSV to Clipboard"));
    setMinimumSize(700, 480);

    auto *lay = new QVBoxLayout(this);

    // ── Target selector ──────────────────────────────────────────────
    auto *topRow = new QHBoxLayout;
    topRow->addWidget(new QLabel(tr("Target:")));
    m_targetCombo = new QComboBox;
    m_targetCombo->setMinimumWidth(260);

    const QStringList targets = model->targetNames();
    if (targets.size() > 1) {
        m_targetCombo->addItem(tr("— select a target —"), QString());
        m_targetCombo->addItem(tr("(all targets)"), QStringLiteral("__all__"));
    }
    for (const QString &t : targets)
        m_targetCombo->addItem(t, t);

    // Single-target: select it immediately and show the preview.
    // Multi-target: leave the placeholder selected; preview stays blank.
    m_targetCombo->setCurrentIndex(0);

    topRow->addWidget(m_targetCombo);
    topRow->addStretch();
    lay->addLayout(topRow);

    // ── CSV preview ──────────────────────────────────────────────────
    m_preview = new QPlainTextEdit;
    m_preview->setReadOnly(true);
    m_preview->setLineWrapMode(QPlainTextEdit::NoWrap);
    syncPreviewFont();
    lay->addWidget(m_preview, 1);

    // ── Buttons ──────────────────────────────────────────────────────
    auto *btnRow = new QHBoxLayout;
    btnRow->addStretch();
    m_copyBtn        = new QPushButton(tr("Copy to Clipboard"));
    auto *closeBtn   = new QPushButton(tr("Close"));
    m_copyBtn->setDefault(true);
    btnRow->addWidget(m_copyBtn);
    btnRow->addWidget(closeBtn);
    lay->addLayout(btnRow);

    connect(m_targetCombo, &QComboBox::currentIndexChanged,
            this, &CopyCsvDialog::onTargetChanged);
    connect(m_copyBtn, &QPushButton::clicked,
            this, &CopyCsvDialog::onCopy);
    connect(closeBtn, &QPushButton::clicked,
            this, &QDialog::accept);

    refreshPreview();
}

void CopyCsvDialog::changeEvent(QEvent *e)
{
    if (e->type() == QEvent::ApplicationFontChange)
        syncPreviewFont();
    QDialog::changeEvent(e);
}

void CopyCsvDialog::syncPreviewFont()
{
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPointSize(QApplication::font().pointSize());
    m_preview->setFont(mono);
}

void CopyCsvDialog::onTargetChanged(int)
{
    refreshPreview();
}

void CopyCsvDialog::onCopy()
{
    QApplication::clipboard()->setText(m_preview->toPlainText());

    m_copyBtn->setText(tr("Copied!"));
    m_copyBtn->setEnabled(false);
    QTimer::singleShot(1500, this, [this]() {
        m_copyBtn->setText(tr("Copy to Clipboard"));
        m_copyBtn->setEnabled(true);
    });
}

void CopyCsvDialog::refreshPreview()
{
    const QString data = m_targetCombo->currentData().toString();

    // Placeholder selected ("— select a target —"): data is null/empty.
    if (data.isEmpty()) {
        m_preview->clear();
        m_copyBtn->setEnabled(false);
        return;
    }

    // "__all__" sentinel means no target filter; otherwise filter by name.
    const QString filter = (data == QStringLiteral("__all__")) ? QString() : data;
    m_preview->setPlainText(m_model->toCsv(filter, m_hiddenCols));
    m_copyBtn->setEnabled(true);
}
