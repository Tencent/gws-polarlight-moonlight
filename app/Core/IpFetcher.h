#ifndef _IPFETCHER_H_
#define _IPFETCHER_H_

#include <QCoreApplication>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QUrl>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>


class PublicIpFetcher : public QObject {
    Q_OBJECT
public:
    PublicIpFetcher() {

    }

    QString getPublicIp(int timeout);

};

#endif
    
