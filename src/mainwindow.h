#pragma once
#include <QMainWindow>
#include <QKeyEvent>
#include <QCloseEvent>
#include <QPlainTextEdit>
#include <QList>
#include <QSortFilterProxyModel>
#include <QAtomicInt>
#include <QHash>
#include "acquisitiontableview.h"
#include "logparser/calibrationlogparser.h"
#include "xisfmasterframereader.h"
#include "models/integrationgroup.h"
#include "models/acquisitionrow.h"
#include "masterfilecache.h"
#include "debuglogger.h"
#include "dialogs/debugresultdialog.h"

class CsvTableModel;
class QComboBox;
class QLabel;
class QProgressBar;
class QListWidget;
class QSplitter;
class QAction;
class QPushButton;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

protected:
    void keyPressEvent(QKeyEvent *e) override;
    void closeEvent(QCloseEvent *e) override;

private slots:
    void onAddLog();
    void onRemoveLog();
    void onExportCsv();
    void onCopyCsv();
    void onGroupingChanged(int index);
    void onManageLocations();
    void onManageFilters();
    void onManageTargets();
    void onAbout();
    void onToggleTheme();
    void onToggleDebugLogging();

private:
    void changeFontSize(int delta, bool save = true);

    void buildMenu();
    void buildToolBar();
    void buildCentralWidget();
    void applyTheme(const QString &theme);
    void checkForOldDebugLogs();

    // Log loading pipeline.
    void resolveFrames(QList<IntegrationGroup> &newGroups,
                       const QStringList       &allLogFiles);
    QString promptForDirectory(const QString &missingPath,
                               const QString &startDir,
                               const QString &errorMessage = {});
    QString promptForMasterDirectory(const QString &missingPath,
                                     const QString &startDir,
                                     const QString &errorMessage = {});
    void rebuildRows();
    void updateStatusBar();
    QStringList knownLogTargets() const;

    enum GroupingStrategy { ByDate = 0, ByDateGainTemp = 1, Collapsed = 2 };
    QList<AcquisitionRow> buildRows(
        const QList<const IntegrationGroup *> &groups,
        const QString                         &astrobinTarget,
        GroupingStrategy                       strategy) const;
    void applyLocationToRows(QList<AcquisitionRow> &rows) const;

    // Data.
    QList<IntegrationGroup> m_groups;

    // Master file directory cache — persists across Add Log... calls.
    MasterFileCache         m_masterCache;

    // "groupLabel|strategy" keys for which the calibration conflict warning
    // has already been shown.
    QSet<QString>           m_calConflictWarnedKeys;

    // "groupLabel|strategy" keys for which the partial-AMBTEMP warning
    // has already been shown.
    QSet<QString>           m_ambTempWarnedKeys;

    // Widgets.
    CsvTableModel         *m_model{nullptr};
    QSortFilterProxyModel *m_proxyModel{nullptr};
    AcquisitionTableView  *m_tableView{nullptr};
    QComboBox             *m_groupingCombo{nullptr};
    QComboBox             *m_locationCombo{nullptr};
    QListWidget           *m_logFileList{nullptr};
    QSplitter             *m_splitter{nullptr};
    QLabel                *m_statusLabel{nullptr};
    QProgressBar          *m_progressBar{nullptr};
    QPushButton           *m_cancelBtn{nullptr};
    QAtomicInt             m_cancelRequested{0};
    QPlainTextEdit        *m_summaryEdit{nullptr};
    int                    m_baseFontSize{10};
    QAction               *m_themeAction{nullptr};
    QAction               *m_debugLogAction{nullptr};

    QString                m_currentTheme;
};
