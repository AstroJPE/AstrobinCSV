#include "filterwebscraper.h"
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QTimer>

FilterWebScraper::FilterWebScraper(QObject *parent)
    : QObject(parent)
{
    connect(&m_nam, &QNetworkAccessManager::finished,
            this,   &FilterWebScraper::onReplyFinished);
}

void FilterWebScraper::start()
{
    m_collected.clear();
    fetchPage(QUrl(QString::fromLatin1(kBaseUrl)));
}

void FilterWebScraper::fetchPage(const QUrl &url)
{
    emit statusUpdate(tr("Fetching: %1").arg(url.toString()));
    QNetworkRequest req(url);
    req.setRawHeader("User-Agent", "AstrobinCSV/0.1");
    req.setRawHeader("Accept",     "application/json");
    m_nam.get(req);
}

void FilterWebScraper::onReplyFinished(QNetworkReply *reply)
{
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        emit statusUpdate(tr("Network error: %1").arg(reply->errorString()));
        emit finished(m_collected);
        return;
    }

    QVariant redirect = reply->attribute(
        QNetworkRequest::RedirectionTargetAttribute);
    if (redirect.isValid()) {
        QUrl target = redirect.toUrl();
        if (target.isRelative()) target = reply->url().resolved(target);
        fetchPage(target);
        return;
    }

    QByteArray body = reply->readAll();
    QJsonParseError err;
    auto doc = QJsonDocument::fromJson(body, &err);

    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        emit statusUpdate(tr("JSON parse error: %1").arg(err.errorString()));
        emit finished(m_collected);
        return;
    }

    auto root    = doc.object();
    auto results = root[QStringLiteral("results")].toArray();

    for (const auto &v : results) {
        auto obj = v.toObject();
        AstrobinFilter f;
        f.id = obj[QStringLiteral("id")].toInt(-1);
        if (f.id < 0) continue;
        f.brandName = obj[QStringLiteral("brandName")].toString().trimmed();
        f.name      = obj[QStringLiteral("name")].toString().trimmed();
        if (f.name.isEmpty()) continue;
        m_collected << f;
    }

    QJsonValue nextVal = root[QStringLiteral("next")];
    const bool hasNext = !nextVal.isNull() && !nextVal.toString().isEmpty();

    // Emit the running count and allow the UI to repaint before fetching the
    // next page. Without this yield the count update is immediately overwritten
    // by the "Fetching: <url>" message from the next fetchPage() call, so the
    // user only ever sees URLs and never the count.
    const QString countMsg = hasNext
        ? tr("%1 filters collected so far…").arg(m_collected.size())
        : tr("Done. %1 filters fetched.").arg(m_collected.size());
    emit statusUpdate(countMsg);

    if (hasNext) {
        QUrl nextUrl(nextVal.toString());
        QTimer::singleShot(0, this, [this, nextUrl]() {
            fetchPage(nextUrl);
        });
    } else {
        emit finished(m_collected);
    }
}
