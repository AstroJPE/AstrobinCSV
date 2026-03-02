#pragma once
#include <QSet>
#include <QList>
#include <QString>

// Persistent cache of master calibration file directories, owned by
// MainWindow and passed by pointer into FrameResolveWorker.
// Survives across multiple Add Log... calls; cleared when logs are removed.
struct MasterFileCache {
    // Exact directories where a master file was previously found.
    // Checked with QFile::exists() before any recursive search.
    QSet<QString>  primaryDirs;

    // User-supplied directories searched recursively via findRecursive().
    QList<QString> secondaryDirs;

    // Set to true when the user cancels a master directory prompt.
    // Suppresses further prompts for the remainder of the current import.
    // Reset to false at the start of each Add Log... call.
    bool           skipPrompts{false};
};
