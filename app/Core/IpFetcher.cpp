#include "IpFetcher.h"

QString PublicIpFetcher::getPublicIp(int timeout) {
    QNetworkAccessManager manager;
    QEventLoop loop;
    QString result;
    QList<QUrl> m_urlList;
    m_urlList.append(QUrl("https://inet-ip.info"));
    m_urlList.append(QUrl("https://ipinfo.io/ip"));
    for (const QUrl &url : m_urlList)
    {
        qDebug()<<"url = "<<url;
        QNetworkRequest request(QUrl("https://ipinfo.io/ip"));
        QNetworkReply *reply = manager.get(request);

        // Set up a timer for the timeout
        QTimer timer;
        timer.setSingleShot(true);
        connect(&timer, &QTimer::timeout, [&]() {
            reply->abort(); // Abort the request if it times out
        });
        timer.start(timeout);

        // Wait for the reply
        QObject::connect(reply, &QNetworkReply::finished, [&]() {
            timer.stop();

            if (reply->error() == QNetworkReply::NoError) {

                QByteArray responseData = reply->readAll();
                qDebug()<<"responseData = "<<responseData;
                QString ip = responseData;
                // Validate the IP address
                QHostAddress address(ip);
                if (!address.isNull()) {
                    result = ip;
                }
            }
            loop.quit();
        });

        loop.exec(); // Block until we get a reply

        // Check if we got a valid IP
        if (!result.isEmpty()) {
            reply->deleteLater();
            return result; // Return the valid IP
        }

        reply->deleteLater(); // Clean up the reply object
    }

    return ""; // Return empty string if no valid IP was found
}
