#include "aboutdialog.h"
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QApplication>

AboutDialog::AboutDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("About AstrobinCSV"));
    auto *lay = new QVBoxLayout(this);
    lay->addWidget(new QLabel(QStringLiteral(
        "<h2>AstrobinCSV</h2>"
        // "<p>Converts PixInsight / Siril stacking logs into<br>"
        "<p>Converts PixInsight stacking logs into<br>"
        "Astrobin-compatible acquisition CSV files.</p>"
        "<p>Version %1</p>"
        "<p><small>Built with Qt 6.</small></p>")
            .arg(QApplication::applicationVersion())));
    auto *btn = new QPushButton(tr("Close"));
    connect(btn, &QPushButton::clicked, this, &QDialog::accept);
    lay->addWidget(btn);
}
