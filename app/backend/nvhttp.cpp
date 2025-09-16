#include <Limelight.h>

#include <QDebug>
#include <QEventLoop>
#include <QImageReader>
#include <QJsonDocument>
#include <QNetworkProxy>
#include <QSslKey>
#include <QTimer>
#include <QUuid>
#include <QXmlStreamReader>
#include <QtEndian>
#include <QtNetwork/QNetworkReply>
#include <SDL.h>

#include "nvcomputer.h"
#include "streaming/encryption.h"
#include "Core/displayInfo.h"
#include "Core/MsgInfo.h"

#define FAST_FAIL_TIMEOUT_MS 2000
#define REQUEST_TIMEOUT_MS 5000
#define LAUNCH_TIMEOUT_MS 120000
#define RESUME_TIMEOUT_MS 30000
#define QUIT_TIMEOUT_MS 30000

NvHTTP::NvHTTP(NvAddress address, uint16_t httpsPort, QSslCertificate serverCert) : m_ServerCert(serverCert) {
    m_BaseUrlHttp.setScheme("http");
    m_BaseUrlHttps.setScheme("https");

    setAddress(address);
    setHttpsPort(httpsPort);

    // Never use a proxy server
    QNetworkProxy noProxy(QNetworkProxy::NoProxy);
    m_Nam.setProxy(noProxy);

    connect(&m_Nam, &QNetworkAccessManager::sslErrors, this, &NvHTTP::handleSslErrors);
}

NvHTTP::NvHTTP(NvComputer* computer)
    : NvHTTP(computer->activeAddress, computer->activeHttpsPort, computer->serverCert) {}

void NvHTTP::setServerCert(QSslCertificate serverCert) { m_ServerCert = serverCert; }

void NvHTTP::setAddress(NvAddress address) {
    Q_ASSERT(!address.isNull());

    m_Address = address;

    m_BaseUrlHttp.setHost(address.address());
    m_BaseUrlHttps.setHost(address.address());

    m_BaseUrlHttp.setPort(address.port());
}

void NvHTTP::setHttpsPort(uint16_t port) { m_BaseUrlHttps.setPort(port); }

NvAddress NvHTTP::address() { return m_Address; }

QSslCertificate NvHTTP::serverCert() { return m_ServerCert; }

uint16_t NvHTTP::httpPort() {
    return m_BaseUrlHttp.port();
}

uint16_t NvHTTP::httpsPort() {
    return m_BaseUrlHttps.port();
}

QVector<int> NvHTTP::parseQuad(QString quad) {
    QVector<int> ret;

    // Return an empty vector for old GFE versions
    // that were missing GfeVersion.
    if (quad.isEmpty()) {
        return ret;
    }

    QStringList parts = quad.split(".");
    ret.reserve(parts.length());
    for (int i = 0; i < parts.length(); i++) {
        ret.append(parts.at(i).toInt());
    }

    return ret;
}

int NvHTTP::getCurrentGame(QString serverInfo) {
    // GFE 2.8 started keeping currentgame set to the last game played. As a result, it no longer
    // has the semantics that its name would indicate. To contain the effects of this change as much
    // as possible, we'll force the current game to zero if the server isn't in a streaming session.
    QString serverState = getXmlString(serverInfo, "state");
    if (serverState != nullptr && serverState.endsWith("_SERVER_BUSY")) {
        return getXmlString(serverInfo, "currentgame").toInt();
    }
    else {
        return 0;
    }
}

QString NvHTTP::getServerInfo(NvLogLevel logLevel, bool fastFail) {
    QString serverInfo;

    // Check if we have a pinned cert and HTTPS port for this host yet
    if (!m_ServerCert.isNull() && httpsPort() != 0) {
        try {
            // Always try HTTPS first, since it properly reports
            // pairing status (and a few other attributes).
            serverInfo = openConnectionToString(m_BaseUrlHttps, "serverinfo", nullptr,
                fastFail ? FAST_FAIL_TIMEOUT_MS : REQUEST_TIMEOUT_MS, logLevel);
            // Throws if the request failed
            verifyResponseStatus(serverInfo);
        }
        catch (const GfeHttpResponseException& e) {
            if (e.getStatusCode() == 401) {
                // Certificate validation error, fallback to HTTP
                serverInfo = openConnectionToString(m_BaseUrlHttp, "serverinfo", nullptr,
                    fastFail ? FAST_FAIL_TIMEOUT_MS : REQUEST_TIMEOUT_MS, logLevel);
                verifyResponseStatus(serverInfo);
            }
            else {
                // Rethrow real errors
                throw e;
            }
        }
    }
    else {
        // Only use HTTP prior to pairing or fetching HTTPS port
        serverInfo = openConnectionToString(m_BaseUrlHttp, "serverinfo", nullptr,
            fastFail ? FAST_FAIL_TIMEOUT_MS : REQUEST_TIMEOUT_MS, logLevel);

        verifyResponseStatus(serverInfo);
        // Populate the HTTPS port
        uint16_t httpsPort = getXmlString(serverInfo, "HttpsPort").toUShort();
        httpsPort = httpsPort = LiGetPortFromPortFlagIndex(ML_PORT_INDEX_TCP_47984);
        if (httpsPort == 0) {
            httpsPort = DEFAULT_HTTPS_PORT;  // DEFAULT_HTTPS_PORT
        }
        setHttpsPort(httpsPort);
        // If we just needed to determine the HTTPS port, we'll try again over
        // HTTPS now that we have the port number
        if (!m_ServerCert.isNull()) {
            return getServerInfo(logLevel, fastFail);
        }
    }

    return serverInfo;
}

void NvHTTP::startApp(QString verb, bool isGfe, int appId, PSTREAM_CONFIGURATION streamConfig, bool sops,
    bool localAudio, int gamepadMask, bool persistGameControllersOnDisconnect,
    QString& rtspSessionUrl, QString& rtspCSessionID, std::string& UUid, std::string& username,
    std::string& watermark, std::string& userID, QString& decrypt_text, QString& szStatus) {
    int riKeyId;

    memcpy(&riKeyId, streamConfig->remoteInputAesIv, sizeof(riKeyId));
    riKeyId = qFromBigEndian(riKeyId);
    qDebug() << " NvHTTP::startApp 1";
    QString response = openConnectionToString(
        m_BaseUrlHttps, verb,
        "appid=" + QString::number(appId) + "&clientuuid=" + (UUid.c_str()) +
        "&username=" + username.c_str() + "&watermark=" + watermark.c_str() +
        "&userID=" + userID.c_str() +
        "&additionalStates=1&sops=" + QString::number(sops ? 1 : 0) +
        "&rikey=" + QByteArray(streamConfig->remoteInputAesKey, sizeof(streamConfig->remoteInputAesKey)).toHex() +
        "&rikeyid=" + QString::number(riKeyId) +
        (streamConfig->enableHdr ?
            "&hdrMode=1&clientHdrCapVersion=0&clientHdrCapSupportedFlagsInUint32=0&clientHdrCapMetaDataId=NV_"
            "STATIC_METADATA_TYPE_1&clientHdrCapDisplayData=0x0x0x0x0x0x0x0x0x0x0" :
            "") +
        "&localAudioPlayMode=" + QString::number(localAudio ? 1 : 0) + "&surroundAudioInfo=" +
        QString::number(SURROUNDAUDIOINFO_FROM_AUDIO_CONFIGURATION(streamConfig->audioConfiguration)) +
        "&remoteControllersBitmap=" + QString::number(gamepadMask) + "&gcmap=" + QString::number(gamepadMask) +
        "&gcpersist=" + QString::number(persistGameControllersOnDisconnect ? 1 : 0),
        LAUNCH_TIMEOUT_MS);
    qDebug() << "NvHTTP::startApp 2:" << verb;
    qDebug() << "Launch response:" << response;

    // Throws if the request failed
    verifyResponseStatus(response);
    rtspSessionUrl = getXmlString(response, "sessionUrl0");
    rtspCSessionID = getXmlString(response, "csessionid");
    decrypt_text = getXmlString(response, "clientguuid");
    szStatus = getXmlString(response, "status");
    qInfo() << "szStatus:" << szStatus;
}

bool NvHTTP::quitApp() {
    QString response = openConnectionToString(m_BaseUrlHttps, "cancel", nullptr, QUIT_TIMEOUT_MS);

    qInfo() << "Quit response:" << response;

    // Throws if the request failed
    bool nRet = verifyResponseStatus(response);

    // Newer GFE versions will just return success even if quitting fails
    // if we're not the original requestor.
    if (getCurrentGame(getServerInfo(NvHTTP::NVLL_ERROR)) != 0) {
        // Generate a synthetic GfeResponseException letting the caller know
        // that they can't kill someone else's stream.
        throw GfeHttpResponseException(599, "");
    }
    return nRet;
}

QVector<NvDisplayMode> NvHTTP::getDisplayModeList(QString serverInfo) {
    QXmlStreamReader xmlReader(serverInfo);
    QVector<NvDisplayMode> modes;

    while (!xmlReader.atEnd()) {
        while (xmlReader.readNextStartElement()) {
            auto name = xmlReader.name();
            if (name == QString("DisplayMode")) {
                modes.append(NvDisplayMode());
            }
            else if (name == QString("Width")) {
                modes.last().width = xmlReader.readElementText().toInt();
            }
            else if (name == QString("Height")) {
                modes.last().height = xmlReader.readElementText().toInt();
            }
            else if (name == QString("RefreshRate")) {
                modes.last().refreshRate = xmlReader.readElementText().toInt();
            }
        }
    }

    return modes;
}

QVector<NvApp> NvHTTP::getAppList() {
    QString appxml =
        openConnectionToString(m_BaseUrlHttps, "applist", nullptr, REQUEST_TIMEOUT_MS, NvLogLevel::NVLL_ERROR);
    verifyResponseStatus(appxml);

    QXmlStreamReader xmlReader(appxml);
    QVector<NvApp> apps;
    while (!xmlReader.atEnd()) {
        while (xmlReader.readNextStartElement()) {
            auto name = xmlReader.name();
            if (name == QString("App")) {
                // We must have a valid app before advancing to the next one
                if (!apps.isEmpty() && !apps.last().isInitialized()) {
                    qWarning() << "Invalid applist XML";
                    Q_ASSERT(false);
                    return QVector<NvApp>();
                }
                apps.append(NvApp());
            }
            else if (name == QString("AppTitle")) {
                apps.last().name = xmlReader.readElementText();
            }
            else if (name == QString("ID")) {
                apps.last().id = xmlReader.readElementText().toInt();
            }
            else if (name == QString("IsHdrSupported")) {
                apps.last().hdrSupported = xmlReader.readElementText() == "1";
            }
            else if (name == QString("IsAppCollectorGame")) {
                apps.last().isAppCollectorGame = xmlReader.readElementText() == "1";
            }
        }
    }

    return apps;
}

bool NvHTTP::verifyResponseStatus(QString xml) {
    QXmlStreamReader xmlReader(xml);

    while (xmlReader.readNextStartElement()) {
        if (xmlReader.name() == QString("root")) {
            // Status code can be 0xFFFFFFFF in some rare cases on GFE 3.20.3, and
            // QString::toInt() will fail in that case, so use QString::toUInt()
            // and cast the result to an int instead.
            int statusCode = (int)xmlReader.attributes().value("status_code").toInt();
            if (statusCode == 200) {
                // Successful
                return true;
            }
            else {
                QString statusMessage = xmlReader.attributes().value("status_message").toString();
                if (statusCode != 401) {
                    // 401 is expected for unpaired PCs when we fetch serverinfo over HTTPS
                    statusMessage = QStringLiteral("error code %1, %2").arg(statusCode).arg(statusMessage);
                    qWarning() << "Request failed:" << statusCode << statusMessage;
                }
                if (statusCode == -1 && statusMessage == "Invalid") {
                    // Special case handling an audio capture error which GFE doesn't
                    // provide any useful status message for.
                    statusCode = 418;
                    statusMessage = tr("Missing audio capture device. Reinstalling GeForce Experience should resolve this error.");
                }
                throw GfeHttpResponseException(statusCode, statusMessage);
            }
        }
    }

    throw GfeHttpResponseException(-1, "Malformed XML (missing root element)");
    return  false;
}

QImage NvHTTP::getBoxArt(int appId) {
    bool bByteFlag = false;
    QNetworkReply* reply =
        openConnection(m_BaseUrlHttps, "appasset", "appid=" + QString::number(appId) + "&AssetType=2&AssetIdx=0",
            REQUEST_TIMEOUT_MS, bByteFlag, NvLogLevel::NVLL_VERBOSE);
    QImage image = QImageReader(reply).read();
    delete reply;

    return image;
}

QByteArray NvHTTP::getXmlStringFromHex(QString xml, QString tagName) {
    QString str = getXmlString(xml, tagName);
    if (str == nullptr) {
        return nullptr;
    }

    return QByteArray::fromHex(str.toLatin1());
}

QString NvHTTP::getXmlString(QString xml, QString tagName) {
    QXmlStreamReader xmlReader(xml);

    while (!xmlReader.atEnd()) {
        if (xmlReader.readNext() != QXmlStreamReader::StartElement) {
            continue;
        }

        if (xmlReader.name() == tagName) {
            return xmlReader.readElementText();
        }
    }

    return nullptr;
}

void NvHTTP::handleSslErrors(QNetworkReply* reply, const QList<QSslError>& errors) {
    bool ignoreErrors = true;

    if (m_ServerCert.isNull()) {
        // We should never make an HTTPS request without a cert
        Q_ASSERT(!m_ServerCert.isNull());
        return;
    }

    for (const QSslError& error : errors) {
        if (m_ServerCert != error.certificate()) {
            ignoreErrors = false;
            break;
        }
    }

    if (ignoreErrors) {
        reply->ignoreSslErrors(errors);
    }
}

QByteArray NvHTTP::openConnectionToByteArray(QUrl baseUrl, QString command, QString arguments, int timeoutMs,
    bool& bByteFlag, NvLogLevel logLevel) {
    QNetworkReply* reply = openConnection(baseUrl, command, arguments, timeoutMs, bByteFlag, logLevel);
    QString ret;

    QTextStream stream(reply);

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    stream.setEncoding(QStringConverter::Utf8);
#else
    stream.setCodec("UTF-8");
#endif

    //ret = stream.readAll();
    QByteArray data = reply->readAll();
    delete reply;
    return data;
}
QString NvHTTP::openConnectionToString(QUrl baseUrl, QString command, QString arguments, int timeoutMs,
    NvLogLevel logLevel) {
    bool flag = false;
    QNetworkReply* reply = openConnection(baseUrl, command, arguments, timeoutMs, flag, logLevel);
    QString ret;

    QTextStream stream(reply);

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    stream.setEncoding(QStringConverter::Utf8);
#else
    stream.setCodec("UTF-8");
#endif

    ret = stream.readAll();
    delete reply;

    return ret;
}

QNetworkReply* NvHTTP::waitForReply(QNetworkRequest& request, QUrl& url, int timeoutMs,
    NvLogLevel logLevel, QByteArray bodyData, bool bPostGet) {
    QNetworkReply* reply;
    if (bPostGet)
    {
        reply = m_Nam.post(request, bodyData);
    }
    else
    {
        reply = m_Nam.get(request);
    }
    // Run the request with a timeout if requested
    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, &loop, &QEventLoop::quit);

    if (timeoutMs) {
        QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
    }
    if (logLevel >= NvLogLevel::NVLL_VERBOSE) {
        // qInfo() << "Executing request:" << url.toString();
    }
    loop.exec(QEventLoop::ExcludeUserInputEvents);

    // Abort the request if it timed out
    if (!reply->isFinished()) {
        qWarning() << "Aborting timed out request for" << url.toString();
        reply->abort();
    }
    return reply;
}
QNetworkReply* NvHTTP::openConnection(QUrl baseUrl, QString command, QString arguments, int timeoutMs, bool& bByteFlag,
    NvLogLevel logLevel) {
    // Port must be set
    Q_ASSERT(baseUrl.port(0) != 0);

    // Build a URL for the request
    QUrl url(baseUrl);
    url.setPath("/" + command);

    // Use a common UID for Gleam clients to allow them to quit
    // games for each other (otherwise GFE gets screwed up and it requires
    // manual intervention to solve).
    url.setQuery("uniqueid=0123456789ABCDEF&uuid=" + QUuid::createUuid().toRfc4122().toHex() +
        ((arguments != nullptr) ? ("&" + arguments) : ""));

    QNetworkRequest request(url);

    // Add our client certificate
    request.setSslConfiguration(IdentityManager::get()->getSslConfig());

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    // Disable HTTP/2 (GFE 3.22 doesn't like it) and Qt 6 enables it by default
    request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
#endif

#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0) && QT_VERSION < QT_VERSION_CHECK(5, 15, 1) && \
    !defined(QT_NO_BEARERMANAGEMENT)
    // HACK: Set network accessibility to work around QTBUG-80947 (introduced in Qt 5.14.0 and fixed in Qt 5.15.1)
    QT_WARNING_PUSH
        QT_WARNING_DISABLE_DEPRECATED
        m_Nam.setNetworkAccessible(QNetworkAccessManager::Accessible);
    QT_WARNING_POP
#endif

    QNetworkReply* reply = waitForReply(request, url, timeoutMs, logLevel);
    QString szContentType = reply->rawHeader("Content-Type");
    if (!szContentType.isEmpty())
    {
        bByteFlag = true;
    }
    //qDebug()<<"szContentType = "<<szContentType;
    // We must clear out cached authentication and connections or
    // GFE will puke next time
    m_Nam.clearAccessCache();
    //auto processCode = [](int code) {
    //    SDL_Event event;
    //    event.type = SDL_SET_STATUS_ERROR_CODE;
    //    event.quit.timestamp = SDL_GetTicks();
    //    event.user.code = code;
    //    SDL_PushEvent(&event);
    //};
    // Handle error
    static unsigned timeoutCnt = 0;
    static unsigned maxTimeoutCnt = 10;
    static int s_iOpId = UIStateMachine::I().genOpId();
    if (reply->error() != QNetworkReply::NoError) {
        if (logLevel >= NvLogLevel::NVLL_ERROR) {
            qWarning() << command << "request failed with error:" << reply->error();
        }

        if (reply->error() == QNetworkReply::SslHandshakeFailedError) {
            // This will trigger falling back to HTTP for the serverinfo query
            // then pairing again to get the updated certificate.
            GfeHttpResponseException exception(401, "Server certificate mismatch");
            //processCode(QNetworkReply::SslHandshakeFailedError);
            //UIStateMachine::I().push(MsgCode::SslHandshakeFailedError);
            delete reply;
            throw exception;
        }
        else {
            timeoutCnt++;
            QtNetworkReplyException exception(QNetworkReply::TimeoutError, "Request timed out");
            if (timeoutCnt > maxTimeoutCnt) {
                UIStateMachine::I().push(s_iOpId, MsgCode::Network_OperationCanceledError);
            }
            qWarning() << command << "request failed with relply:" << reply->errorString();
            delete reply;
            throw exception;
        }
    }
    else {
        timeoutCnt = 0;
        UIStateMachine::I().clear(s_iOpId);
    }
    //QTextStream stream(reply);
    //qInfo() << "NvHTTP::openConnection command = " << command<<"stream = "<< stream.Text;
    return reply;
}
typedef QMap<QByteArray, QByteArray> MAPBYTE;
QNetworkReply* NvHTTP::openConnectionByToken(QString baseUrl, QString token, QJsonDocument body, int timeoutMs,
    NvLogLevel logLevel, int& nRet) {
    QUrl url(baseUrl);
    QNetworkRequest request(url);

    MAPBYTE headers = MAPBYTE();
    headers["content-type"] = "application/json;charset=UTF-8";
    if (!token.isEmpty()) {
        std::string Bearer = "Bearer ";
        Bearer += token.toStdString();
        headers["Authorization"] = Bearer.c_str();
    }
    for (MAPBYTE::iterator it = headers.begin(); it != headers.end(); it++) {
        request.setRawHeader(it.key(), it.value());
    }

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    // Disable HTTP/2 (GFE 3.22 doesn't like it) and Qt 6 enables it by default
    request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
#endif

#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0) && QT_VERSION < QT_VERSION_CHECK(5, 15, 1) && \
    !defined(QT_NO_BEARERMANAGEMENT)
    // HACK: Set network accessibility to work around QTBUG-80947 (introduced in Qt 5.14.0 and fixed in Qt 5.15.1)
    QT_WARNING_PUSH
        QT_WARNING_DISABLE_DEPRECATED
        m_Nam.setNetworkAccessible(QNetworkAccessManager::Accessible);
    QT_WARNING_POP
#endif

    QByteArray data;
    qInfo() << "body data:" << body;
    QByteArray bodyData = body.toJson(QJsonDocument::Compact);
    qDebug() << "openConnectionByToken bodyData = " << bodyData.toStdString();

    QNetworkReply* reply = waitForReply(request, url, timeoutMs, logLevel, bodyData, true);
    // We must clear out cached authentication and connections or
    // GFE will puke next time
    m_Nam.clearAccessCache();

    // Handle error
    int nError = reply->error();
    qDebug() << "nError: " << nError;
    qDebug() << "errorString: " << reply->errorString();
    if (reply->error() != QNetworkReply::NoError) {
        qInfo() << " reply->error(): " << nError;
        nRet = -1;
        return reply;
    }
    nRet = 0;
    return reply;
}

QString NvHTTP::GwsHttpsPost(QString baseUrl, QString token, QJsonDocument body, int& nRet) {
    QString response = ConnectionToStringByToken(baseUrl, token, body, QUIT_TIMEOUT_MS, nRet);

    qInfo() << "GwsHttpsPost:" << response;
    return response;
}

QString NvHTTP::GwsHttpsClientResolution(QString baseUrl, QString token, QJsonDocument body, int& nRet) {
    QString response = ConnectionToStringByToken(baseUrl, token, body, QUIT_TIMEOUT_MS, nRet);
    qInfo() << "GwsHttpsPost:" << response;
    return response;
}
QString NvHTTP::ConnectionToStringByToken(QString baseUrl, QString token, QJsonDocument body, int timeoutMs, int& nRet,
    NvLogLevel logLevel) {
    QNetworkReply* reply = openConnectionByToken(baseUrl, token, body, timeoutMs, logLevel, nRet);
    QString ret = reply->readAll();
    delete reply;

    return ret;
}

NvReqHTTPS::NvReqHTTPS() {}

std::string NvReqHTTPS::RequestByToken(QString& baseUrl, QString token,
    QJsonDocument body, int timeoutMs,
    int& nRet,bool PostFlag) {
    nRet = 0;
    std::string OutStr;
    QUrl url(baseUrl);
    QNetworkRequest request(url);

    // request.setHeader(QNetworkRequest::ContentTypeHeader,QVariant("application/json;charset=UTF-8"));
    MAPBYTE headers = MAPBYTE();
    headers["content-type"] = "application/json;charset=UTF-8";
    if (!token.isEmpty()) {
        std::string Bearer = "Bearer ";
        Bearer += token.toStdString();
        headers["Authorization"] = Bearer.c_str();
    }
    for (MAPBYTE::iterator it = headers.begin(); it != headers.end(); it++) {
        request.setRawHeader(it.key(), it.value());
    }

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    // Disable HTTP/2 (GFE 3.22 doesn't like it) and Qt 6 enables it by default
    request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
#endif

#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0) && QT_VERSION < QT_VERSION_CHECK(5, 15, 1) && \
    !defined(QT_NO_BEARERMANAGEMENT)
    // HACK: Set network accessibility to work around QTBUG-80947 (introduced in Qt 5.14.0 and fixed in Qt 5.15.1)
    QT_WARNING_PUSH
        QT_WARNING_DISABLE_DEPRECATED
        m_Nam.setNetworkAccessible(QNetworkAccessManager::Accessible);
    QT_WARNING_POP
#endif

        QByteArray data;
    // qInfo() << "body data:" <<body;
    QByteArray bodyData = body.toJson(QJsonDocument::Compact);
    // qDebug()<<"openConnectionByToken bodyData = " <<bodyData.toStdString();

    QNetworkReply* reply ;
    if(PostFlag == true)
    {
        reply = m_Nam.post(request, bodyData);
    }
    else
    {
        reply = m_Nam.get(request);
    }
    // Run the request with a timeout if requested
    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, &loop, &QEventLoop::quit);

    if (timeoutMs) {
        QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
    }

    loop.exec(QEventLoop::ExcludeUserInputEvents);

    // Abort the request if it timed out
    if (!reply->isFinished()) {
        qWarning() << "Aborting timed out request for" << baseUrl.toStdString();
        reply->abort();
    }

    // We must clear out cached authentication and connections or
    // GFE will puke next time
    m_Nam.clearAccessCache();

    // Handle error
    int nError = reply->error();
    // std::string outArr = reply->readAll().toStdString();

    qDebug() << "nError: " << nError;
    qDebug()<<"portal retString = "<<reply->errorString();
    nRet = nError;
    if (nError != QNetworkReply::NoError) {
        qWarning() << "request error!: " << reply->errorString();
        return std::string();
    }
    OutStr = reply->readAll().toStdString();
    delete reply;
    return OutStr;
}

int NvHTTP::CheckDisplay() {
    QString szStr = openConnectionToString(m_BaseUrlHttps, "checkdisplay", nullptr, QUIT_TIMEOUT_MS);

    // qInfo() << "CheckDisplay response:" << szStr;

    // Throws if the request failed
    verifyResponseStatus(szStr);

    // Populate the HTTPS port
    QString szDualdisplay = getXmlString(szStr, "dualdisplay");
    int nDualdisplay = -1;
    if (!szDualdisplay.isNull()) {
        nDualdisplay = szDualdisplay.toInt();
    }
    return nDualdisplay;
}

QByteArray NvHTTP::SendMsg(QString& msg, bool& bByteFlag)
{
    QByteArray queryByte = openConnectionToByteArray(m_BaseUrlHttps, "sendmsg", msg, QUIT_TIMEOUT_MS, bByteFlag);
    qDebug() << "SendMsg queryByte:" << bByteFlag;
    if (bByteFlag == false)
    {
        QString szStr = queryByte;
        verifyResponseStatus(szStr);
    }
    return queryByte;
}
