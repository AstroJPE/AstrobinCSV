#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include "settings/appsettings.h"

class FilterWebScraper : public QObject {
    Q_OBJECT
public:
    explicit FilterWebScraper(QObject *parent = nullptr);
    void start();

signals:
    void finished(const QList<AstrobinFilter> &filters);
    void statusUpdate(const QString &message);

private slots:
    void onReplyFinished(QNetworkReply *reply);

private:
    void fetchPage(const QUrl &url);

    QNetworkAccessManager  m_nam;
    QList<AstrobinFilter>  m_collected;

    static constexpr const char *kBaseUrl =
        "https://app.astrobin.com/api/v2/equipment/filter/"
        "?format=json&page_size=50";
};
