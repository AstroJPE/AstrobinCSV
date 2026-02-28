#pragma once
#include <QMainWindow>
#include <QKeyEvent>
#include <QCloseEvent>
#include <QPlainTextEdit>
#include <QList>
#include <QSortFilterProxyModel>
#include <QAtomicInt>
#include "acquisitiontableview.h"
#include "logparser/calibrationlogparser.h"
#include "xisfmasterframereader.h"
#include "models/acquisitiongroup.h"
#include "models/acquisitionrow.h"
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
    void changeFontSize(int delta, bool save = true);  // delta in points; 0 = reset to system default

    void buildMenu();
    void buildToolBar();
    void buildCentralWidget();
    void applyTheme(const QString &theme);

    void loadLogFile(const QString &path);
    void checkForOldDebugLogs();
    void resolveXisfHeaders(QList<AcquisitionGroup> &newGroups);
    void resolveAmbientTemperatures(QList<AcquisitionGroup> &newGroups);
    void resolveCalibrationBlocks(QList<AcquisitionGroup> &groups,
                                  const QStringList &logFiles);
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
    QList<AcquisitionRow> applyGrouping(
        const QList<const AcquisitionGroup *> &groups,
        const QString &target,
        GroupingStrategy strategy) const;
    void applyLocationToRows(QList<AcquisitionRow> &rows) const;

    QList<AcquisitionGroup> m_groups;

    // Calibration master file directory caches — persist across Add Log calls
    // for the lifetime of the app session so the user only needs to locate a
    // missing master directory once.
    // primaryMasterCache  : exact directories where a master file was found;
    //                       checked with QFile::exists() before any search.
    // secondaryMasterCache: user-supplied directories searched recursively.
    QSet<QString>  m_primaryMasterCache;
    QList<QString> m_secondaryMasterCache;
    // already been shown. Fires once per unique group label + grouping
    // strategy combination, so switching strategies re-shows the warning
    // for the new row arrangement but repeated Manage*/location changes do not.
    QSet<QString>           m_ambTempWarnedKeys;

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
