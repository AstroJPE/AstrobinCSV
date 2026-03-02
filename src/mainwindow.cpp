#include "mainwindow.h"
#include "models/csvtablemodel.h"
#include "logparser/pixinsightlogparser.h"
#include "logparser/sirillogparser.h"
#include "logparser/logparserbase.h"
#include "xisfheaderreader.h"
#include "frameresolverworker.h"
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

// ── Styled splitter handle ────────────────────────────────────────────────
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
        const int dotD = 3, gap = 5, nDots = 7;
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

// ── Helpers ───────────────────────────────────────────────────────────────

static QString siblingDir(const QString &logFilePath, const QString &name)
{
    QDir logDir = QFileInfo(logFilePath).absoluteDir();
    QDir parent = logDir;
    if (!parent.cdUp()) return {};
    QString candidate = parent.filePath(name);
    return QDir(candidate).exists() ? candidate : QString{};
}

// ── MainWindow ────────────────────────────────────────────────────────────

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

    m_baseFontSize = QApplication::font().pointSize();
    const int savedFontSize = AppSettings::instance().fontSize();
    if (savedFontSize > 0 && savedFontSize != m_baseFontSize)
        changeFontSize(savedFontSize, false);

    const QByteArray geo = AppSettings::instance().windowGeometry();
    if (!geo.isEmpty()) restoreGeometry(geo);

    const QByteArray split = AppSettings::instance().splitterState();
    if (!split.isEmpty()) m_splitter->restoreState(split);

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
    tb->addAction(tr("Export CSV"),       this, &MainWindow::onExportCsv);
    tb->addAction(tr("Copy CSV"),         this, &MainWindow::onCopyCsv);
    tb->addSeparator();
    tb->addAction(tr("Manage Locations"), this, &MainWindow::onManageLocations);
    tb->addAction(tr("Manage Filters"),   this, &MainWindow::onManageFilters);
    tb->addAction(tr("Manage Targets"),   this, &MainWindow::onManageTargets);
}

void MainWindow::buildCentralWidget()
{
    auto *central = new QWidget(this);
    auto *vlay    = new QVBoxLayout(central);
    vlay->setContentsMargins(8, 8, 8, 8);
    vlay->setSpacing(6);

    // ── Log files panel ──────────────────────────────────────────────────
    auto *logBox  = new QGroupBox(tr("Loaded Log Files"));
    auto *logLay  = new QHBoxLayout(logBox);
    m_logFileList = new QListWidget;
    m_logFileList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    {
        auto *tmp = new QListWidgetItem(QStringLiteral("M"));
        m_logFileList->addItem(tmp);
        const int rowH   = m_logFileList->sizeHintForRow(0);
        const int frameH = m_logFileList->frameWidth() * 2;
        m_logFileList->setFixedHeight(rowH * 6 + frameH);
        delete m_logFileList->takeItem(0);
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

    // ── Control row ──────────────────────────────────────────────────────
    auto *ctrlRow = new QHBoxLayout;
    ctrlRow->addWidget(new QLabel(tr("Location:")));
    m_locationCombo = new QComboBox;
    m_locationCombo->setMinimumWidth(160);
    ctrlRow->addWidget(m_locationCombo);
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

    // ── Table view ───────────────────────────────────────────────────────
    m_tableView  = new AcquisitionTableView;
    m_proxyModel = new QSortFilterProxyModel(this);
    m_proxyModel->setSourceModel(m_model);
    m_proxyModel->setSortCaseSensitivity(Qt::CaseInsensitive);
    m_proxyModel->setSortRole(Qt::DisplayRole);
    m_tableView->setModel(m_proxyModel);
    m_tableView->setSortingEnabled(true);
    m_tableView->horizontalHeader()->setSortIndicatorShown(true);
    m_tableView->horizontalHeader()->setSortIndicator(-1, Qt::AscendingOrder);
    m_tableView->horizontalHeader()->setStretchLastSection(false);
    m_tableView->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_tableView->horizontalHeader()->setSectionResizeMode(
        CsvTableModel::ColGroup, QHeaderView::Stretch);
    connect(m_tableView->horizontalHeader(), &QHeaderView::sectionDoubleClicked,
            this, [this](int col) { m_tableView->resizeColumnToContents(col); });
    m_tableView->setAlternatingRowColors(true);
    m_tableView->setSelectionBehavior(QAbstractItemView::SelectItems);
    m_tableView->setEditTriggers(QAbstractItemView::DoubleClicked |
                                 QAbstractItemView::SelectedClicked);

    // ── Splitter ─────────────────────────────────────────────────────────
    m_splitter = new GripSplitter(Qt::Vertical);
    m_splitter->setChildrenCollapsible(false);

    auto *topPane = new QWidget;
    auto *topLay  = new QVBoxLayout(topPane);
    topLay->setContentsMargins(0, 0, 0, 0);
    topLay->setSpacing(2);
    topLay->addWidget(m_tableView, 1);
    auto *legendLabel = new QLabel(
        tr("[*] Display-only column — not included in exported CSV"));
    legendLabel->setStyleSheet(QStringLiteral("color: gray; font-size: 11px;"));
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
            "border:1px solid #666;padding:4px 8px;}");
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
    const int     fileCount = oldFiles.size();
    const QString logDir    = DebugLogger::debugLogDirectory();
    QMessageBox   msg(this);
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
        if (removed != fileCount)
            QMessageBox::warning(
                this, tr("Cleanup Incomplete"),
                tr("Deleted %1 of %2 file(s). Some files could not be "
                   "removed.\n\nLocation: %3")
                    .arg(removed).arg(fileCount).arg(logDir));
    }
}

void MainWindow::onToggleDebugLogging()
{
    const bool on = m_debugLogAction->isChecked();
    DebugLogger::instance().setEnabled(on);
    statusBar()->showMessage(
        on ? tr("Debug logging enabled — active on next import")
           : tr("Debug logging disabled"), 4000);
}

void MainWindow::onAddLog()
{
    QString dir = AppSettings::instance().lastOpenDirectory();
    if (dir.isEmpty() || !QDir(dir).exists())
        dir = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);

    QFileDialog dlg(this, tr("Add PixInsight / Siril Log File"), dir,
                    tr("Log Files (*.log);;All Files (*)"));
    dlg.setFileMode(QFileDialog::ExistingFiles);
    if (dlg.exec() != QDialog::Accepted) return;
    QStringList files = dlg.selectedFiles();
    if (files.isEmpty()) return;

    AppSettings::instance().setLastOpenDirectory(
        QFileInfo(files.first()).absolutePath());

    QSet<QString> loadedPaths;
    for (int i = 0; i < m_logFileList->count(); ++i)
        loadedPaths.insert(
            m_logFileList->item(i)->data(Qt::UserRole).toString());

    auto &dbg = DebugLogger::instance();
    if (dbg.isEnabled()) dbg.beginSession();

    // Reset skipPrompts at the start of each import so a previous cancel
    // doesn't suppress prompts in this session.
    m_masterCache.skipPrompts = false;

    PixInsightLogParser piParser;
    SirilLogParser      sirilParser;
    QList<IntegrationGroup> newGroups;

    for (const QString &path : files) {
        if (loadedPaths.contains(path)) continue;

        // Determine parser.
        QList<IntegrationGroup> parsed;
        if (piParser.canParse(path)) {
            parsed = piParser.parse(path);
            if (parsed.isEmpty()) {
                if (dbg.isSessionActive())
                    dbg.logError(
                        QStringLiteral("No groups in %1: %2")
                            .arg(path, piParser.errorString()));
                QMessageBox::warning(
                    this, tr("Parse Error"),
                    tr("No integration groups found in:\n%1\n\n%2")
                        .arg(path, piParser.errorString()));
                continue;
            }
        } else if (sirilParser.canParse(path)) {
            // Siril not yet implemented.
            QMessageBox::warning(
                this, tr("Parse Error"),
                tr("Siril log parsing is not yet implemented."));
            continue;
        } else {
            if (dbg.isSessionActive())
                dbg.logWarning(
                    QStringLiteral("Unknown log format: %1").arg(path));
            QMessageBox::warning(
                this, tr("Unknown Log Format"),
                tr("Could not identify the log format of:\n%1").arg(path));
            continue;
        }

        auto *item = new QListWidgetItem(QFileInfo(path).fileName());
        item->setData(Qt::UserRole, path);
        item->setToolTip(path);
        m_logFileList->addItem(item);
        newGroups << parsed;
    }

    if (newGroups.isEmpty()) {
        if (dbg.isSessionActive()) dbg.endSession();
        return;
    }

    QStringList allLogPaths;
    for (int i = 0; i < m_logFileList->count(); ++i)
        allLogPaths << m_logFileList->item(i)->data(Qt::UserRole).toString();

    resolveFrames(newGroups, allLogPaths);

    m_groups << newGroups;
    rebuildRows();
    updateStatusBar();

    if (dbg.isSessionActive()) {
        dbg.endSession();
        DebugResultDialog resultDlg(dbg.humanLogPath(),
                                    dbg.jsonLogPath(), this);
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
                       [&](const IntegrationGroup &g) {
                           return removedPaths.contains(g.sourceLogFile);
                       }),
        m_groups.end());

    m_ambTempWarnedKeys.clear();
    m_calConflictWarnedKeys.clear();
    m_masterCache.primaryDirs.clear();
    m_masterCache.secondaryDirs.clear();

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
        f.write(m_model->toCsv({},
            AppSettings::instance().hiddenColumns()).toUtf8());
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
                    fname = base + QStringLiteral("(")
                            + QString::number(n) + QStringLiteral(").csv");
                    ++n;
                } while (QFile::exists(QDir(outDir).filePath(fname)));
            }
            QFile f(QDir(outDir).filePath(fname));
            if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) continue;
            f.write(m_model->toCsv(target,
                AppSettings::instance().hiddenColumns()).toUtf8());
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
    CopyCsvDialog dlg(m_model,
                      AppSettings::instance().hiddenColumns(), this);
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
    hint.setText(errorMessage.isEmpty()
                     ? tr("A master calibration frame could not be found "
                          "at its original path.")
                     : errorMessage);
    hint.setInformativeText(
        tr("Missing file: %1\n\n"
           "Please select the folder containing this master calibration "
           "file.\n\nTip: use Shift+Cmd+G in the directory picker to type "
           "a path directly (e.g. /Volumes/…).")
            .arg(QFileInfo(missingPath).fileName()));
    hint.setDetailedText(tr("Full path:\n%1").arg(missingPath));
    hint.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);
    hint.setDefaultButton(QMessageBox::Ok);
    if (hint.exec() != QMessageBox::Ok) return {};

    QFileDialog dlg(this,
                    tr("Locate folder containing: %1")
                        .arg(QFileInfo(missingPath).fileName()),
                    startDir);
    dlg.setFileMode(QFileDialog::Directory);
    dlg.setOption(QFileDialog::ShowDirsOnly, true);
    if (dlg.exec() != QDialog::Accepted) return {};
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
        hint.setText(errorMessage.isEmpty()
                         ? tr("A registered frame could not be found at "
                              "its original path.")
                         : errorMessage);
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
        if (hint.exec() != QMessageBox::Ok) return {};

        QFileDialog dlg(this,
                        tr("Locate folder containing: %1")
                            .arg(QFileInfo(missingPath).fileName()),
                        startDir);
        dlg.setFileMode(QFileDialog::Directory);
        dlg.setOption(QFileDialog::ShowDirsOnly, true);
        if (dlg.exec() != QDialog::Accepted) return {};
        QString chosen = dlg.selectedFiles().first();

        if (QFileInfo(chosen).fileName().compare(
                QLatin1String("registered"),
                Qt::CaseInsensitive) != 0) {
            QMessageBox warn(this);
            warn.setWindowTitle(tr("Unexpected Folder Name"));
            warn.setIcon(QMessageBox::Warning);
            warn.setText(
                tr("The selected folder is not named 'registered'."));
            warn.setInformativeText(
                tr("PixInsight WBPP stores registered frames in a folder "
                   "called 'registered'. Selecting a higher-level folder "
                   "may cause a slow recursive search.\n\n"
                   "Selected: %1\n\nUse this folder anyway?").arg(chosen));
            warn.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
            warn.setDefaultButton(QMessageBox::No);
            if (warn.exec() != QMessageBox::Yes) continue;
        }
        return chosen;
    }
}

// ── Frame resolution pipeline ─────────────────────────────────────────────

void MainWindow::resolveFrames(QList<IntegrationGroup> &newGroups,
                                const QStringList       &allLogFiles)
{
    auto &dbg = DebugLogger::instance();

    // Build calibration lookup structures from all loaded log files.
    CalibrationLogParser calParser;
    QList<CalibrationBlock> allBlocks;
    for (const QString &lf : allLogFiles)
        allBlocks << calParser.parse(lf);

    QHash<QString, QString> flatToBias;
    for (const QString &lf : allLogFiles) {
        for (const FlatBlock &fb : calParser.parseFlatBlocks(lf)) {
            if (!fb.masterFlatPath.isEmpty() && !fb.masterBiasPath.isEmpty())
                flatToBias.insert(fb.masterFlatPath.toLower(),
                                  fb.masterBiasPath);
        }
    }

    // Detect external flats (produced in a different session).
    QSet<QString> knownFlatBasenames;
    for (const QString &flatPath : flatToBias.keys())
        knownFlatBasenames.insert(
            QFileInfo(flatPath).fileName().toLower());

    QSet<QString> externalFlatBasenames;
    for (const CalibrationBlock &blk : allBlocks) {
        if (blk.masterFlatPath.isEmpty()) continue;
        const QString base =
            QFileInfo(blk.masterFlatPath).fileName().toLower();
        if (!knownFlatBasenames.contains(base))
            externalFlatBasenames.insert(
                QFileInfo(blk.masterFlatPath).fileName());
    }
    if (!externalFlatBasenames.isEmpty()) {
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

    // Build basenameToBlock index.
    QHash<QString, int> basenameToBlock;
    for (int b = 0; b < allBlocks.size(); ++b)
        for (const QString &cp : allBlocks[b].calibratedPaths)
            basenameToBlock.insert(
                QFileInfo(cp).fileName().toLower(), b);

    // Build sibling directory maps.
    QHash<QString, QString> logToMasterDir, logToCalibratedDir;
    for (const QString &lf : allLogFiles) {
        logToMasterDir[lf]     = siblingDir(lf, QStringLiteral("master"));
        logToCalibratedDir[lf] = siblingDir(lf, QStringLiteral("calibrated"));
    }

    // Count total frames for progress bar.
    int total = 0;
    for (const auto &g : newGroups) total += g.frames.size();
    if (total == 0) return;

    m_cancelRequested.storeRelease(0);
    m_progressBar->setRange(0, total);
    m_progressBar->setValue(0);
    m_progressBar->setVisible(true);
    m_cancelBtn->setVisible(true);
    m_statusLabel->setText(tr("Reading .xisf headers…"));

    auto *thread = new QThread(this);
    auto *worker = new FrameResolveWorker;
    worker->groups              = &newGroups;
    worker->cancelFlag          = &m_cancelRequested;
    worker->masterCache         = &m_masterCache;
    worker->basenameToBlock     = basenameToBlock;
    worker->calBlocks           = allBlocks;
    worker->flatToBias          = flatToBias;
    worker->logToMasterDir      = logToMasterDir;
    worker->logToCalibratedDir  = logToCalibratedDir;
    worker->moveToThread(thread);

    connect(thread, &QThread::started, worker, &FrameResolveWorker::run);

    connect(worker, &FrameResolveWorker::progress,
            this, [this](int done, int total) {
                m_progressBar->setValue(done);
                Q_UNUSED(total)
            }, Qt::QueuedConnection);

    // Registered frame directory prompt — retry loop.
    connect(worker, &FrameResolveWorker::requestRegisteredDirectory,
            this, [this, worker](const QString &missingPath,
                                  const QString &startDir) {
                QString errorMsg;
                QString chosenDir;
                while (true) {
                    chosenDir = promptForDirectory(missingPath,
                                                   startDir, errorMsg);
                    if (chosenDir.isEmpty()) break;
                    const QString fn = QFileInfo(missingPath).fileName();
                    const QString found =
                        FrameResolveWorker::findRecursive(
                            chosenDir, fn, &m_cancelRequested);
                    if (!found.isEmpty()) break;
                    errorMsg = tr("The selected directory did not contain "
                                  "the file \"%1\". Please try again.")
                                   .arg(fn);
                }
                if (DebugLogger::instance().isSessionActive()) {
                    DebugLogger::instance().logDecision(
                        chosenDir.isEmpty()
                            ? QStringLiteral("User cancelled registered "
                                             "frame prompt for: %1")
                                  .arg(missingPath)
                            : QStringLiteral("User supplied dir '%1' for: %2")
                                  .arg(chosenDir, missingPath));
                }
                worker->supplyDirectory(chosenDir);
            }, Qt::QueuedConnection);

    // Master calibration file directory prompt — retry loop.
    connect(worker, &FrameResolveWorker::requestMasterDirectory,
            this, [this, worker](const QString &missingPath,
                                  const QString &startDir) {
                QString errorMsg;
                QString chosenDir;
                while (true) {
                    chosenDir = promptForMasterDirectory(missingPath,
                                                         startDir, errorMsg);
                    if (chosenDir.isEmpty()) break;
                    const QString fn = QFileInfo(missingPath).fileName();
                    const QString found =
                        FrameResolveWorker::findRecursive(
                            chosenDir, fn, &m_cancelRequested);
                    if (!found.isEmpty()) break;
                    errorMsg = tr("The selected directory did not contain "
                                  "the file \"%1\". Please try again.")
                                   .arg(fn);
                }
                if (DebugLogger::instance().isSessionActive()) {
                    DebugLogger::instance().logDecision(
                        chosenDir.isEmpty()
                            ? QStringLiteral("User cancelled master prompt "
                                             "for: %1").arg(missingPath)
                            : QStringLiteral("User supplied master dir "
                                             "'%1' for: %2")
                                  .arg(chosenDir, missingPath));
                }
                worker->supplyDirectory(chosenDir);
            }, Qt::QueuedConnection);

    QEventLoop loop;
    connect(worker, &FrameResolveWorker::finished,
            &loop, &QEventLoop::quit);
    connect(worker, &FrameResolveWorker::finished,
            worker, &QObject::deleteLater);
    connect(worker, &FrameResolveWorker::finished,
            thread, &QThread::quit);
    connect(thread, &QThread::finished,
            thread, &QObject::deleteLater);

    thread->start();
    loop.exec();

    m_progressBar->setVisible(false);
    m_cancelBtn->setVisible(false);
    m_cancelRequested.storeRelease(0);

    // ── Back-fill missing calibration data on already-loaded frames ───────
    // For each frame in m_groups whose calibration is incomplete, look up
    // its _c.xisf basename in the combined basenameToBlock index built from
    // all loaded logs. If found, the frame was calibrated in one of the
    // loaded sessions and we can resolve the missing counts. This allows
    // loading a supplementary log to populate bias/dark/flat values that
    // were unknown when the original log was first imported.
    //
    // Master file reads use m_masterCache which was populated by the worker,
    // so most lookups will be cache hits with no further I/O needed.
    if (!m_groups.isEmpty()) {
        // Build a simple cachedCount lambda for main-thread use.
        QHash<QString, int> mainCountCache;
        auto mainCachedCount = [&](const QString &path,
                                    const QString &logFile) -> int {
            if (path.isEmpty()) return -1;
            auto it = mainCountCache.find(path);
            if (it != mainCountCache.end()) return it.value();

            const QString fileName   = QFileInfo(path).fileName();
            const QString masterRoot = logToMasterDir.value(logFile);

            auto tryRead = [](const QString &p) -> std::optional<int> {
                if (p.isEmpty() || !QFile::exists(p)) return std::nullopt;
                return XisfMasterFrameReader::readFrameCount(p);
            };

            // Tier 1: original path.
            if (auto v = tryRead(path)) {
                mainCountCache.insert(path, v.value_or(-1));
                return v.value_or(-1);
            }

            // Tier 2: ../master/ sibling.
            {
                const QString found =
                    FrameResolveWorker::findRecursive(
                        masterRoot, fileName, nullptr);
                if (!found.isEmpty()) {
                    int val =
                        XisfMasterFrameReader::readFrameCount(found)
                            .value_or(-1);
                    mainCountCache.insert(found, val);
                    mainCountCache.insert(path, val);
                    return val;
                }
            }

            // Tier 3: primary cache.
            for (const QString &dir :
                     std::as_const(m_masterCache.primaryDirs)) {
                QString candidate = QDir(dir).filePath(fileName);
                if (auto v = tryRead(candidate)) {
                    mainCountCache.insert(path, v.value_or(-1));
                    return v.value_or(-1);
                }
            }

            // Tier 4: secondary cache (recursive).
            for (const QString &dir :
                     std::as_const(m_masterCache.secondaryDirs)) {
                QString found =
                    FrameResolveWorker::findRecursive(
                        dir, fileName, nullptr);
                if (!found.isEmpty()) {
                    int val =
                        XisfMasterFrameReader::readFrameCount(found)
                            .value_or(-1);
                    m_masterCache.primaryDirs.insert(
                        QFileInfo(found).absolutePath());
                    mainCountCache.insert(found, val);
                    mainCountCache.insert(path, val);
                    return val;
                }
            }

            mainCountCache.insert(path, -1);
            return -1;
        };

        for (auto &grp : m_groups) {
            for (auto &frame : grp.frames) {
                if (!frame.resolved) continue;

                // Only back-fill if at least one calibration field is missing.
                const bool needsDarks = (frame.calibration.darks < 0);
                const bool needsFlats = (frame.calibration.flats < 0);
                const bool needsBias  = (frame.calibration.bias  < 0);
                if (!needsDarks && !needsFlats && !needsBias) continue;

                // Derive calibrated basename from this frame's registered path.
                const QString cb =
                    FrameResolveWorker::calibratedBasenameStatic(
                        frame.registeredPath);
                if (cb.isEmpty()) continue;

                auto it = basenameToBlock.find(cb.toLower());
                if (it == basenameToBlock.end()) continue;

                const CalibrationBlock &blk = allBlocks[it.value()];

                if (needsDarks)
                    frame.calibration.darks =
                        mainCachedCount(blk.masterDarkPath,
                                        grp.sourceLogFile);
                if (needsFlats)
                    frame.calibration.flats =
                        mainCachedCount(blk.masterFlatPath,
                                        grp.sourceLogFile);
                if (needsBias) {
                    // Try flatToBias chain first.
                    if (!blk.masterFlatPath.isEmpty()) {
                        auto biasIt =
                            flatToBias.find(blk.masterFlatPath.toLower());
                        if (biasIt != flatToBias.end()) {
                            frame.calibration.masterBiasPath =
                                biasIt.value();
                            frame.calibration.bias =
                                mainCachedCount(biasIt.value(),
                                                grp.sourceLogFile);
                        }
                    }
                    // Fall back to direct bias path in calibration block.
                    if (frame.calibration.bias < 0 &&
                            !blk.masterBiasPath.isEmpty()) {
                        frame.calibration.bias =
                            mainCachedCount(blk.masterBiasPath,
                                            grp.sourceLogFile);
                    }
                }
            }
        }
    }
}

// ── Row building ──────────────────────────────────────────────────────────

void MainWindow::rebuildRows()
{
    auto savedEdits = m_model->snapshotEdits();
    auto strategy   = static_cast<GroupingStrategy>(
        m_groupingCombo->currentData().toInt());

    auto &dbg = DebugLogger::instance();
    if (dbg.isSessionActive()) {
        const QString sn =
            strategy == ByDate         ? QStringLiteral("ByDate") :
            strategy == ByDateGainTemp ? QStringLiteral("ByDateGainTemp") :
                                         QStringLiteral("Collapsed");
        dbg.logSection(QStringLiteral("rebuildRows"));
        dbg.logResult(QStringLiteral("groupingStrategy"), sn);
        dbg.logResult(QStringLiteral("inputGroups"),
                      QString::number(m_groups.size()));
    }

    // ── Collect all frames, keyed by astrobinTarget + filter ─────────────
    struct FrameKey {
        QString target;
        QString filter;
        bool operator<(const FrameKey &o) const {
            if (target != o.target) return target < o.target;
            return filter < o.filter;
        }
    };

    // Map from FrameKey to the IntegrationGroups whose frames belong there,
    // preserving per-group calibration data.
    QMap<FrameKey, QList<const IntegrationGroup *>> combined;

    for (const auto &grp : m_groups) {
        // Determine the astrobin target name for this group.
        // Prefer the log-level target (from WBPP grouping keyword) if set.
        // Otherwise use the most common target name across resolved frames.
        // Fall back to the log filename if no frames have a target.
        QString logTarget = grp.logTarget;
        if (logTarget.isEmpty()) {
            QMap<QString, int> targetCounts;
            for (const auto &f : grp.frames)
                if (!f.logTarget.isEmpty())
                    targetCounts[f.logTarget]++;
            if (!targetCounts.isEmpty()) {
                // Pick the most frequently occurring target name.
                auto it = std::max_element(
                    targetCounts.constBegin(), targetCounts.constEnd(),
                    [](const auto &a, const auto &b) {
                        return a < b;
                    });
                logTarget = it.key();
            }
        }
        if (logTarget.isEmpty())
            logTarget = QFileInfo(grp.sourceLogFile).baseName();

        const QString astrobinTarget =
            AppSettings::instance().astrobinTargetName(logTarget);

        // Use the filter from the first resolved frame.
        QString filter = grp.frames.isEmpty() ? QString()
                         : grp.frames.first().filter;

        combined[{astrobinTarget, filter}].append(&grp);
    }

    // ── Build rows ───────────────────────────────────────────────────────
    QList<AcquisitionRow> allRows;
    QStringList calConflictLabels;
    QStringList partialAmbTempLabels;

    for (auto it = combined.constBegin(); it != combined.constEnd(); ++it) {
        const QString &astrobinTarget = it.key().target;
        const QString &filter         = it.key().filter;
        const QString  groupPrefix    =
            astrobinTarget + QStringLiteral(" / ") + filter;

        // Collect all frames from all groups in this target/filter.
        // Deduplicate by filename across groups.
        QSet<QString>         seenNames;
        QList<const AcquisitionFrame *> allFrames;
        for (const auto *grp : it.value()) {
            for (const auto &frame : grp->frames) {
                const QString fn =
                    QFileInfo(frame.registeredPath).fileName();
                if (!seenNames.contains(fn)) {
                    seenNames.insert(fn);
                    allFrames.append(&frame);
                }
            }
        }

        // Bucket frames by grouping strategy key.
        struct BucketKey {
            QDate date;
            int   gain{-1};
            int   sensorTemp{0};
            bool operator<(const BucketKey &o) const {
                if (date != o.date) return date < o.date;
                if (gain != o.gain) return gain < o.gain;
                return sensorTemp < o.sensorTemp;
            }
        };

        QMap<BucketKey, QList<const AcquisitionFrame *>> buckets;
        for (const auto *fp : allFrames) {
            BucketKey k;
            if (fp->resolved) {
                k.date       = fp->date;
                k.gain       = fp->gain;
                k.sensorTemp = fp->hasSensorTemp ? fp->sensorTemp : 0;
            }
            if (strategy == ByDate || strategy == Collapsed) {
                k.gain       = -1;
                k.sensorTemp = 0;
            }
            buckets[k].append(fp);
        }

        // Build one AcquisitionRow per bucket.
        QList<AcquisitionRow> groupRows;
        for (auto bit = buckets.constBegin();
             bit != buckets.constEnd(); ++bit) {
            const BucketKey &k        = bit.key();
            const auto      &bFrames  = bit.value();

            AcquisitionRow r;
            r.number   = bFrames.size();
            r.hasFilter = true;
            r.filterAstrobinId =
                AppSettings::instance().astrobinFilterId(filter);

            if (k.date.isValid()) { r.date = k.date; r.hasDate = true; }

            // Take gain / sensorTemp from the first resolved frame.
            for (const auto *fp : bFrames) {
                if (!fp->resolved) continue;
                if (fp->gain >= 0) { r.gain = fp->gain; r.hasGain = true; }
                if (fp->hasSensorTemp) {
                    r.sensorCooling    = fp->sensorTemp;
                    r.hasSensorCooling = true;
                }
                r.duration    = std::round(fp->exposureSec);
                r.hasBinning  = true;
                r.binning     = fp->binning;
                break;
            }

            // Calibration counts: use the first frame's values; warn if
            // frames in this bucket disagree.
            int darks = -1, flats = -1, bias = -1;
            bool firstCal = true;
            bool calConflict = false;
            for (const auto *fp : bFrames) {
                if (!fp->resolved) continue;
                const auto &cal = fp->calibration;
                if (firstCal) {
                    darks    = cal.darks;
                    flats    = cal.flats;
                    bias     = cal.bias;
                    firstCal = false;
                } else {
                    if (cal.darks != darks || cal.flats != flats
                            || cal.bias != bias)
                        calConflict = true;
                }
            }

            if (darks >= 0) { r.darks = darks; r.hasDarks = true; }
            if (flats >= 0) { r.flats = flats; r.hasFlats = true; }
            if (bias  >= 0) { r.bias  = bias;  r.hasBias  = true; }

            // Build group label.
            QString dateStr = k.date.isValid()
                ? k.date.toString(Qt::ISODate) : tr("unknown date");
            r.groupLabel = (strategy == Collapsed)
                ? groupPrefix
                : groupPrefix + QStringLiteral(" / ") + dateStr;

            // Ambient temperature: average of frames with AMBTEMP.
            double ambSum   = 0.0;
            int    ambCount = 0;
            int    noAmbCount = 0;
            for (const auto *fp : bFrames) {
                if (!fp->resolved) continue;
                if (fp->hasAmbTemp) { ambSum += fp->ambTemp; ++ambCount; }
                else                { ++noAmbCount; }
            }
            if (ambCount > 0) {
                r.temperature    = ambSum / ambCount;
                r.hasTemperature = true;
            }

            // Track calibration conflicts and partial AMBTEMP.
            if (calConflict) {
                const QString key = r.groupLabel + QLatin1Char('|')
                                    + QString::number(strategy);
                if (!m_calConflictWarnedKeys.contains(key))
                    calConflictLabels << r.groupLabel;
            }
            if (ambCount > 0 && noAmbCount > 0) {
                const QString key = r.groupLabel + QLatin1Char('|')
                                    + QString::number(strategy);
                if (!m_ambTempWarnedKeys.contains(key))
                    partialAmbTempLabels << r.groupLabel;
            }

            groupRows << r;
        }

        // Collapsed: merge all buckets into one row.
        if (strategy == Collapsed && groupRows.size() > 1) {
            AcquisitionRow cr = groupRows.first();
            for (int ri = 1; ri < groupRows.size(); ++ri) {
                const AcquisitionRow &r = groupRows[ri];
                int prev = cr.number;
                cr.number += r.number;
                if (r.hasDate && (!cr.hasDate || r.date < cr.date)) {
                    cr.date    = r.date;
                    cr.hasDate = true;
                }
                if (r.hasTemperature) {
                    if (!cr.hasTemperature) {
                        cr.temperature    = r.temperature;
                        cr.hasTemperature = true;
                    } else {
                        cr.temperature =
                            (cr.temperature * prev
                             + r.temperature * r.number) / cr.number;
                    }
                }
                if (cr.darks < 0 && r.hasDarks)
                    { cr.darks = r.darks; cr.hasDarks = true; }
                if (cr.flats < 0 && r.hasFlats)
                    { cr.flats = r.flats; cr.hasFlats = true; }
                if (cr.bias  < 0 && r.hasBias)
                    { cr.bias  = r.bias;  cr.hasBias  = true; }
            }
            groupRows = {cr};
        }

        allRows << groupRows;
    }

    // ── Emit calibration conflict warning (once per label+strategy) ───────
    if (!calConflictLabels.isEmpty()) {
        for (const QString &lbl : std::as_const(calConflictLabels))
            m_calConflictWarnedKeys.insert(
                lbl + QLatin1Char('|') + QString::number(strategy));
        QString msg = tr(
            "The calibration frame counts (darks, flats, or bias) differ "
            "among the registered frames in the following group(s). The "
            "counts from the earliest frame in each group have been used.\n\n");
        for (const QString &lbl : std::as_const(calConflictLabels))
            msg += QStringLiteral("  \u2022 ") + lbl + QLatin1Char('\n');
        QMessageBox::information(this, tr("Calibration Count Mismatch"), msg);
    }

    // ── Emit partial AMBTEMP warning (once per label+strategy) ───────────
    if (!partialAmbTempLabels.isEmpty()) {
        for (const QString &lbl : std::as_const(partialAmbTempLabels))
            m_ambTempWarnedKeys.insert(
                lbl + QLatin1Char('|') + QString::number(strategy));
        QString msg = tr(
            "Not all files in the following groups contained an ambient "
            "temperature (AMBTEMP keyword). The temperature was calculated "
            "using only those files that contain the AMBTEMP keyword:\n\n");
        for (const QString &lbl : std::as_const(partialAmbTempLabels))
            msg += QStringLiteral("  \u2022 ") + lbl + QLatin1Char('\n');
        QMessageBox::information(this, tr("Partial Temperature Data"), msg);
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
        // Collect unique target names from resolved frames first.
        // Fall back to the group's logTarget, then the log filename.
        QSet<QString> grpTargets;
        for (const auto &frame : grp.frames) {
            if (!frame.logTarget.isEmpty())
                grpTargets.insert(frame.logTarget);
        }
        if (grpTargets.isEmpty()) {
            const QString t = grp.logTarget.isEmpty()
                ? QFileInfo(grp.sourceLogFile).baseName()
                : grp.logTarget;
            grpTargets.insert(t);
        }
        for (const QString &t : std::as_const(grpTargets)) {
            if (!seen.contains(t)) {
                seen.insert(t);
                targets << t;
            }
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
    int totalFrames = 0;
    for (const auto &g : m_groups) totalFrames += g.frames.size();
    m_statusLabel->setText(
        tr("%1 log file(s) loaded · %2 integration group(s) · "
           "%3 CSV row(s)")
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
        case Qt::Key_Equal: changeFontSize(+1);  return;
        case Qt::Key_Minus: changeFontSize(-1);  return;
        case Qt::Key_0:     changeFontSize(0);   return;
        default: break;
        }
    }
    QMainWindow::keyPressEvent(e);
}

void MainWindow::changeFontSize(int delta, bool save)
{
    int newSize;
    if (delta == 0)       newSize = m_baseFontSize;
    else if (!save)       newSize = delta;
    else                  newSize = QApplication::font().pointSize() + delta;
    newSize = qBound(7, newSize, 24);

    QFont appFont = QApplication::font();
    appFont.setPointSize(newSize);
    QApplication::setFont(appFont);

    for (QWidget *w : QApplication::allWidgets()) {
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
    if (save)
        AppSettings::instance().setFontSize(delta == 0 ? -1 : newSize);
}
