#include "connectionmanager.h"
#include "cli/commandlineparser.h"
#include "backend/computermanager.h"
#include "cli/pair.h"
#include "backend/nvcomputer.h"
#include <iostream>
#include "sessionui.h"
#include "streaming/connection.h"
#include "displayInfo.h"
#include "MsgInfo.h"
#include "httpmessagetask.h"
#include "EventReporter.h"
#include <thread>
#include <chrono>
#include <QUrlQuery>
#include <QThreadPool>
#include <QXmlStreamReader>

void ConnectionManager::ParseArgs(const char* clientUUid, const char* privateKeyStr,const QStringList& args)
{

    auto* pComputerManager = new ComputerManager();
    PairCommandLineParser pairParser;
    pairParser.parse(args);

    QString szUrl = pairParser.getHost();
    QString strtoken = pairParser.getToken();


    m_HttpJsonStr = pairParser.getHttpsJson().toStdString();
    m_Arch = pairParser.getArch().toStdString();

    auto launcher = new CliPair::Launcher(pairParser.getHost(), pairParser.getPredefinedPin(),
        pairParser.getInstanceId(), pairParser.getToken(), pairParser.getHttpsJson(),
        pairParser.getArch(), pairParser.getPairHost(), clientUUid,
        privateKeyStr, /*bFullscreenflag*/false, /*&app*/nullptr);
    launcher->SetSeekSelfOnly(true);
    launcher->execute(pComputerManager);
    launcher->deleteLater();
    pComputerManager->deleteLater();
    std::cout << "Done" << std::endl;
}
static bool UpdateAppList(NvComputer* computer,bool& changed) {
    NvHTTP http(computer);

    QVector<NvApp> appList;

    try {
        appList = http.getAppList();
        if (appList.isEmpty()) {
            return false;
        }
    }
    catch (...) {
        return false;
    }

    QWriteLocker lock(&computer->lock);
    changed = computer->updateAppList(appList);
    return true;
}
static NvApp* FindDesktopApp(NvComputer* computer)
{
    NvApp* pAppMatched = nullptr;
    QString m_AppName = "Desktop";
    int index = -1;
    int size = computer->appList.length();
    for (int i = 0; i < size; i++)
    {
        QString strGet = computer->appList[i].name;
        if (strGet == m_AppName)
        {
            index = i;
            break;
        }
    }
    if (-1 != index)
    {
        pAppMatched = &computer->appList[index];
    }
    return pAppMatched;
}

static CONNECTION_LISTENER_CALLBACKS s_ConnCallbacks = {
    AsyncConnection::clStageStarting,
    nullptr,
    AsyncConnection::clStageFailed,
    nullptr,
    [](int errorCode){Q_UNUSED(errorCode);ConnectionManager::I().OnDisconnected(); },
    AsyncConnection::clLogMessage,
    AsyncConnection::clRumble,
    AsyncConnection::clConnectionStatusUpdate,
    AsyncConnection::clSetHdrMode,
};


bool gReconect = false;
void ConnectionManager::Reconnect()
{
    ConnectionManager::I().pushEvent(new CMReconnectEvent());
}
extern std::string getClientUUid();

HttpMessageTask* ConnectionManager::createMessageTask(const QString& msg,
    const QString& type, int nReq, bool start /*= true*/)
{
    if (!thread_running.load())
        return nullptr;

    QUrlQuery query;
    query.addQueryItem("message", msg);
    query.addQueryItem("type", type);
    query.addQueryItem("client_uuid", getClientUUid().c_str());
    QString szMessage = query.toString(QUrl::FullyEncoded);
    HttpMessageTask* pTask = new HttpMessageTask(SessionUI::I().getSession(), szMessage, nReq);
    if (start) {
        QThreadPool::globalInstance()->start(pTask);
    }
    return pTask;
}

void ConnectionManager::sendQuitMessage()
{
    HttpMessageTask* pTask = createMessageTask("{}", "quit", -1, false);
    pTask->Run();
    pTask->deleteLater();
}

void ConnectionManager::RunQueryStatus()
{
    while(!thread_running.load() || m_bLiStartConnection.load()) {
        std::unique_lock<std::mutex> lock(m_StatusMutex);
        m_StatusCondition.wait_for(lock, std::chrono::milliseconds(1));
    }

    HttpMessageTask* pTask = createMessageTask("{\"Type\":\"Status\"}", "wordbook", -1, false);
    pTask->setAutoDelete(false);
    QObject::connect(pTask, &HttpMessageTask::sendMsgRet, [this](int flag, int nReq, const QByteArray& data) {
        Q_UNUSED(nReq);
        PaserQueryTaskXml(flag, data);
    });
    while (thread_running.load())
    {
        if (m_bLiStartConnection.load())
        {
            pTask->Run();
        }
        std::unique_lock<std::mutex> lock(m_StatusMutex);
        m_StatusCondition.wait_for(lock, std::chrono::milliseconds(1000));
    }
    delete pTask;
    std::cout << "Exit RunQueryStatus function..." << std::endl;
}

void ConnectionManager::PaserQueryTaskXml(int flag, const QByteArray& data)
{
    QString jsonString;
    if (flag == false && !data.isEmpty())
    {
        QString szStr = data;
        QXmlStreamReader xmlReader(szStr);
        QString text;
        while (!xmlReader.atEnd()) {
            if (xmlReader.readNext() != QXmlStreamReader::StartElement)
                continue;

            if (xmlReader.name() == QString("gleamRet")) 
            {
                text = xmlReader.readElementText();
                break;
            }
        }
        SetStatusByXml(text);
    }
}

void ConnectionManager::SetStatusByXml(const QString& text)
{
    QJsonDocument jsonDoc = QJsonDocument::fromJson(text.toUtf8());
    if (!jsonDoc.isObject())
        return;

    Status newStatus;
    QJsonObject jsonObj = jsonDoc.object();
    if (jsonObj.contains("HostDisplayList") &&
        jsonObj.contains("displayChosen") &&
        jsonObj.contains("PrivacyState") &&
        jsonObj.contains("ForcePrivacyMode")) {
        newStatus.PrivacyState = jsonObj["PrivacyState"].toString() != "Disable";
        newStatus.ForcePrivacyMode = jsonObj["ForcePrivacyMode"].toString() != "Disable";

        auto HostDisplayList = jsonObj.value("HostDisplayList");
        auto displayChosen = jsonObj.value("displayChosen");

        QSet<QString> chosenSet;
        QJsonArray jsonArray = displayChosen.toArray();
        for (const QJsonValue& value : jsonArray) {
            if (value.isString()) {
                QString qstr = value.toString();
                chosenSet.insert(qstr.left(qstr.indexOf("[")));
            }
        }
        newStatus.HostDisplayList = GleamCore::parseDisplayInfo(HostDisplayList);
        for (int i = 0; i < newStatus.HostDisplayList.size(); ++i)
        {
            if (chosenSet.contains(newStatus.HostDisplayList.at(i).shortName))
                newStatus.displayChosen.push_back(i);
        }
    }

    if (jsonObj.contains("Reconnect")) 
        newStatus.Reconnect = jsonObj["Reconnect"].toString() == "True";

    m_StatusMutex.lock();
    if (newStatus != m_Status)
    {
        m_StatusMutex.unlock();
        QJsonDocument jsonDoc = QJsonDocument::fromJson(text.toUtf8());
        newStatus.jsonString = QString(jsonDoc.toJson(QJsonDocument::Compact));
        {
            m_StatusMutex.lock();
            m_Status = std::move(newStatus);
            m_StatusMutex.unlock();
        }
        {
            SDL_Event event;
            event.type = SDL_DISPLAY_CHANGED;
            SDL_PushEvent(&event);
        }
        {
            SDL_Event event;
            event.type = SDL_STATUS_CHANGED;
            SDL_PushEvent(&event);
        }
    }
    else
    {
        m_StatusMutex.unlock();
    }
}

void ConnectionManager::OnDisconnected()
{
    SDL_Event event;
    event.type = SDL_DISCONNECTION_EVENT;
    event.quit.timestamp = SDL_GetTicks();
    SDL_PushEvent(&event);
    // SessionUI::I().getSession()->SendSessionInfoPortal("Disconnection End");
}
int ConnectionManager::OnConnect(NvComputer* computer)
{
    qInfo() << "Processing OnConnect Event";
    if (computer->pairState != NvComputer::PS_PAIRED)
    {
        SDL_Event event;
        event.type = SDL_DISCONNECTION_EVENT;
        SDL_PushEvent(&event);
        return -1;
    }
    auto* pSession = SessionUI::I().getSession();
    pSession->SetComputer(computer);
    // The computer might be released somewhere else, so get it from pSession
    computer = pSession->getNvComputer().get(); 

    auto streamConfig = pSession->getStreamConfig();
    auto* pPreferences = pSession->getStreamingPreferences();
    if (pPreferences == nullptr) {
        SDL_Event event;
        event.type = SDL_DISCONNECTION_EVENT;
        SDL_PushEvent(&event);
        return -1;
    }

    // try reset to 0x0 when match host
    streamConfig.width =  pPreferences->width;
    streamConfig.width2 =  pPreferences->width2;
    streamConfig.height =  pPreferences->height;
    streamConfig.height2 =  pPreferences->height2;

    auto* pApp = FindDesktopApp(computer);
    if(pApp == nullptr)
    {
        //need to update AppList from Host
        bool bChanged = false;
        UpdateAppList(computer, bChanged);
        pApp = FindDesktopApp(computer);
    }

    SERVER_INFORMATION hostInfo;

    bool enableGameOptimizations = false;
    for (const NvDisplayMode& mode : computer->displayModes)
    {
        if (mode.width == streamConfig.width && mode.height == streamConfig.height)
        {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Found host supported resolution: %dx%d",
                mode.width, mode.height);
            enableGameOptimizations = pPreferences->gameOptimizations;
            break;
        }
    }
    int loopCount = 1000;
    enum class StageStatus
    {
        Ok,
        Loop,
        WaitAndTry,
        
    };
    NvHTTP http(computer);
    QString serverInfo;
    QString rtspSessionUrl;
    QString rtspCSessionID;
    QString guuidEncryptText;
    int currentGameId = 0;
    int nStreamNum = 1;
    QString szStatus;

    StageStatus ss = StageStatus::Loop;
    while (loopCount-- > 0) {
        if (!thread_running.load()) {
            return 0;
        }
        switch (ss)
        {
        case StageStatus::WaitAndTry:
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            break;
        default:
            break;
        }
        static int s_iGetInfoOpId = UIStateMachine::I().genOpId();
        static int s_iStartAppOpId = UIStateMachine::I().genOpId();
        try
        {
            serverInfo = http.getServerInfo(NvHTTP::NvLogLevel::NVLL_NONE, true);
        }
        catch (const GfeHttpResponseException& e)
        {
            UIStateMachine::I().push(s_iGetInfoOpId, static_cast<MsgCode>(e.getStatusCode()));
            ss = StageStatus::WaitAndTry;

            if (gConnectionReporter != nullptr)
            {
                QString msg = e.getStatusMessage();
                gConnectionReporter->SetStateFailed(ConnectionReporter::CONNECT_GET_SERVERINFO_FAILED, msg);
            }
            continue;
        }
        catch (const QtNetworkReplyException& e)
        {
            UIStateMachine::I().push(s_iGetInfoOpId, MsgCode::NO_CODE, MsgType::Progress, e.getErrorText());
            ss = StageStatus::WaitAndTry;
            if (gConnectionReporter != nullptr)
            {
                QString msg = e.getErrorText();
                gConnectionReporter->SetStateFailed(ConnectionReporter::CONNECT_GET_SERVERINFO_FAILED, msg);
            }
            continue;
        }
        currentGameId = NvHTTP::getCurrentGame(serverInfo);
        
        try
        {
            http.startApp(currentGameId != 0 ? "resume" : "launch", computer->isNvidiaServerSoftware, pApp->id,
                &streamConfig, enableGameOptimizations, pPreferences->playAudioOnHost,
                pSession->m_InputHandler->getAttachedGamepadMask(), !pPreferences->multiController,
                rtspSessionUrl, rtspCSessionID, m_clientId, m_username, m_watermark, m_userID,guuidEncryptText, szStatus);

            qDebug()<<"szStatus = "<<szStatus;
            //The number of screens returned by the analysis, not yet processed

            if(!szStatus.isEmpty())
            {
                szStatus.replace("\n\t","");
                szStatus.replace("\n","");
                qDebug()<<"str = "<<szStatus;
                QJsonDocument json_doc = QJsonDocument::fromJson(szStatus.toUtf8());
                if (!json_doc.isNull())
                {
                    QJsonObject root_obj = json_doc.object();
                    QJsonArray display_chosen_array = root_obj.value("displayChosen").toArray();
                    nStreamNum = display_chosen_array.size();
                    qDebug() <<"display_chosen_array size "<< nStreamNum;
                }
            }
        }
        catch (const GfeHttpResponseException& e)
        {
            if (e.getStatusCode() == 600) // another client is connected
            {
                // do something if needed
            }
            // skip error a few times if reconnect for user switch
            static int skipCnt = 2, curCnt = 0;
            if (!m_Status.Reconnect || (m_Status.Reconnect && curCnt < skipCnt))
            {
                UIStateMachine::I().push(s_iStartAppOpId, static_cast<MsgCode>(e.getStatusCode()), MsgType::Progress);
                if (gConnectionReporter != nullptr)
                {
                    auto it = MsgMap.find(static_cast<MsgCode>(e.getStatusCode()));
                    QString msg = it != MsgMap.end() ? it->second.c_str() : e.getStatusMessage();
                    gConnectionReporter->SetStateFailed(ConnectionReporter::START_APP_FAILED, msg);
                }

                if (m_Status.Reconnect)
                    ++curCnt;
                else
                    curCnt = 0;
            }
            ss = StageStatus::WaitAndTry;
            continue;
        }
        catch (const QtNetworkReplyException& e)
        {
            if (gConnectionReporter != nullptr)
            {
                QString msg = e.getErrorText();
                gConnectionReporter->SetStateFailed(ConnectionReporter::START_APP_FAILED, msg);
            }

            ss = StageStatus::WaitAndTry;
            continue;
        }
        //last stage, OK and break
        ss = StageStatus::Ok;

        break;
    }
    if (ss != StageStatus::Ok)
    {
        SDL_Event event;
        event.type = SDL_DISCONNECTION_EVENT;
        SDL_PushEvent(&event);
        return -1;
    }
    QByteArray hostnameStr = computer->activeAddress.address().toLatin1();
    QByteArray siAppVersion = computer->appVersion.toLatin1();

    hostInfo.address = hostnameStr.data();
    hostInfo.serverInfoAppVersion = siAppVersion.data();

    // Older GFE versions didn't have this field
    QByteArray siGfeVersion;
    if (!computer->gfeVersion.isEmpty()) {
        siGfeVersion = computer->gfeVersion.toLatin1();
    }
    if (!siGfeVersion.isEmpty()) {
        hostInfo.serverInfoGfeVersion = siGfeVersion.data();
    }

    // Older GFE and Sunshine versions didn't have this field
    QByteArray rtspSessionUrlStr;
    if (!rtspSessionUrl.isEmpty())
    {
        rtspSessionUrlStr = rtspSessionUrl.toLatin1();
        hostInfo.rtspSessionUrl = rtspSessionUrlStr.data();
    }

    if (pPreferences->packetSize != 0)
    {
        // Override default packet size and remote streaming detection
        // NB: Using STREAM_CFG_AUTO will cap our packet size at 1024 for remote hosts.
        //save back to Session's streamConfig
        streamConfig.streamingRemotely = STREAM_CFG_LOCAL;
        streamConfig.packetSize = pPreferences->packetSize;
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Using custom packet size: %d bytes", pPreferences->packetSize);
    }
    else
    {
        // Use 1392 byte video packets by default
        streamConfig.packetSize = 1392;

        // getActiveAddressReachability() does network I/O, so we only attempt to check
        // reachability if we've already contacted the PC successfully.
        switch (computer->getActiveAddressReachability()) {
        case NvComputer::RI_LAN:
            // This address is on-link, so treat it as a local address
            // even if it's not in RFC 1918 space or it's an IPv6 address.
            streamConfig.streamingRemotely = STREAM_CFG_LOCAL;
            break;
        case NvComputer::RI_VPN:
            // It looks like our route to this PC is over a VPN, so cap at 1024 bytes.
            // Treat it as remote even if the target address is in RFC 1918 address space.
            streamConfig.streamingRemotely = STREAM_CFG_REMOTE;
            streamConfig.packetSize = 1024;
            break;
        default:
            // If we don't have reachability info, let Gleam-common-c decide.
            streamConfig.streamingRemotely = STREAM_CFG_AUTO;
            break;
        }
    }

    if (m_clientId.size() > 0 && m_privateKey.size() > 0 && guuidEncryptText.size() == 0)
    {
        qInfo() << "Encrypt version error.";
    }

    if (guuidEncryptText.size() > 0)
    {
        std::string decrypt = guuidEncryptText.toStdString();
        std::vector<std::string> xml_files;
        Stringsplit(decrypt, '|', xml_files);
        qDebug() << "xml_files.size() = " << xml_files.size();
        std::vector<unsigned char> newStr;
        newStr.resize(xml_files.size());
        for (std::vector<std::string>::size_type i = 0; i < xml_files.size(); i++) {
            newStr[i] = atoi(xml_files.at(i).c_str());
        }

        std::vector<unsigned char> dencrypt_vec;
        RsaPriDecrypt(newStr.data(), newStr.size(), m_privateKey, dencrypt_vec);  // private key decrypt
        g_Encrypted_AES_Key.assign(dencrypt_vec.begin(), dencrypt_vec.end());
        qInfo() << "ready LiStartConnection g_Encrypted_AES_Key.size() = " << g_Encrypted_AES_Key.size();
    }

    qDebug() << "RequestComputerInfor: LiStartConnection begin";

    if (!gReconect)
    {
        nStreamNum = 1;
    }


    m_bLiStartConnection.store(false);
    int err = LiStartConnection(nStreamNum, &hostInfo, &streamConfig, &s_ConnCallbacks, &pSession->m_VideoCallbacks,
        pSession->m_AudioDisabled ? nullptr : &pSession->m_AudioCallbacks, NULL, 0, NULL, 0);

    if (err != 0)
    {
        if (gConnectionReporter != nullptr)
        {
            QString msg = "LiStartConnection failed";
            gConnectionReporter->SetStateFailed(ConnectionReporter::START_CONNECTION_FAILED, msg);
        }

        qWarning() << "RequestComputerInfor: LiStartConnection err = " << err;
        SDL_Event event;
        event.type = SDL_DISCONNECTION_EVENT;
        SDL_PushEvent(&event);
        return -1;
    }
    pSession->WakeupVideoDecoders();
    pSession->SetRequestComputerInforData(rtspCSessionID,m_HttpJsonStr,m_Arch);
    pSession->SendSessionInfoPortal();

    if (!szStatus.isEmpty())
    {
        SetStatusByXml(szStatus);
        m_StatusMutex.lock();
        int iCnt = m_Status.HostDisplayList.size();
        m_StatusMutex.unlock();
        if (!iCnt)
        {
            static int s_iOpId = UIStateMachine::I().genOpId();
            UIStateMachine::I().push(s_iOpId, MsgCode::NO_CODE, MsgType::Confirm,
            "There is no display on host, do you want to create a virtual display now?",
            [this, pSession](bool bOk) {
                if (bOk)
                {
                    QString type = "config";
                    QString act = "{\"ActionType\":\"CreateVirtualDisplay\",\"VirtualDisplay\":\"1\"}";
                    createMessageTask(act, type, -1);
                    pSession->setCheckAutoCreateStreaming();
                } else {
                    SDL_Event event;
                    event.type = SDL_QUIT;
                    SDL_PushEvent(&event);
                }
            });
        }
    }
    m_bLiStartConnection.store(true);
    return 0;
}

void ConnectionManager::OnReconnect()
{
    gReconect = true;
    auto* pSession = SessionUI::I().getSession();
    pSession->PauseVideoDecoders();
    LiStopConnection(2);
    pSession->ResetDecoders();

    // If the network is down, this check will not work.
    QJsonDocument retJson;
    NvReqHTTPS reqHttps;
    int nGetAgentDataRet = 0;
    std::string szRetStr = reqHttps.RequestByToken(gQReq, gQToken, retJson, 3000, nGetAgentDataRet,false);
    if (nGetAgentDataRet == QNetworkReply::AuthenticationRequiredError) {
        gQuitMessage = "Portal requires authentication";
        SDL_Event event;
        event.type = SDL_QUIT;
        SDL_PushEvent(&event);
        return;
    }
    ParseArgs(m_clientId.c_str(), m_privateKey.c_str(), m_args);
}
