#pragma once
#include <QDialog>

// Shown after a log import completes when debug logging is active.
// Displays the paths to the two debug files and offers a button to
// reveal them in Finder (macOS) or File Explorer (Windows).
class DebugResultDialog : public QDialog {
    Q_OBJECT
public:
    explicit DebugResultDialog(const QString &humanPath,
                               const QString &jsonPath,
                               QWidget *parent = nullptr);
};
