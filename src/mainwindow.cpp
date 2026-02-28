#include "mainwindow.h"
#include "models/csvtablemodel.h"
#include "logparser/pixinsightlogparser.h"
#include "logparser/sirillogparser.h"
#include "xisfheaderreader.h"
#include "xisfresolveworker.h"
#include "settings/appsettings.h"
#include "dialogs/managelocations.h"
#include "dialogs/managefilters.h"
#include "dialogs/managetargets.h"
#include "dialogs/aboutdialog.h"
#include "dialogs/copycsv.h"
#include "dialogs/debugresultdialog.h"
#include "debuglogger.h"

#include <QMenuBar>
#include <QToolBar>
#include <QComboBox>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QListWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QStatusBar>
#include <QHeaderView>
#include <QApplication>
#include <QFileInfo>
#include <QDir>
#include <QDirIterator>
#include <QThread>
#include <QAction>
#include <QMap>
#include <QSet>
#include <QStandardPaths>
#include <QRegularExpression>
#include <QFontDatabase>
#include <QGroupBox>
#include <QSplitter>
#include <QPainter>
#include <QEventLoop>
#include <cmath>

// ── Styled splitter handle ──────────────────────────────────────────
class GripHandle : public QSplitterHandle {
public:
    explicit GripHandle(Qt::Orientation o, QSplitter *parent)
        : QSplitterHandle(o, parent) {}
protected:
    QSize sizeHint() const override {
        return orientation() == Qt::Vertical
                   ? QSize(0, 12) : QSize(12, 0);
    }
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        QRect r = rect();

        QColor bg = palette().color(QPalette::Window).darker(110);
        p.fillRect(r, bg);

        const int dotD   = 3;
        const int gap    = 5;
        const int nDots  = 7;
        const int totalW = nDots * dotD + (nDots - 1) * gap;
        int x = r.center().x() - totalW / 2;
        int y = r.center().y() - dotD / 2;

        QColor dot = palette().color(QPalette::Mid);
        p.setRenderHint(QPainter::Antialiasing);
        p.setBrush(dot);
        p.setPen(Qt::NoPen);
        for (int i = 0; i < nDots; ++i) {
            p.drawEllipse(x, y, dotD, dotD);
            x += dotD + gap;
        }
    }
    void enterEvent(QEnterEvent *e) override {
        setCursor(Qt::SplitVCursor);
        QSplitterHandle::enterEvent(e);
    }
    void leaveEvent(QEvent *e) override {
        unsetCursor();
        QSplitterHandle::leaveEvent(e);
    }
};

class GripSplitter : public QSplitter {
public:
    explicit GripSplitter(Qt::Orientation o, QWidget *parent = nullptr)
        : QSplitter(o, parent) {}
protected:
    QSplitterHandle *createHandle() override {
        return new GripHandle(orientation(), this);
    }
};

// ─────────────────────────────────────────────────────────────────────

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("AstrobinCSV"));
    resize(1200, 750);
    m_model = new CsvTableModel(this);
    buildMenu();
    buildToolBar();
    buildCentralWidget();
    m_currentTheme = AppSettings::instance().theme();
    applyTheme(m_currentTheme);
    m_groupingCombo->setCurrentIndex(AppSettings::instance().groupingStrategy());

    // Capture the system default font size before applying any saved value.
    // This is what Ctrl/Cmd+0 resets to.
    m_baseFontSize = QApplication::font().pointSize();

    // Restore saved font size if one was set by the user.
    const int savedFontSize = AppSettings::instance().fontSize();
    if (savedFontSize > 0 && savedFontSize != m_baseFontSize)
        changeFontSize(savedFontSize, false);  // false = don't save, already saved

    const QByteArray geo = AppSettings::instance().windowGeometry();
    if (!geo.isEmpty())
        restoreGeometry(geo);

    const QByteArray split = AppSettings::instance().splitterState();
    if (!split.isEmpty())
        m_splitter->restoreState(split);

    checkForOldDebugLogs();
}

void MainWindow::buildMenu()
{
    auto *fileMenu = menuBar()->addMenu(tr("&File"));

    auto *exportAct = new QAction(tr("&Export CSV…"), this);
    exportAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_E));
    connect(exportAct, &QAction::triggered, this, &MainWindow::onExportCsv);
    fileMenu->addAction(exportAct);

    auto *copyAct = new QAction(tr("&Copy CSV to Clipboard…"), this);
    copyAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_K));
    connect(copyAct, &QAction::triggered, this, &MainWindow::onCopyCsv);
    fileMenu->addAction(copyAct);

    fileMenu->addSeparator();

    auto *quitAct = new QAction(tr("&Quit"), this);
    quitAct->setShortcut(QKeySequence::Quit);
    connect(quitAct, &QAction::triggered, qApp, &QApplication::quit);
    fileMenu->addAction(quitAct);

    auto *toolsMenu = menuBar()->addMenu(tr("&Tools"));

    auto *locAct = new QAction(tr("Manage &Locations…"), this);
    connect(locAct, &QAction::triggered, this, &MainWindow::onManageLocations);
    toolsMenu->addAction(locAct);

    auto *filtAct = new QAction(tr("Manage &Filters…"), this);
    connect(filtAct, &QAction::triggered, this, &MainWindow::onManageFilters);
    toolsMenu->addAction(filtAct);

    auto *targAct = new QAction(tr("Manage &Targets…"), this);
    connect(targAct, &QAction::triggered, this, &MainWindow::onManageTargets);
    toolsMenu->addAction(targAct);

    toolsMenu->addSeparator();

    m_themeAction = new QAction(tr("Switch to Dark Theme"), this);
    connect(m_themeAction, &QAction::triggered, this, &MainWindow::onToggleTheme);
    toolsMenu->addAction(m_themeAction);

    toolsMenu->addSeparator();

    m_debugLogAction = new QAction(tr("Enable Debug Logging"), this);
    m_debugLogAction->setCheckable(true);
    m_debugLogAction->setChecked(false);
    connect(m_debugLogAction, &QAction::triggered,
            this, &MainWindow::onToggleDebugLogging);
    toolsMenu->addAction(m_debugLogAction);

    auto *helpMenu = menuBar()->addMenu(tr("&Help"));
    auto *aboutAct = new QAction(tr("&About AstrobinCSV…"), this);
    connect(aboutAct, &QAction::triggered, this, &MainWindow::onAbout);
    helpMenu->addAction(aboutAct);
}

void MainWindow::buildToolBar()
{
    auto *tb = addToolBar(tr("Main"));
    tb->setMovable(false);
    tb->addAction(tr("Export CSV"),      this, &MainWindow::onExportCsv);
    tb->addAction(tr("Copy CSV"),        this, &MainWindow::onCopyCsv);
    tb->addSeparator();
    tb->addAction(tr("Manage Locations"),this, &MainWindow::onManageLocations);
    tb->addAction(tr("Manage Filters"),  this, &MainWindow::onManageFilters);
    tb->addAction(tr("Manage Targets"),  this, &MainWindow::onManageTargets);
}

void MainWindow::buildCentralWidget()
{
    auto *central = new QWidget(this);
    auto *vlay    = new QVBoxLayout(central);
    vlay->setContentsMargins(8, 8, 8, 8);
    vlay->setSpacing(6);

    // ── Log files panel ──────────────────────────────────────
    auto *logBox    = new QGroupBox(tr("Loaded Log Files"));
    auto *logLay    = new QHBoxLayout(logBox);
    m_logFileList   = new QListWidget;
    m_logFileList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    // Size the list to show exactly 3 rows regardless of platform DPI or
    // font metrics. Use font metrics plus a small padding per row to match
    // the default item delegate's rendering on all platforms.
    {
        const QFontMetrics fm(m_logFileList->font());
        const int rowH   = fm.height() + 6;  // 6px padding matches Qt's default item delegate
        const int frameH = m_logFileList->frameWidth() * 2;
        m_logFileList->setFixedHeight(rowH * 6 + frameH);
    }
    logLay->addWidget(m_logFileList, 1);

    auto *logBtnLay = new QVBoxLayout;
    auto *addLogBtn = new QPushButton(tr("Add Log…"));
    auto *remLogBtn = new QPushButton(tr("Remove"));
    logBtnLay->addWidget(addLogBtn);
    logBtnLay->addWidget(remLogBtn);
    logBtnLay->addStretch();
    logLay->addLayout(logBtnLay);
    vlay->addWidget(logBox);

    connect(addLogBtn, &QPushButton::clicked, this, &MainWindow::onAddLog);
    connect(remLogBtn, &QPushButton::clicked, this, &MainWindow::onRemoveLog);

    // ── Control row ──────────────────────────────────────────
    auto *ctrlRow = new QHBoxLayout;

    ctrlRow->addWidget(new QLabel(tr("Location:")));
    m_locationCombo = new QComboBox;
    m_locationCombo->setMinimumWidth(160);
    m_locationCombo->setToolTip(tr("Select observing location. "
                                   "Bortle and SQM will be populated from this selection."));
    ctrlRow->addWidget(m_locationCombo);

    m_locationCombo->clear();
    m_locationCombo->addItem(tr("(none)"));
    for (const auto &loc : AppSettings::instance().locations())
        m_locationCombo->addItem(loc.name);

    ctrlRow->addSpacing(20);
    ctrlRow->addWidget(new QLabel(tr("Row grouping:")));
    m_groupingCombo = new QComboBox;
    m_groupingCombo->addItem(tr("One row per date"),                    ByDate);
    m_groupingCombo->addItem(tr("One row per date + gain + temp"),      ByDateGainTemp);
    m_groupingCombo->addItem(tr("Collapsed (one row per integration)"), Collapsed);
    m_groupingCombo->setCurrentIndex(1);
    connect(m_groupingCombo, &QComboBox::currentIndexChanged,
            this, &MainWindow::onGroupingChanged);
    ctrlRow->addWidget(m_groupingCombo);
    ctrlRow->addStretch();

    m_progressBar = new QProgressBar;
    m_progressBar->setVisible(false);
    m_progressBar->setMaximumWidth(200);
    ctrlRow->addWidget(m_progressBar);

    m_cancelBtn = new QPushButton(tr("Cancel"));
    m_cancelBtn->setVisible(false);
    connect(m_cancelBtn, &QPushButton::clicked, this, [this]() {
        m_cancelRequested.storeRelease(1);
    });
    ctrlRow->addWidget(m_cancelBtn);

    vlay->addLayout(ctrlRow);

    // ── Table view ───────────────────────────────────────────
    m_tableView = new AcquisitionTableView;
    m_proxyModel = new QSortFilterProxyModel(this);
    m_proxyModel->setSourceModel(m_model);
    m_proxyModel->setSortCaseSensitivity(Qt::CaseInsensitive);
    m_proxyModel->setSortRole(Qt::DisplayRole);
    m_tableView->setModel(m_proxyModel);
    m_tableView->setSortingEnabled(true);
    m_tableView->horizontalHeader()->setSortIndicatorShown(true);
    m_tableView->horizontalHeader()->setSortIndicator(-1, Qt::AscendingOrder);
    m_tableView->horizontalHeader()->setStretchLastSection(false);

    m_tableView->horizontalHeader()->setSectionResizeMode(
        QHeaderView::Interactive);
    m_tableView->horizontalHeader()->setSectionResizeMode(
        CsvTableModel::ColGroup, QHeaderView::Stretch);
    connect(m_tableView->horizontalHeader(), &QHeaderView::sectionDoubleClicked,
            this, [this](int col) {
                m_tableView->resizeColumnToContents(col);
            });

    m_tableView->setAlternatingRowColors(true);
    m_tableView->setSelectionBehavior(QAbstractItemView::SelectItems);
    m_tableView->setEditTriggers(QAbstractItemView::DoubleClicked |
                                 QAbstractItemView::SelectedClicked);

    // ── Splitter: table (top) + summary (bottom) ─────────────
    m_splitter = new GripSplitter(Qt::Vertical);
    m_splitter->setChildrenCollapsible(false);

    auto *topPane = new QWidget;
    auto *topLay  = new QVBoxLayout(topPane);
    topLay->setContentsMargins(0, 0, 0, 0);
    topLay->setSpacing(2);
    topLay->addWidget(m_tableView, 1);

    auto *legendLabel = new QLabel(
        tr("[*] Display-only column — not included in exported CSV"));
    legendLabel->setStyleSheet(
        QStringLiteral("color: gray; font-size: 11px;"));
    legendLabel->setAlignment(Qt::AlignRight);
    topLay->addWidget(legendLabel);

    m_splitter->addWidget(topPane);

    auto *summaryBox = new QGroupBox(tr("Integration Time Summary"));
    auto *summaryLay = new QVBoxLayout(summaryBox);
    summaryLay->setContentsMargins(6, 6, 6, 6);
    m_summaryEdit = new QPlainTextEdit;
    m_summaryEdit->setReadOnly(true);
    m_summaryEdit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_summaryEdit->setPlaceholderText(
        tr("Integration totals will appear here once a log is loaded."));
    summaryLay->addWidget(m_summaryEdit);
    m_splitter->addWidget(summaryBox);

    m_splitter->setStretchFactor(0, 7);
    m_splitter->setStretchFactor(1, 3);

    connect(m_splitter, &QSplitter::splitterMoved, this, [this]() {
        AppSettings::instance().setSplitterState(m_splitter->saveState());
    });

    vlay->addWidget(m_splitter, 1);

    m_statusLabel = new QLabel(tr("No log loaded."));
    statusBar()->addWidget(m_statusLabel, 1);

    setCentralWidget(central);

    connect(m_locationCombo, &QComboBox::currentIndexChanged,
            this, &MainWindow::rebuildRows);
}

void MainWindow::applyTheme(const QString &theme)
{
    if (theme == QStringLiteral("dark")) {
        qApp->setStyleSheet(
            "QMainWindow,QDialog,QWidget{background:#2b2b2b;color:#f0f0f0;}"
            "QTableView{gridline-color:#555;alternate-background-color:#333;}"
            "QHeaderView::section{background:#3c3c3c;color:#f0f0f0;"
            "border:1px solid #555;}"
            "QMenuBar{background:#3c3c3c;color:#f0f0f0;}"
            "QMenu{background:#3c3c3c;color:#f0f0f0;}"
            "QToolBar{background:#3c3c3c;}"
            "QComboBox{background:#3c3c3c;color:#f0f0f0;border:1px solid #666;}"
            "QPushButton{background:#3c3c3c;color:#f0f0f0;"
            "border:1px solid #666;padding:4px 8px;}"
            );
        if (m_themeAction) m_themeAction->setText(tr("Switch to Light Theme"));
    } else {
        qApp->setStyleSheet({});
        if (m_themeAction) m_themeAction->setText(tr("Switch to Dark Theme"));
    }
    m_currentTheme = theme;
    AppSettings::instance().setTheme(theme);
}

void MainWindow::checkForOldDebugLogs()
{
    const QStringList oldFiles = DebugLogger::existingDebugLogFiles();
    if (oldFiles.isEmpty()) return;

    const int fileCount = oldFiles.size();
    const QString logDir = DebugLogger::debugLogDirectory();

    QMessageBox msg(this);
    msg.setWindowTitle(tr("Old Debug Logs Found"));
    msg.setIcon(QMessageBox::Question);
    msg.setText(tr("%n debug log file(s) from a previous session were found.",
                   nullptr, fileCount));
    msg.setInformativeText(
        tr("Location: %1\n\nWould you like to delete them now?").arg(logDir));
    msg.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    msg.setDefaultButton(QMessageBox::Yes);

    if (msg.exec() == QMessageBox::Yes) {
        const int removed = DebugLogger::removeOldDebugLogs();
        if (removed != fileCount) {
            QMessageBox::warning(
                this,
                tr("Cleanup Incomplete"),
                tr("Deleted %1 of %2 file(s). Some files could not be removed.\n\nLocation: %3")
                    .arg(removed).arg(fileCount).arg(logDir));
        }
    }
}

void MainWindow::onToggleDebugLogging()
{
    const bool on = m_debugLogAction->isChecked();
    DebugLogger::instance().setEnabled(on);
    statusBar()->showMessage(
        on ? tr("Debug logging enabled — active on next import")
           : tr("Debug logging disabled"),
        4000);
}

void MainWindow::onAddLog()
{
    QString dir = AppSettings::instance().lastOpenDirectory();
    if (dir.isEmpty() || !QDir(dir).exists())
        dir = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);

    QFileDialog dlg(this,
                    tr("Add PixInsight / Siril Log File"),
                    dir,
                    tr("Log Files (*.log);;All Files (*)"));
    dlg.setFileMode(QFileDialog::ExistingFiles);
    if (dlg.exec() != QDialog::Accepted) return;
    QStringList files = dlg.selectedFiles();
    if (files.isEmpty()) return;

    AppSettings::instance().setLastOpenDirectory(
        QFileInfo(files.first()).absolutePath());

    QSet<QString> loadedPaths;
    for (int i = 0; i < m_logFileList->count(); ++i)
        loadedPaths.insert(m_logFileList->item(i)->data(Qt::UserRole).toString());

    // ── Begin debug session if enabled ───────────────────────────────────
    auto &dbg = DebugLogger::instance();
    if (dbg.isEnabled())
        dbg.beginSession();

    PixInsightLogParser piParser;
    SirilLogParser      sirilParser;
    QList<AcquisitionGroup> newGroups;

    for (const QString &path : files) {
        if (loadedPaths.contains(path)) continue;

        ILogParser *parser = nullptr;
        if (piParser.canParse(path))         parser = &piParser;
        else if (sirilParser.canParse(path)) parser = &sirilParser;

        if (!parser) {
            if (dbg.isSessionActive())
                dbg.logWarning(
                    QStringLiteral("Unknown log format: %1").arg(path));
            QMessageBox::warning(this, tr("Unknown Log Format"),
                                 tr("Could not identify the log format of:\n%1").arg(path));
            continue;
        }

        auto groups = parser->parse(path);
        if (groups.isEmpty()) {
            if (dbg.isSessionActive())
                dbg.logError(
                    QStringLiteral("No groups found in %1: %2")
                        .arg(path, parser->errorString()));
            QMessageBox::warning(this, tr("Parse Error"),
                                 tr("No integration groups found in:\n%1\n\n%2")
                                     .arg(path, parser->errorString()));
            continue;
        }

        auto *item = new QListWidgetItem(QFileInfo(path).fileName());
        item->setData(Qt::UserRole, path);
        item->setToolTip(path);
        m_logFileList->addItem(item);

        newGroups << groups;
    }

    if (newGroups.isEmpty()) {
        if (dbg.isSessionActive()) dbg.endSession();
        return;
    }

    resolveXisfHeaders(newGroups);

    QStringList allLogPaths;
    for (int i = 0; i < m_logFileList->count(); ++i)
        allLogPaths << m_logFileList->item(i)->data(Qt::UserRole).toString();
    // Pass all groups (existing + new) to resolveCalibrationBlocks so that
    // loading a second log file can fill in calibration counts (e.g. bias)
    // that were missing from groups loaded by a previous log file.
    QList<AcquisitionGroup> allGroups = m_groups + newGroups;
    resolveCalibrationBlocks(allGroups, allLogPaths);

    // Copy the resolved calibration counts back to m_groups for any groups
    // that were already loaded — they share the same order since allGroups
    // is m_groups followed by newGroups.
    for (int i = 0; i < m_groups.size(); ++i) {
        m_groups[i].darks = allGroups[i].darks;
        m_groups[i].flats = allGroups[i].flats;
        m_groups[i].bias  = allGroups[i].bias;
    }
    // newGroups gets its counts from the tail of allGroups.
    for (int i = 0; i < newGroups.size(); ++i) {
        newGroups[i].darks = allGroups[m_groups.size() + i].darks;
        newGroups[i].flats = allGroups[m_groups.size() + i].flats;
        newGroups[i].bias  = allGroups[m_groups.size() + i].bias;
    }

    m_groups << newGroups;
    rebuildRows();
    updateStatusBar();

    // ── End debug session and show result dialog ──────────────────────────
    // Must come after rebuildRows() so that grouping decisions are captured
    // in the log before the session is closed.
    if (dbg.isSessionActive()) {
        dbg.endSession();
        DebugResultDialog resultDlg(dbg.humanLogPath(), dbg.jsonLogPath(), this);
        resultDlg.exec();
    }
}

void MainWindow::onRemoveLog()
{
    auto selected = m_logFileList->selectedItems();
    if (selected.isEmpty()) return;

    QSet<QString> removedPaths;
    for (auto *item : selected) {
        removedPaths.insert(item->data(Qt::UserRole).toString());
        delete item;
    }

    m_groups.erase(
        std::remove_if(m_groups.begin(), m_groups.end(),
                       [&](const AcquisitionGroup &g) {
                           return removedPaths.contains(g.sourceLogFile);
                       }),
        m_groups.end());

    // Clear warned keys so warnings reappear if the same log is re-added.
    m_ambTempWarnedKeys.clear();

    // Clear master directory caches so the user is prompted again if they
    // remove logs and start fresh with a different directory structure.
    m_primaryMasterCache.clear();
    m_secondaryMasterCache.clear();

    rebuildRows();
    updateStatusBar();
}

void MainWindow::onExportCsv()
{
    if (m_model->rows().isEmpty()) {
        QMessageBox::information(this, tr("Nothing to Export"),
                                 tr("Please add a log file first."));
        return;
    }

    QString dir = AppSettings::instance().lastExportDirectory();
    if (dir.isEmpty() || !QDir(dir).exists())
        dir = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);

    QStringList targets = m_model->targetNames();

    if (targets.size() == 1) {
        QFileDialog dlg(this, tr("Save CSV"), dir,
                        tr("CSV Files (*.csv);;All Files (*)"));
        dlg.setAcceptMode(QFileDialog::AcceptSave);
        dlg.setDefaultSuffix(QStringLiteral("csv"));
        if (dlg.exec() != QDialog::Accepted) return;
        QString path = dlg.selectedFiles().first();

        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QMessageBox::critical(this, tr("Export Error"),
                                  tr("Cannot write to:\n%1").arg(path));
            return;
        }
        f.write(m_model->toCsv({}, AppSettings::instance().hiddenColumns()).toUtf8());
        AppSettings::instance().setLastExportDirectory(
            QFileInfo(path).absolutePath());
    } else {
        QFileDialog dlg(this, tr("Select Export Directory"), dir);
        dlg.setFileMode(QFileDialog::Directory);
        dlg.setOption(QFileDialog::ShowDirsOnly, true);
        if (dlg.exec() != QDialog::Accepted) return;
        QString outDir = dlg.selectedFiles().first();

        AppSettings::instance().setLastExportDirectory(outDir);
        int exported = 0;
        for (const QString &target : targets) {
            QString base = target;
            base.replace(QRegularExpression(R"([/\\:*?"<>|])"),
                         QStringLiteral("_"));

            QString fname = base + QStringLiteral(".csv");
            if (QFile::exists(QDir(outDir).filePath(fname))) {
                int n = 2;
                do {
                    fname = base
                            + QStringLiteral("(")
                            + QString::number(n)
                            + QStringLiteral(").csv");
                    ++n;
                } while (QFile::exists(QDir(outDir).filePath(fname)));
            }

            QFile f(QDir(outDir).filePath(fname));
            if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) continue;
            f.write(m_model->toCsv(target, AppSettings::instance().hiddenColumns()).toUtf8());
            ++exported;
        }
        QMessageBox::information(this, tr("Export Complete"),
                                 tr("Exported %1 CSV file(s) to:\n%2")
                                     .arg(exported).arg(outDir));
    }
}

void MainWindow::onCopyCsv()
{
    if (m_model->rows().isEmpty()) {
        QMessageBox::information(this, tr("Nothing to Copy"),
                                 tr("Please add a log file first."));
        return;
    }
    CopyCsvDialog dlg(m_model, AppSettings::instance().hiddenColumns(), this);
    dlg.exec();
}

void MainWindow::onGroupingChanged(int)
{
    AppSettings::instance().setGroupingStrategy(
        m_groupingCombo->currentIndex());
    rebuildRows();
}

void MainWindow::onManageLocations()
{
    ManageLocationsDialog dlg(this);
    dlg.exec();
    int prev = m_locationCombo->currentIndex();
    m_locationCombo->clear();
    m_locationCombo->addItem(tr("(none)"));
    for (const auto &loc : AppSettings::instance().locations())
        m_locationCombo->addItem(loc.name);
    m_locationCombo->setCurrentIndex(
        qBound(0, prev, m_locationCombo->count() - 1));
    rebuildRows();
}

void MainWindow::onManageFilters()
{
    ManageFiltersDialog dlg(this);
    dlg.exec();
    rebuildRows();
}

void MainWindow::onManageTargets()
{
    ManageTargetsDialog dlg(knownLogTargets(), this);
    if (dlg.exec() == QDialog::Accepted)
        rebuildRows();
}

void MainWindow::onAbout()   { AboutDialog dlg(this); dlg.exec(); }

void MainWindow::onToggleTheme()
{
    applyTheme(m_currentTheme == QStringLiteral("dark")
               ? QStringLiteral("light") : QStringLiteral("dark"));
}

QString MainWindow::promptForMasterDirectory(const QString &missingPath,
                                              const QString &startDir,
                                              const QString &errorMessage)
{
    QMessageBox hint(this);
    hint.setWindowTitle(tr("Locate Master Calibration Frame"));
    hint.setIcon(QMessageBox::Information);

    if (!errorMessage.isEmpty())
        hint.setText(errorMessage);
    else
        hint.setText(tr("A master calibration frame could not be found at its "
                        "original path."));

    hint.setInformativeText(
        tr("Missing file: %1\n\n"
           "Please select the folder containing this master calibration file.\n\n"
           "Tip: use Shift+Cmd+G in the directory picker to type a path "
           "directly (e.g. /Volumes/…).")
            .arg(QFileInfo(missingPath).fileName()));
    hint.setDetailedText(tr("Full path:\n%1").arg(missingPath));
    hint.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);
    hint.setDefaultButton(QMessageBox::Ok);
    if (hint.exec() != QMessageBox::Ok)
        return {};

    QFileDialog dlg(this,
                    tr("Locate folder containing: %1")
                        .arg(QFileInfo(missingPath).fileName()),
                    startDir);
    dlg.setFileMode(QFileDialog::Directory);
    dlg.setOption(QFileDialog::ShowDirsOnly, true);
    if (dlg.exec() != QDialog::Accepted)
        return {};

    return dlg.selectedFiles().first();
}

QString MainWindow::promptForDirectory(const QString &missingPath,
                                       const QString &startDir,
                                       const QString &errorMessage)
{
    while (true) {
        QMessageBox hint(this);
        hint.setWindowTitle(tr("Locate Registered Frames Folder"));
        hint.setIcon(QMessageBox::Information);

        if (!errorMessage.isEmpty())
            hint.setText(errorMessage);
        else
            hint.setText(tr("A registered frame could not be found at its "
                            "original path."));

        hint.setInformativeText(
            tr("Missing file: %1\n\n"
               "Please select the 'registered' folder (or a subfolder) "
               "containing the registered .xisf files.\n\n"
               "Tip: use Shift+Cmd+G in the directory picker to type a "
               "path directly (e.g. /Volumes/…).")
                .arg(QFileInfo(missingPath).fileName()));
        hint.setDetailedText(tr("Full path:\n%1").arg(missingPath));
        hint.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);
        hint.setDefaultButton(QMessageBox::Ok);
        if (hint.exec() != QMessageBox::Ok)
            return {};

        QFileDialog dlg(this,
                        tr("Locate folder containing: %1")
                            .arg(QFileInfo(missingPath).fileName()),
                        startDir);
        dlg.setFileMode(QFileDialog::Directory);
        dlg.setOption(QFileDialog::ShowDirsOnly, true);
        if (dlg.exec() != QDialog::Accepted)
            return {};

        QString chosen = dlg.selectedFiles().first();

        if (QFileInfo(chosen).fileName().compare(
                QLatin1String("registered"),
                Qt::CaseInsensitive) != 0) {
            QMessageBox warn(this);
            warn.setWindowTitle(tr("Unexpected Folder Name"));
            warn.setIcon(QMessageBox::Warning);
            warn.setText(tr("The selected folder is not named 'registered'."));
            warn.setInformativeText(
                tr("PixInsight WBPP stores registered frames in a folder "
                   "called 'registered'. Selecting a higher-level folder "
                   "may cause a slow recursive search.\n\n"
                   "Selected: %1\n\nUse this folder anyway?").arg(chosen));
            warn.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
            warn.setDefaultButton(QMessageBox::No);
            if (warn.exec() != QMessageBox::Yes)
                continue;
        }
        return chosen;
    }
}

void MainWindow::resolveXisfHeaders(QList<AcquisitionGroup> &newGroups)
{
    int total = 0;
    for (const auto &g : newGroups) total += g.xisfPaths.size();
    if (total == 0) return;

    auto &dbg = DebugLogger::instance();
    if (dbg.isSessionActive()) {
        dbg.logSection(QStringLiteral("resolveXisfHeaders"));
        dbg.logResult(QStringLiteral("totalFrames"), QString::number(total));
    }

    m_cancelRequested.storeRelease(0);
    m_progressBar->setRange(0, total);
    m_progressBar->setValue(0);
    m_progressBar->setVisible(true);
    m_cancelBtn->setVisible(true);
    m_statusLabel->setText(tr("Reading .xisf headers…"));

    auto *thread = new QThread(this);
    auto *worker = new XisfResolveWorker;
    worker->groups     = &newGroups;
    worker->cancelFlag = &m_cancelRequested;
    worker->moveToThread(thread);

    connect(thread, &QThread::started,
            worker, &XisfResolveWorker::run);

    connect(worker, &XisfResolveWorker::progress,
            this, [this](int v) {
                m_progressBar->setValue(v);
            }, Qt::QueuedConnection);

    connect(worker, &XisfResolveWorker::requestDirectory,
            this, [this, worker](const QString &missingPath,
                           const QString &startDir) {
                // Retry loop — redisplay the dialog with an error message if
                // the chosen directory doesn't contain the missing file.
                QString errorMsg;
                QString chosenDir;
                while (true) {
                    chosenDir = promptForDirectory(missingPath, startDir, errorMsg);
                    if (chosenDir.isEmpty()) break;  // user cancelled

                    // Check whether the file actually exists somewhere under
                    // the chosen directory before accepting it.
                    const QString fileName = QFileInfo(missingPath).fileName();
                    const QString found =
                        XisfResolveWorker::findRecursive(chosenDir, fileName,
                                                         &m_cancelRequested);
                    if (!found.isEmpty()) break;  // good directory

                    errorMsg = tr("The selected directory did not contain the "
                                  "file \"%1\". Please try again.").arg(fileName);
                }

                if (DebugLogger::instance().isSessionActive()) {
                    if (chosenDir.isEmpty())
                        DebugLogger::instance().logDecision(
                            QStringLiteral("User cancelled directory prompt for: %1")
                                .arg(missingPath));
                    else
                        DebugLogger::instance().logDecision(
                            QStringLiteral("User supplied directory '%1' for: %2")
                                .arg(chosenDir, missingPath));
                }
                worker->supplyDirectory(chosenDir);
            }, Qt::QueuedConnection);

    QEventLoop loop;
    connect(worker, &XisfResolveWorker::finished, &loop, &QEventLoop::quit);
    connect(worker, &XisfResolveWorker::finished, worker, &QObject::deleteLater);
    connect(worker, &XisfResolveWorker::finished, thread, &QThread::quit);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);

    thread->start();
    loop.exec();

    m_progressBar->setVisible(false);
    m_cancelBtn->setVisible(false);
    m_cancelRequested.storeRelease(0);

    if (dbg.isSessionActive()) {
        int resolved = 0;
        for (const auto &g : newGroups)
            for (bool r : g.frameResolved)
                if (r) ++resolved;
        dbg.logResult(QStringLiteral("framesResolved"), QString::number(resolved));
        dbg.logResult(QStringLiteral("framesUnresolved"),
                      QString::number(total - resolved));
    }
}

// ---------------------------------------------------------------------------
// Derive the calibrated-file basename from a registered path.
// ---------------------------------------------------------------------------
static QString calibratedBasename(const QString &registeredPath)
{
    QString stem = QFileInfo(registeredPath).completeBaseName();
    int pos = stem.lastIndexOf(QLatin1String("_c"));
    while (pos != -1) {
        int after = pos + 2;
        if (after == stem.size() || stem[after] == QLatin1Char('_'))
            return stem.left(pos + 2) + QStringLiteral(".xisf");
        pos = stem.lastIndexOf(QLatin1String("_c"), pos - 1);
    }
    return {};
}

static QString siblingDir(const QString &logFilePath, const QString &name)
{
    QDir logDir = QFileInfo(logFilePath).absoluteDir();
    QDir parent = logDir;
    if (!parent.cdUp()) return {};
    QString candidate = parent.filePath(name);
    return QDir(candidate).exists() ? candidate : QString{};
}

static QString findUnder(const QString &root, const QString &fileName)
{
    if (root.isEmpty()) return {};
    return XisfResolveWorker::findRecursive(root, fileName, nullptr);
}

// ---------------------------------------------------------------------------
// resolveCalibrationBlocks
// ---------------------------------------------------------------------------
void MainWindow::resolveCalibrationBlocks(QList<AcquisitionGroup> &groups,
                                          const QStringList       &logFiles)
{
    auto &dbg = DebugLogger::instance();
    if (dbg.isSessionActive())
        dbg.logSection(QStringLiteral("MainWindow::resolveCalibrationBlocks"));

    CalibrationLogParser calParser;

    QList<CalibrationBlock> allBlocks;
    for (const QString &lf : logFiles)
        allBlocks << calParser.parse(lf);

    if (dbg.isSessionActive())
        dbg.logResult(QStringLiteral("totalLightCalBlocks"),
                      QString::number(allBlocks.size()));

    QHash<QString, QString> flatToBias;
    for (const QString &lf : logFiles) {
        const QList<FlatBlock> flatBlocks = calParser.parseFlatBlocks(lf);
        for (const FlatBlock &fb : flatBlocks) {
            if (!fb.masterFlatPath.isEmpty() && !fb.masterBiasPath.isEmpty()) {
                flatToBias.insert(fb.masterFlatPath.toLower(), fb.masterBiasPath);
                if (dbg.isSessionActive())
                    dbg.logResult(
                        QStringLiteral("flatToBias"),
                        QStringLiteral("%1 → %2")
                            .arg(fb.masterFlatPath, fb.masterBiasPath));
            }
        }
    }

    if (allBlocks.isEmpty()) {
        if (dbg.isSessionActive())
            dbg.logDecision(
                QStringLiteral("No calibration blocks found — skipping"));
        return;
    }

    // ── Detect master flats from external sessions ────────────────────────
    // Build a set of master flat basenames that are in flatToBias (i.e. were
    // produced and calibrated within this log's session).
    QSet<QString> knownFlatBasenames;
    for (const QString &flatPath : flatToBias.keys())
        knownFlatBasenames.insert(QFileInfo(flatPath).fileName().toLower());

    // Find any master flat referenced in a Light calibration block whose
    // basename is NOT in knownFlatBasenames — those came from a different
    // session and we cannot determine which master bias was used for them.
    QSet<QString> externalFlatBasenames;
    for (const CalibrationBlock &blk : allBlocks) {
        if (blk.masterFlatPath.isEmpty()) continue;
        const QString base = QFileInfo(blk.masterFlatPath).fileName().toLower();
        if (!knownFlatBasenames.contains(base))
            externalFlatBasenames.insert(
                QFileInfo(blk.masterFlatPath).fileName());  // preserve case for display
    }

    if (!externalFlatBasenames.isEmpty()) {
        if (dbg.isSessionActive()) {
            for (const QString &f : std::as_const(externalFlatBasenames))
                dbg.logWarning(
                    QStringLiteral("Master flat from external session — "
                                   "bias count unavailable: %1").arg(f));
        }
        QString msg = tr(
            "The following master flat file(s) were produced in a different "
            "PixInsight session and are not calibrated in this log file. "
            "The bias count for groups that use these flats cannot be "
            "determined automatically and will be left blank.\n\n"
            "To resolve this, load the log file from the session that "
            "produced these master flats.\n\n");
        for (const QString &f : std::as_const(externalFlatBasenames))
            msg += QStringLiteral("  \u2022 ") + f + QLatin1Char('\n');
        QMessageBox::information(this, tr("External Master Flats"), msg);
    }

    QHash<QString, QString> logToCalibratedDir;
    QHash<QString, QString> logToMasterDir;
    for (const QString &lf : logFiles) {
        logToCalibratedDir[lf] = siblingDir(lf, QStringLiteral("calibrated"));
        logToMasterDir[lf]     = siblingDir(lf, QStringLiteral("master"));
        if (dbg.isSessionActive()) {
            dbg.logResult(
                QStringLiteral("calibratedDir[%1]").arg(QFileInfo(lf).fileName()),
                logToCalibratedDir[lf].isEmpty()
                    ? QStringLiteral("(not found)") : logToCalibratedDir[lf]);
            dbg.logResult(
                QStringLiteral("masterDir[%1]").arg(QFileInfo(lf).fileName()),
                logToMasterDir[lf].isEmpty()
                    ? QStringLiteral("(not found)") : logToMasterDir[lf]);
        }
    }

    QHash<QString, int>     basenameToBlock;
    QHash<QString, QString> basenameToLogDir;
    for (int b = 0; b < allBlocks.size(); ++b) {
        for (const QString &cp : allBlocks[b].calibratedPaths) {
            const QString base = QFileInfo(cp).fileName().toLower();
            basenameToBlock.insert(base, b);
            basenameToLogDir.insert(base, QFileInfo(cp).absolutePath());
        }
    }
    if (dbg.isSessionActive())
        dbg.logResult(QStringLiteral("basenameToBlock entries"),
                      QString::number(basenameToBlock.size()));

    QSet<QString> calibratedDirCache;
    for (const QString &dir : std::as_const(basenameToLogDir))
        if (!dir.isEmpty()) calibratedDirCache.insert(dir);

    // ── Master file resolution caches ────────────────────────────────────
    // m_primaryMasterCache and m_secondaryMasterCache are member variables
    // that persist across Add Log calls so the user only needs to locate a
    // missing master directory once per app session.
    // skipMasterPrompts is local — it resets each import so cancelling on
    // one Add Log does not suppress prompts on the next.
    bool skipMasterPrompts = false;

    QHash<QString, int> masterCountCache;

    // Returns the frame count for a master .xisf file, searching in order:
    //   1. original path from the log
    //   2. ../master/ sibling of the log file
    //   3. primary cache (exact directories found previously)
    //   4. secondary cache (user-supplied directories, recursive)
    //   5. user prompt (unless the user has already cancelled once)
    auto cachedCount = [&](const QString &path,
                           const QString &logFile) -> int {
        if (path.isEmpty()) return -1;

        auto it = masterCountCache.find(path);
        if (it != masterCountCache.end()) return it.value();

        const QString fileName   = QFileInfo(path).fileName();
        const QString masterRoot = logToMasterDir.value(logFile);

        // Helper: try to read frame count from a concrete file path.
        auto tryRead = [&](const QString &p) -> std::optional<int> {
            if (p.isEmpty() || !QFile::exists(p)) return std::nullopt;
            return XisfMasterFrameReader::readFrameCount(p);
        };

        // ── Tier 1: original path ─────────────────────────────────────
        if (auto v = tryRead(path)) {
            if (dbg.isSessionActive())
                dbg.logResult(
                    QStringLiteral("frameCount(%1)").arg(fileName),
                    QString::number(v.value_or(-1)));
            masterCountCache.insert(path, v.value_or(-1));
            return v.value_or(-1);
        }

        // ── Tier 2: ../master/ sibling of the log file ────────────────
        {
            const QString found = findUnder(masterRoot, fileName);
            if (!found.isEmpty()) {
                const int val =
                    XisfMasterFrameReader::readFrameCount(found).value_or(-1);
                if (dbg.isSessionActive())
                    dbg.logDecision(
                        QStringLiteral("frameCount: '%1' not at original path; "
                                       "found at '%2', count=%3")
                            .arg(fileName, found).arg(val));
                m_primaryMasterCache.insert(QFileInfo(found).absolutePath());
                masterCountCache.insert(found, val);
                masterCountCache.insert(path, val);
                return val;
            }
        }

        // ── Tier 3: primary cache (exact directories) ─────────────────
        for (const QString &dir : std::as_const(m_primaryMasterCache)) {
            const QString candidate = QDir(dir).filePath(fileName);
            if (auto v = tryRead(candidate)) {
                if (dbg.isSessionActive())
                    dbg.logDecision(
                        QStringLiteral("frameCount: '%1' found in primary "
                                       "cache dir '%2', count=%3")
                            .arg(fileName, dir).arg(v.value_or(-1)));
                masterCountCache.insert(path, v.value_or(-1));
                return v.value_or(-1);
            }
        }

        // ── Tier 4: secondary cache (recursive search) ────────────────
        for (const QString &dir : std::as_const(m_secondaryMasterCache)) {
            const QString found = findUnder(dir, fileName);
            if (!found.isEmpty()) {
                const int val =
                    XisfMasterFrameReader::readFrameCount(found).value_or(-1);
                const QString foundDir = QFileInfo(found).absolutePath();
                if (dbg.isSessionActive())
                    dbg.logDecision(
                        QStringLiteral("frameCount: '%1' found via secondary "
                                       "cache in '%2', count=%3")
                            .arg(fileName, found).arg(val));
                m_primaryMasterCache.insert(foundDir);
                masterCountCache.insert(found, val);
                masterCountCache.insert(path, val);
                return val;
            }
        }

        // ── Tier 5: user prompt ───────────────────────────────────────
        if (!skipMasterPrompts) {
            if (dbg.isSessionActive())
                dbg.logDecision(
                    QStringLiteral("frameCount: '%1' not found — prompting "
                                   "user").arg(fileName));

            const QString startDir = masterRoot.isEmpty()
                ? QFileInfo(logFile).absolutePath()
                : masterRoot;

            QString errorMsg;  // empty on first attempt, set on retry
            while (true) {
                const QString supplied =
                    promptForMasterDirectory(path, startDir, errorMsg);

                if (supplied.isEmpty()) {
                    // User cancelled — skip all further prompts this import.
                    skipMasterPrompts = true;
                    if (dbg.isSessionActive())
                        dbg.logDecision(
                            QStringLiteral("User cancelled master directory "
                                           "prompt — suppressing further prompts"));
                    masterCountCache.insert(path, -1);
                    return -1;
                }

                // Search the supplied directory for the file.
                const QString found = findUnder(supplied, fileName);
                if (!found.isEmpty()) {
                    const int val =
                        XisfMasterFrameReader::readFrameCount(found).value_or(-1);
                    const QString foundDir = QFileInfo(found).absolutePath();
                    if (dbg.isSessionActive())
                        dbg.logDecision(
                            QStringLiteral("frameCount: '%1' found after user "
                                           "prompt in '%2', count=%3")
                                .arg(fileName, found).arg(val));
                    m_primaryMasterCache.insert(foundDir);
                    m_secondaryMasterCache.append(supplied);
                    masterCountCache.insert(found, val);
                    masterCountCache.insert(path, val);
                    return val;
                }

                // Not found — retry with error message at top of dialog.
                if (dbg.isSessionActive())
                    dbg.logWarning(
                        QStringLiteral("frameCount: '%1' not found in user-"
                                       "supplied dir '%2' — retrying")
                            .arg(fileName, supplied));
                errorMsg = tr("The selected directory did not contain the file "
                              "\"%1\". Please try again.")
                               .arg(fileName);
            }
        } else {
            if (dbg.isSessionActive())
                dbg.logDecision(
                    QStringLiteral("frameCount: '%1' not found — skipping "
                                   "prompt (user previously cancelled)")
                        .arg(fileName));
        }

        masterCountCache.insert(path, -1);
        return -1;
    };

    QStringList unmatched;
    for (auto &grp : groups) {
        // Skip only if all three calibration counts are already resolved.
        // A group with darks and flats but no bias should still be processed
        // so that loading a second log file can fill in the missing bias.
        if (grp.darks >= 0 && grp.flats >= 0 && grp.bias >= 0) continue;

        const QString &lf = grp.sourceLogFile;
        const QString label = grp.target.isEmpty()
            ? QFileInfo(grp.sourceLogFile).baseName() : grp.target;

        if (dbg.isSessionActive())
            dbg.logDecision(
                QStringLiteral("Matching group '%1 / %2' (%3 frames)")
                    .arg(label, grp.filter).arg(grp.xisfPaths.size()));

        bool matched = false;
        for (const QString &regPath : grp.xisfPaths) {
            QString calBase = calibratedBasename(regPath);
            if (calBase.isEmpty()) {
                if (dbg.isSessionActive())
                    dbg.logWarning(
                        QStringLiteral("  calibratedBasename: no '_c' suffix "
                                       "in '%1'")
                            .arg(QFileInfo(regPath).fileName()));
                continue;
            }

            if (dbg.isSessionActive())
                dbg.logDecision(
                    QStringLiteral("  looking for calibrated basename: %1")
                        .arg(calBase));

            auto it = basenameToBlock.find(calBase.toLower());

            if (it == basenameToBlock.end()) {
                if (dbg.isSessionActive())
                    dbg.logDecision(
                        QStringLiteral("  not in basenameToBlock — searching "
                                       "calibrated dirs"));

                QString foundPath;
                for (const QString &dir : std::as_const(calibratedDirCache)) {
                    QString candidate = QDir(dir).filePath(calBase);
                    if (QFile::exists(candidate)) {
                        foundPath = candidate;
                        if (dbg.isSessionActive())
                            dbg.logDecision(
                                QStringLiteral("  found in calibrated cache "
                                               "dir: %1").arg(dir));
                        break;
                    }
                }

                if (foundPath.isEmpty()) {
                    const QString calibRoot = logToCalibratedDir.value(lf);
                    foundPath = findUnder(calibRoot, calBase);
                    if (!foundPath.isEmpty()) {
                        calibratedDirCache.insert(
                            QFileInfo(foundPath).absolutePath());
                        if (dbg.isSessionActive())
                            dbg.logDecision(
                                QStringLiteral("  found by recursive search: "
                                               "%1").arg(foundPath));
                    } else {
                        if (dbg.isSessionActive())
                            dbg.logWarning(
                                QStringLiteral("  '%1' not found in calibrated "
                                               "dir or recursive search")
                                    .arg(calBase));
                    }
                }

                if (!foundPath.isEmpty()) {
                    const QString foundBase =
                        QFileInfo(foundPath).fileName().toLower();
                    it = basenameToBlock.find(foundBase);
                    if (it != basenameToBlock.end() && dbg.isSessionActive())
                        dbg.logDecision(
                            QStringLiteral("  matched to block %1 via found "
                                           "path").arg(it.value()));
                }
            } else {
                if (dbg.isSessionActive())
                    dbg.logDecision(
                        QStringLiteral("  matched to block %1 via "
                                       "basenameToBlock").arg(it.value()));
            }

            if (it == basenameToBlock.end()) continue;

            const CalibrationBlock &blk = allBlocks[it.value()];

            // Only resolve fields that haven't been populated yet, so that
            // loading a second log file can fill in fields (e.g. bias) that
            // were missing from the first without overwriting good values.
            if (grp.darks < 0)
                grp.darks = cachedCount(blk.masterDarkPath, lf);
            if (grp.flats < 0)
                grp.flats = cachedCount(blk.masterFlatPath, lf);

            if (grp.bias < 0) {
                if (!blk.masterFlatPath.isEmpty()) {
                    auto biasIt = flatToBias.find(blk.masterFlatPath.toLower());
                    if (biasIt != flatToBias.end()) {
                        grp.bias = cachedCount(biasIt.value(), lf);
                        if (dbg.isSessionActive())
                            dbg.logDecision(
                                QStringLiteral("  bias from flatToBias map: %1")
                                    .arg(grp.bias));
                    }
                }
                if (grp.bias < 0 && !blk.masterBiasPath.isEmpty()) {
                    grp.bias = cachedCount(blk.masterBiasPath, lf);
                    if (dbg.isSessionActive())
                        dbg.logDecision(
                            QStringLiteral("  bias from calibration block "
                                           "directly: %1").arg(grp.bias));
                }
            }

            if (dbg.isSessionActive()) {
                dbg.logResult(
                    QStringLiteral("'%1 / %2' darks").arg(label, grp.filter),
                    grp.darks < 0 ? QStringLiteral("(not found)")
                                  : QString::number(grp.darks));
                dbg.logResult(
                    QStringLiteral("'%1 / %2' flats").arg(label, grp.filter),
                    grp.flats < 0 ? QStringLiteral("(not found)")
                                  : QString::number(grp.flats));
                dbg.logResult(
                    QStringLiteral("'%1 / %2' bias").arg(label, grp.filter),
                    grp.bias < 0 ? QStringLiteral("(not found)")
                                 : QString::number(grp.bias));
            }

            matched = true;
            break;
        }

        if (!matched) {
            if (dbg.isSessionActive())
                dbg.logWarning(
                    QStringLiteral("No calibration block matched for "
                                   "'%1 / %2'").arg(label, grp.filter));
            unmatched << QStringLiteral("%1 / %2").arg(label, grp.filter);
        }
    }

    if (!unmatched.isEmpty()) {
        QString msg = tr("No calibration block was found for the following "
                         "integration group(s). The darks and flats columns "
                         "will be left blank for these groups.\n\n");
        for (const QString &u : std::as_const(unmatched))
            msg += QStringLiteral("  \u2022 ") + u + QLatin1Char('\n');
        QMessageBox::information(this, tr("Calibration Data"), msg);
    }
}

void MainWindow::rebuildRows()
{
    auto savedEdits = m_model->snapshotEdits();

    auto strategy = static_cast<GroupingStrategy>(
        m_groupingCombo->currentData().toInt());

    auto &dbg = DebugLogger::instance();
    if (dbg.isSessionActive()) {
        const QString stratName =
            strategy == ByDate         ? QStringLiteral("ByDate") :
            strategy == ByDateGainTemp ? QStringLiteral("ByDateGainTemp") :
                                         QStringLiteral("Collapsed");
        dbg.logSection(QStringLiteral("rebuildRows"));
        dbg.logResult(QStringLiteral("groupingStrategy"), stratName);
        dbg.logResult(QStringLiteral("inputGroups"), QString::number(m_groups.size()));
    }

    struct GroupKey {
        QString target;
        QString filter;
        bool operator<(const GroupKey &o) const {
            if (target != o.target) return target < o.target;
            return filter < o.filter;
        }
    };

    QMap<GroupKey, QList<const AcquisitionGroup *>> combined;
    for (const auto &grp : m_groups) {
        QString logTarget = grp.target.isEmpty()
        ? QFileInfo(grp.sourceLogFile).baseName()
        : grp.target;
        QString astrobinTarget =
            AppSettings::instance().astrobinTargetName(logTarget);
        combined[{astrobinTarget, grp.filter}].append(&grp);
    }

    QList<AcquisitionRow> allRows;
    for (auto it = combined.constBegin(); it != combined.constEnd(); ++it) {
        QList<const AcquisitionGroup *> groups = it.value();

        std::sort(groups.begin(), groups.end(),
                  [](const AcquisitionGroup *a, const AcquisitionGroup *b) {
                      return a->xisfPaths.size() > b->xisfPaths.size();
                  });

        QSet<QString> seenFileNames;
        QList<const AcquisitionGroup *> dedupedGroups;

        for (const AcquisitionGroup *grp : groups) {
            int newFrames = 0;
            for (const QString &path : grp->xisfPaths)
                if (!seenFileNames.contains(QFileInfo(path).fileName()))
                    ++newFrames;

            if (newFrames > 0) {
                dedupedGroups.append(grp);
                for (const QString &path : grp->xisfPaths)
                    seenFileNames.insert(QFileInfo(path).fileName());
            }
        }

        allRows << applyGrouping(dedupedGroups, it.key().target, strategy);
    }

    if (dbg.isSessionActive())
        dbg.logResult(QStringLiteral("rowsProduced"), QString::number(allRows.size()));

    // Warn about rows where only a subset of their contributing frames had
    // AMBTEMP.
    {
        struct GroupInfo {
            const AcquisitionGroup *grp;
            QString                 astrobinTarget;
        };
        QList<GroupInfo> groupInfos;
        for (const auto &grp : m_groups) {
            QString logTarget = grp.target.isEmpty()
            ? QFileInfo(grp.sourceLogFile).baseName()
            : grp.target;
            groupInfos.push_back({&grp,
                                  AppSettings::instance().astrobinTargetName(logTarget)});
        }

        QStringList partialLabels;

        for (const AcquisitionRow &row : allRows) {
            if (!row.hasTemperature) continue;

            int withTemp    = 0;
            int withoutTemp = 0;

            for (const GroupInfo &gi : groupInfos) {
                QString prefix = gi.astrobinTarget
                                 + QStringLiteral(" / ")
                                 + gi.grp->filter;
                if (!row.groupLabel.startsWith(prefix))
                    continue;

                for (int i = 0; i < gi.grp->xisfPaths.size(); ++i) {
                    if (!gi.grp->frameResolved[i]) continue;

                    if (strategy != Collapsed) {
                        if (!gi.grp->frameDates[i].isValid()) continue;
                        if (gi.grp->frameDates[i].toString(Qt::ISODate)
                            != row.date.toString(Qt::ISODate))
                            continue;
                    }

                    if (gi.grp->frameHasAmbTemp[i])
                        ++withTemp;
                    else
                        ++withoutTemp;
                }
            }

            if (withTemp > 0 && withoutTemp > 0) {
                const QString key = row.groupLabel
                                    + QLatin1Char('|')
                                    + QString::number(strategy);
                if (!m_ambTempWarnedKeys.contains(key))
                    partialLabels << row.groupLabel;
            }
        }

        if (!partialLabels.isEmpty()) {
            for (const QString &lbl : std::as_const(partialLabels)) {
                const QString key = lbl
                                    + QLatin1Char('|')
                                    + QString::number(strategy);
                m_ambTempWarnedKeys.insert(key);
            }

            QString msg = tr(
                "Not all files in the following groups contained an ambient "
                "temperature (AMBTEMP keyword). The temperature was calculated "
                "using only those files that contain the AMBTEMP keyword:\n\n");
            for (const QString &lbl : std::as_const(partialLabels))
                msg += QStringLiteral("  \u2022 ") + lbl + QLatin1Char('\n');
            QMessageBox::information(this, tr("Partial Temperature Data"), msg);
        }
    }

    applyLocationToRows(allRows);
    m_model->setRows(allRows);
    m_model->applyEdits(savedEdits);

    m_tableView->horizontalHeader()->setSortIndicator(-1, Qt::AscendingOrder);
    m_proxyModel->invalidate();
    m_tableView->resizeColumnsToContents();
    m_tableView->restoreColumnVisibility();
    updateStatusBar();
    m_summaryEdit->setPlainText(m_model->integrationSummary());
}

QList<AcquisitionRow> MainWindow::applyGrouping(
    const QList<const AcquisitionGroup *> &groups,
    const QString &target,
    GroupingStrategy strategy) const
{
    Q_ASSERT(!groups.isEmpty());
    const AcquisitionGroup &first = *groups.first();

    int astrobinId = AppSettings::instance().astrobinFilterId(first.filter);

    auto &dbg = DebugLogger::instance();
    const bool logging = dbg.isSessionActive();

    const QString stratName =
        strategy == ByDate         ? QStringLiteral("ByDate") :
        strategy == ByDateGainTemp ? QStringLiteral("ByDateGainTemp") :
                                     QStringLiteral("Collapsed");

    if (logging) {
        int totalFrames = 0;
        for (const auto *g : groups) totalFrames += g->xisfPaths.size();
        dbg.logDecision(
            QStringLiteral("applyGrouping: target='%1' filter='%2' "
                           "strategy=%3 sourceGroups=%4 totalFrames=%5")
                .arg(target, first.filter, stratName)
                .arg(groups.size()).arg(totalFrames));
        if (astrobinId >= 0)
            dbg.logResult(QStringLiteral("  astrobinFilterId"),
                          QString::number(astrobinId));
        else
            dbg.logWarning(QStringLiteral("  filter '%1' has no Astrobin ID mapping")
                               .arg(first.filter));
    }

    auto makeBase = [&]() -> AcquisitionRow {
        AcquisitionRow r;
        r.duration         = std::round(first.exposureSec);
        r.hasBinning       = true;
        r.binning          = first.binning;
        r.filterAstrobinId = astrobinId;
        r.hasFilter        = true;
        if (first.darks >= 0) { r.darks = first.darks; r.hasDarks = true; }
        if (first.flats >= 0) { r.flats = first.flats; r.hasFlats = true; }
        if (first.bias  >= 0) { r.bias  = first.bias;  r.hasBias  = true; }
        return r;
    };

    QString groupPrefix = target + QStringLiteral(" / ") + first.filter;

    auto bucketAmbTemp = [](const QList<QPair<const AcquisitionGroup *, int>> &frames,
                            double &outTemp) -> bool {
        double sum   = 0.0;
        int    count = 0;
        for (const auto &[grp, idx] : frames) {
            if (grp->frameResolved[idx] && grp->frameHasAmbTemp[idx]) {
                sum += grp->frameAmbTemps[idx];
                ++count;
            }
        }
        if (count == 0) return false;
        outTemp = sum / count;
        return true;
    };

    if (strategy == Collapsed) {
        AcquisitionRow r = makeBase();
        QDate earliest;

        QList<QPair<const AcquisitionGroup *, int>> allFrames;
        for (const auto *grp : groups) {
            r.number += grp->xisfPaths.size();
            for (int i = 0; i < grp->xisfPaths.size(); ++i) {
                allFrames.append({grp, i});
                if (grp->frameResolved[i] &&
                    (!earliest.isValid() || grp->frameDates[i] < earliest))
                    earliest = grp->frameDates[i];
            }
        }
        if (earliest.isValid()) { r.date = earliest; r.hasDate = true; }

        double t = 0.0;
        if (bucketAmbTemp(allFrames, t)) {
            r.temperature    = t;
            r.hasTemperature = true;
        }

        r.groupLabel = groupPrefix;

        if (logging)
            dbg.logDecision(
                QStringLiteral("  Collapsed → 1 row: label='%1' frames=%2 "
                               "earliestDate=%3 ambTemp=%4")
                    .arg(r.groupLabel)
                    .arg(r.number)
                    .arg(r.hasDate ? r.date.toString(Qt::ISODate)
                                   : QStringLiteral("(none)"))
                    .arg(r.hasTemperature
                             ? QString::number(r.temperature, 'f', 2)
                             : QStringLiteral("(none)")));

        return {r};
    }

    struct Key {
        QDate date;
        int   gain{-1};
        int   sensorTemp{0};
        bool operator<(const Key &o) const {
            if (date != o.date) return date < o.date;
            if (gain != o.gain) return gain < o.gain;
            return sensorTemp < o.sensorTemp;
        }
    };

    QMap<Key, QList<QPair<const AcquisitionGroup *, int>>> buckets;
    for (const auto *grp : groups) {
        for (int i = 0; i < grp->xisfPaths.size(); ++i) {
            Key k;
            if (grp->frameResolved[i]) {
                k.date       = grp->frameDates[i];
                k.gain       = grp->frameGains[i];
                k.sensorTemp = grp->frameHasSensorTemp[i]
                                   ? grp->frameSensorTemps[i] : 0;
            }
            if (strategy == ByDate) { k.gain = -1; k.sensorTemp = 0; }
            buckets[k].append({grp, i});
        }
    }

    if (logging)
        dbg.logDecision(
            QStringLiteral("  %1 → %2 bucket(s) (keyed by %3)")
                .arg(stratName)
                .arg(buckets.size())
                .arg(strategy == ByDate
                         ? QStringLiteral("date")
                         : QStringLiteral("date + gain + sensorTemp")));

    QList<AcquisitionRow> rows;
    for (auto it = buckets.constBegin(); it != buckets.constEnd(); ++it) {
        const Key &k = it.key();
        AcquisitionRow r = makeBase();
        r.number = it.value().size();
        if (k.date.isValid()) { r.date = k.date; r.hasDate = true; }

        for (const auto &[grp, idx] : it.value()) {
            if (grp->frameResolved[idx]) {
                if (grp->frameGains[idx] >= 0)
                { r.gain = grp->frameGains[idx]; r.hasGain = true; }
                if (grp->frameHasSensorTemp[idx]) {
                    r.sensorCooling    = grp->frameSensorTemps[idx];
                    r.hasSensorCooling = true;
                }
                break;
            }
        }

        double t = 0.0;
        if (bucketAmbTemp(it.value(), t)) {
            r.temperature    = t;
            r.hasTemperature = true;
        }

        QString dateStr = k.date.isValid()
                              ? k.date.toString(Qt::ISODate) : tr("unknown date");
        r.groupLabel = groupPrefix + QStringLiteral(" / ") + dateStr;

        if (logging)
            dbg.logDecision(
                QStringLiteral("  bucket → row: label='%1' frames=%2 "
                               "gain=%3 sensorTemp=%4 ambTemp=%5")
                    .arg(r.groupLabel)
                    .arg(r.number)
                    .arg(r.hasGain ? QString::number(r.gain)
                                   : QStringLiteral("(none)"))
                    .arg(r.hasSensorCooling ? QString::number(r.sensorCooling)
                                           : QStringLiteral("(none)"))
                    .arg(r.hasTemperature
                             ? QString::number(r.temperature, 'f', 2)
                             : QStringLiteral("(none)")));

        rows << r;
    }
    return rows;
}

void MainWindow::applyLocationToRows(QList<AcquisitionRow> &rows) const
{
    int locIdx = m_locationCombo->currentIndex() - 1;
    if (locIdx < 0) return;
    auto locs = AppSettings::instance().locations();
    if (locIdx >= locs.size()) return;
    const Location &loc = locs[locIdx];
    for (auto &r : rows) {
        if (loc.hasBortle)  { r.bortle  = loc.bortle;  r.hasBortle  = true; }
        if (loc.hasMeanSqm) { r.meanSqm = loc.meanSqm; r.hasMeanSqm = true; }
    }
}

QStringList MainWindow::knownLogTargets() const
{
    QStringList targets;
    QSet<QString> seen;
    for (const auto &grp : m_groups) {
        QString t = grp.target.isEmpty()
        ? QFileInfo(grp.sourceLogFile).baseName()
        : grp.target;
        if (!seen.contains(t)) {
            seen.insert(t);
            targets << t;
        }
    }
    std::sort(targets.begin(), targets.end(),
              [](const QString &a, const QString &b) {
                  return a.compare(b, Qt::CaseInsensitive) < 0;
              });
    return targets;
}

void MainWindow::updateStatusBar()
{
    m_statusLabel->setText(
        tr("%1 log file(s) loaded · %2 integration group(s) · %3 CSV row(s)")
            .arg(m_logFileList->count())
            .arg(m_groups.size())
            .arg(m_model->rowCount()));
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    AppSettings::instance().setWindowGeometry(saveGeometry());
    AppSettings::instance().setSplitterState(m_splitter->saveState());
    QMainWindow::closeEvent(e);
}

void MainWindow::keyPressEvent(QKeyEvent *e)
{
    if (e->modifiers() & Qt::ControlModifier) {
        switch (e->key()) {
        case Qt::Key_Plus:
        case Qt::Key_Equal:
            changeFontSize(+1);
            return;
        case Qt::Key_Minus:
            changeFontSize(-1);
            return;
        case Qt::Key_0:
            changeFontSize(0);   // reset to system default, saves -1
            return;
        default:
            break;
        }
    }
    QMainWindow::keyPressEvent(e);
}

void MainWindow::changeFontSize(int delta, bool save)
{
    // delta == 0 resets to the system default captured at startup.
    // A positive/negative delta adjusts from the current size.
    // When called with an absolute target size (e.g. from settings restore),
    // pass delta as the target and save=false.
    int newSize;
    if (delta == 0) {
        newSize = m_baseFontSize;
    } else if (!save) {
        // Called from constructor to restore a saved absolute size.
        newSize = delta;
    } else {
        newSize = QApplication::font().pointSize() + delta;
    }
    newSize = qBound(7, newSize, 24);

    QFont appFont = QApplication::font();
    appFont.setPointSize(newSize);
    QApplication::setFont(appFont);

    const auto widgets = QApplication::allWidgets();
    for (QWidget *w : widgets) {
        QFont wf = w->font();
        wf.setPointSize(newSize);
        w->setFont(wf);
        w->update();
    }

    if (m_summaryEdit) {
        QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        mono.setPointSize(newSize);
        m_summaryEdit->setFont(mono);
    }

    if (save) {
        // Reset (delta==0) saves -1 to indicate "use system default".
        AppSettings::instance().setFontSize(
            delta == 0 ? -1 : newSize);
    }
}
