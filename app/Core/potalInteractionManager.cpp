#include "potalInteractionManager.h"
void CPortalInteractionManager::PortalSessionInfo(std::string & clientId,std::string &HttpJsonStr,
                                                std::string & ClientOS,std::string &ClientArch,std::string & message)
{
    if(clientId.size() > 0 && HttpJsonStr.size() > 0)
    {
        QString szUrl;
        QString sztoken;
        QJsonDocument Json;
        std::string body = ParseJson(HttpJsonStr.c_str(), clientId, szUrl, sztoken, Json,ClientOS,ClientArch,message);
        if (szUrl.size() > 0 && sztoken.size() > 0)
        {
            qDebug()<<"CPortalInteractionManage::PortalSessionInfo post session info "<< clientId
                    <<"to portal! message = " <<message;
            NvReqHTTPS m_ReqHttps;
            try {
                int nRet = 0;
                m_ReqHttps.RequestByToken(szUrl, sztoken, Json,3000, nRet);
                if(0 == nRet)
                {
                    qInfo()<<"CPortalInteractionManage::PortalSessionInfo Sucess.";
                }
                else
                {
                    qWarning()<<"CPortalInteractionManage::PortalSessionInfo error, try again! nRet =  "<<nRet;
                    CPortalInteractionManager::I().pushEvent(new CPortalSessionEvent(clientId,HttpJsonStr, ClientOS,ClientArch,message));
                }
            }
            catch (const GfeHttpResponseException& e) {
                qWarning() << "CPortalInteractionManage::PortalSessionInfo error! " << e.toQString();
            }
            catch (const QtNetworkReplyException& e) {
                qWarning() << "CPortalInteractionManage::PortalSessionInfo error! " << e.toQString();
            }

        }
    }
}

std::string ParseJson(QString json, std::string & clientId, QString &httpCurl, QString &token,
                      QJsonDocument &retJson, std::string &ClientOS, std::string & ClientArch,std::string & message) {
    QJsonParseError jsonerror;
    QJsonDocument doc = QJsonDocument::fromJson(json.toLatin1(), &jsonerror);
    QString clientVersion, clientIP;
    if (!doc.isNull() && jsonerror.error == QJsonParseError::NoError) {
        if (doc.isObject()) {
            QJsonObject object = doc.object();
            if (object.contains("endpoint") && object["endpoint"].isString()) {
                httpCurl = object.value("endpoint").toString();
            }
            if (object.contains("token") && object["token"].isString()) {
                token = object.value("token").toString();
            }
            if (object.contains("body")) {
                if (object["body"].isObject()) {
                    QJsonObject chidren_obj = object.value("body").toObject();
                    if (chidren_obj.contains("attributes") && chidren_obj["attributes"].isObject()) {
                            QJsonObject obj = chidren_obj.value("attributes").toObject();
                            if (obj.contains("clientVersion")) {
                                clientVersion = obj.value("clientVersion").isString() ?
                                                obj.value("clientVersion").toString() : "";
                            }
                            if (obj.contains("clientIP")) {
                                clientIP = obj.value("clientIP").isString() ?
                                                    obj.value("clientIP").toString() : "";
                            }
                    }
                }
            }
        }
    }

    QJsonObject attributes;
    QJsonObject subObj;
    subObj.insert("clientVersion", clientVersion);
    subObj.insert("clientIP", clientIP);
    subObj.insert("clientOS", ClientOS.c_str());
    subObj.insert("clientArch", ClientArch.c_str());
    QJsonObject sessionid;
    sessionid.insert("clientId", clientId.c_str());
    sessionid.insert("attributes", subObj);
    sessionid.insert("message", message.c_str());
    QJsonDocument newdoc(sessionid);
    retJson = newdoc;
    QByteArray newjson = newdoc.toJson();
    return newjson.toStdString();
}







