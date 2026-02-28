#pragma once
#include <QString>
#include <optional>

// Reads the integrated-frame count from a PixInsight master dark or flat .xisf.
// Supports two formats:
//   New PI: <table id="images" rows="N"> in the XML header block
//   Old PI: FITSKeyword HISTORY comment="ImageIntegration.numberOfImages: N"
//
// Reads at most kScanBytes from the file.
class XisfMasterFrameReader {
public:
    static constexpr qint64 kScanBytes = 256 * 1024;  // 256 KB

    // Returns the frame count, or nullopt if not found / file unreadable.
    static std::optional<int> readFrameCount(const QString &path);
};
