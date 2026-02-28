#include "xisfmasterframereader.h"
#include <QFile>
#include <QXmlStreamReader>
#include <QRegularExpression>

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

// Try to extract the frame count from a chunk of bytes using any known format.
static std::optional<int> scanChunk(const QByteArray &data)
{
    // ── Format 1: literal <table id="images" rows="N"> in XML ─────────────
    // Occurs when the XISF XML header directly contains the table element.
    {
        QXmlStreamReader xml(data);
        xml.setNamespaceProcessing(false);
        while (!xml.atEnd()) {
            auto tok = xml.readNext();
            if (tok == QXmlStreamReader::StartElement
                    && xml.name().compare(QLatin1String("table"),
                                          Qt::CaseInsensitive) == 0) {
                auto id   = xml.attributes().value(QLatin1String("id")).toString();
                auto rows = xml.attributes().value(QLatin1String("rows")).toString();
                if (id.compare(QLatin1String("images"), Qt::CaseInsensitive) == 0
                        && !rows.isEmpty()) {
                    bool ok = false;
                    int  n  = rows.toInt(&ok);
                    if (ok && n > 0) return n;
                }
            }
        }
    }

    // ── Format 2: entity-encoded XML inside a property attribute ──────────
    // The PixInsight:ProcessingHistory property stores its XML payload as an
    // entity-encoded string, so the raw bytes contain:
    //   &lt;table id=&quot;images&quot; rows=&quot;N&quot;&gt;
    // QXmlStreamReader never sees a literal <table> element in this case.
    {
        static const QRegularExpression encRe(
            R"(&lt;table\s+id=&quot;images&quot;\s+rows=&quot;(\d+)&quot;)",
            QRegularExpression::CaseInsensitiveOption);
        QString text = QString::fromLatin1(data);
        auto    m    = encRe.match(text);
        if (m.hasMatch()) {
            bool ok = false;
            int  n  = m.captured(1).toInt(&ok);
            if (ok && n > 0) return n;
        }
    }

    // ── Format 3: old PI HISTORY keyword comment ──────────────────────────
    // Older PixInsight versions embed the count as:
    //   ImageIntegration.numberOfImages: N
    // in a FITS HISTORY keyword comment attribute.
    {
        static const QRegularExpression histRe(
            R"(ImageIntegration\.numberOfImages:\s*(\d+))",
            QRegularExpression::MultilineOption);
        QString text = QString::fromLatin1(data);
        auto    m    = histRe.match(text);
        if (m.hasMatch()) {
            bool ok = false;
            int  n  = m.captured(1).toInt(&ok);
            if (ok && n > 0) return n;
        }
    }

    return std::nullopt;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

std::optional<int> XisfMasterFrameReader::readFrameCount(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return std::nullopt;

    // ── Parse the XISF fixed header ────────────────────────────────────────
    const QByteArray magic = f.read(8);
    if (!magic.startsWith("XISF0100"))
        return std::nullopt;

    const QByteArray lenBytes = f.read(4);
    if (lenBytes.size() < 4)
        return std::nullopt;

    const quint32 xmlLen =
        static_cast<quint8>(lenBytes[0])          |
        (static_cast<quint8>(lenBytes[1]) <<  8)  |
        (static_cast<quint8>(lenBytes[2]) << 16)  |
        (static_cast<quint8>(lenBytes[3]) << 24);

    f.read(4);  // skip reserved bytes (total header so far: 16 bytes)

    // ── Try the declared XML block first ───────────────────────────────────
    if (xmlLen > 0 && xmlLen <= static_cast<quint32>(kScanBytes)) {
        QByteArray xmlData = f.read(xmlLen);
        if (static_cast<quint32>(xmlData.size()) == xmlLen) {
            auto result = scanChunk(xmlData);
            if (result) return result;
        }
        // Fall through: scan remaining budget as raw bytes.
        qint64 already = static_cast<qint64>(16 + xmlLen);
        qint64 remain  = kScanBytes - already;
        if (remain > 0) {
            QByteArray extra = f.read(remain);
            if (!extra.isEmpty()) {
                auto result = scanChunk(extra);
                if (result) return result;
            }
        }
    } else {
        // XML block absent or larger than budget: scan raw bytes.
        QByteArray bulk = f.read(kScanBytes - 16);
        if (!bulk.isEmpty()) {
            auto result = scanChunk(bulk);
            if (result) return result;
        }
    }

    return std::nullopt;
}
