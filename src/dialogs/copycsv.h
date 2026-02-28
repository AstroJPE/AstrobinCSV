#pragma once
#include <QDialog>
#include <QEvent>

class QComboBox;
class QPlainTextEdit;
class QPushButton;
class CsvTableModel;

class CopyCsvDialog : public QDialog {
    Q_OBJECT
public:
    // model       – the live CsvTableModel (read-only use)
    // hiddenCols  – mirrors MainWindow's AppSettings::hiddenColumns()
    explicit CopyCsvDialog(const CsvTableModel *model,
                           const QSet<int>     &hiddenCols,
                           QWidget             *parent = nullptr);

protected:
    void changeEvent(QEvent *e) override;

private slots:
    void onTargetChanged(int index);
    void onCopy();

private:
    void refreshPreview();
    void syncPreviewFont();

    const CsvTableModel *m_model;
    QSet<int>            m_hiddenCols;

    QComboBox      *m_targetCombo{nullptr};
    QPlainTextEdit *m_preview{nullptr};
    QPushButton    *m_copyBtn{nullptr};
};
