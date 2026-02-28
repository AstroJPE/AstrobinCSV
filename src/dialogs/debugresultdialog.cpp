#include "debugresultdialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QGroupBox>
#include <QProcess>
#include <QFileInfo>
#include <QDialogButtonBox>
#include <QDir>

DebugResultDialog::DebugResultDialog(const QString &humanPath,
                                     const QString &jsonPath,
                                     QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Debug Log Created"));
    setMinimumWidth(560);

    auto *lay = new QVBoxLayout(this);

    auto *intro = new QLabel(
        tr("Debug logging is enabled. Two log files were produced for this import:"));
    intro->setWordWrap(true);
    lay->addWidget(intro);

    // ── File paths ───────────────────────────────────────────────────────
    auto makePathRow = [&](const QString &label, const QString &path) {
        auto *box     = new QGroupBox(label);
        auto *boxLay  = new QHBoxLayout(box);
        auto *edit    = new QLineEdit(path);
        edit->setReadOnly(true);
        edit->setToolTip(path);
        boxLay->addWidget(edit, 1);
        lay->addWidget(box);
    };

    makePathRow(tr("Human-readable log (.log)"), humanPath);
    makePathRow(tr("Machine-readable log (.json)"), jsonPath);

    // ── Buttons ──────────────────────────────────────────────────────────
    auto *btnRow  = new QHBoxLayout;
    auto *revealBtn = new QPushButton(tr("Show in Finder / File Explorer"));
    auto *closeBtn  = new QPushButton(tr("Close"));
    closeBtn->setDefault(true);
    btnRow->addWidget(revealBtn);
    btnRow->addStretch();
    btnRow->addWidget(closeBtn);
    lay->addLayout(btnRow);

    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);

    connect(revealBtn, &QPushButton::clicked, this, [humanPath]() {
#if defined(Q_OS_MACOS)
        // -R selects (reveals) the file in Finder
        QProcess::startDetached(
            QStringLiteral("/usr/bin/osascript"),
            {QStringLiteral("-e"),
             QStringLiteral("tell application \"Finder\" to reveal POSIX file \"%1\"")
                 .arg(humanPath)});
        QProcess::startDetached(
            QStringLiteral("/usr/bin/osascript"),
            {QStringLiteral("-e"),
             QStringLiteral("tell application \"Finder\" to activate")});
#elif defined(Q_OS_WIN)
        // /select highlights the file in Explorer
        QProcess::startDetached(
            QStringLiteral("explorer.exe"),
            {QStringLiteral("/select,\"%1\"").arg(QDir::toNativeSeparators(humanPath))});
#else
        // Fallback: open the containing directory
        QProcess::startDetached(
            QStringLiteral("xdg-open"),
            {QFileInfo(humanPath).absolutePath()});
#endif
    });
}
