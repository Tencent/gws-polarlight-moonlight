#ifndef GWSCLI_H
#define GWSCLI_H
#include <iostream>
#include <regex>
#include <string>
#include <sstream>
#include <vector>
#include <map>

#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QCoreApplication>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QTimer>
#include <QEventLoop>
#include <QDebug>
#include <QList>
#include <QByteArray>
#include <QString>
#include <QRegularExpression>
#include <QThread>
#include <random>
#include "EventReporter.h"

struct Param  {
    std::string Client;
    std::string InstanceId;
    std::string Token;
    std::string Host;
    std::string Finger;
};

// 重载 << 运算符
std::ostream& operator<<(std::ostream& os, const Param& param) {
    os << "Client: " << param.Client << std::endl
       << "InstanceId: " << param.InstanceId << std::endl
       << "Token: " << param.Token << std::endl
       << "Host: " << param.Host << std::endl
       << "Finger: " << param.Finger << std::endl;
    return os;
}

// 重载 << 运算符
QDebug operator<<(QDebug debug, const Param& param) {
    QDebugStateSaver saver(debug);
    debug.nospace() << "Client: " << param.Client << "\n"
                    << "InstanceId: " << param.InstanceId << "\n"
                    << "Token: " << param.Token << "\n"
                    << "Host: " << param.Host << "\n"
                    << "Finger: " << param.Finger << "\n";
    return debug;
}

std::string Protocol = "cesgws:%5C?";
std::string ParamRegex = "client\\$.*#instanceId\\$.*#token\\$.*";

void TrimQuotes(std::string &s)  {
    if (s.size() >= 2) {
        if (s.front() == s.back() && (s.front() == '\"' || s.front() == '\'')) {
            s.erase(0,1);
            s.pop_back();
        }
    }
}
void TrimPrefix(std::string &str, const std::string& prefix) {
    if (str.find(prefix) == 0) {
        str.erase(0, prefix.length());
    }
}

std::vector<std::string> Split(const std::string& str, char delimiter) {
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream tokenStream(str);

    while (std::getline(tokenStream, token, delimiter)) {
        tokens.push_back(token);
    }

    return tokens;
}

std::string ToLower(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    return result;
}

// 函数：将字符串中的 "%23" 替换为 "#"
std::string replacePercent23WithHash(std::string& result) {
    size_t pos = 0;

    // 查找并替换所有的 "%23"
    while ((pos = result.find("%23", pos)) != std::string::npos) {
        result.replace(pos, 3, "#");
        pos += 1; // 移动到替换后的位置
    }

    return result;
}

void AssignParams(Param& param, const std::string& k, const std::string& v) {
    if ("client" == ToLower(k)) {
        param.Client = v;
    } else if ("instanceid" == ToLower(k)) {
        param.InstanceId = v;
    } else if ("token" == ToLower(k)) {
        param.Token = v;
    } else if ("host" == ToLower(k)) {
        param.Host = v;
    } else if ("fingerprint" == ToLower(k)) {
        param.Finger = v;
    } else {
        qWarning() << "String does not match any key." ;
    }
}

std::pair<Param, bool>  ParseParam(std::string arg) {
    replacePercent23WithHash(arg);
    Param p;
    bool ret = false;
    TrimQuotes(arg);
    TrimPrefix(arg,Protocol);
    std::regex pattern(ParamRegex);

    if (std::regex_match(arg, pattern)) {
        qInfo() << "String matches the pattern.";
    } else {
        qCritical() << "String does not match the pattern.";
        return {p, ret};
    }
    std::vector<std::string> params = Split(arg, '#');
    for (std::string& param : params) {
        auto kv = Split(param, '$');
        AssignParams(p, kv[0], kv[1]);
    }
    std::string fixs = "https://";
    TrimPrefix(p.Host, fixs);

    std::string fix = "http://";
    TrimPrefix(p.Host, fix);
    ret = true;
    return {p, ret};
}


void JsonParse(QString json,
               std::string &ip,
               std::string &username,
               std::string &watermark,
               std::string &userID,
               std::pair<std::string,std::string> &RequiredClientVersion,
               std::map<std::string,std::string>&vTcp,
               std::map<std::string,std::string>&vUdp,
               int &bitrateLimit,
               TimeoutAction &timeoutAction) {
    QJsonParseError jsonerror;
    QJsonDocument doc = QJsonDocument::fromJson(json.toLatin1(), &jsonerror);
    if (!doc.isNull() && jsonerror.error == QJsonParseError::NoError) {
        if (doc.isObject()) {
            QJsonObject object = doc.object();
            if (object.contains("minRequiredClientVersion") && object["minRequiredClientVersion"].isString()) {
                RequiredClientVersion.first = object.value("minRequiredClientVersion").toString().toStdString();
            }

            if (object.contains("maxRequiredClientVersion") && object["maxRequiredClientVersion"].isString()) {
                RequiredClientVersion.second = object.value("maxRequiredClientVersion").toString().toStdString();
            }

            if (object.contains("ip") && object["ip"].isString()) {
                ip = object.value("ip").toString().toStdString();
            }

            if (object.contains("username") && object["username"].isString()) {
                username = object.value("username").toString().toStdString();
            }

            if (object.contains("watermark") && object["watermark"].isBool()) {
                if (object.value("watermark").toBool()) {
                    watermark = "Enable";
                }
            }

            if (object.contains("userId") && object["userId"].isString()) {
                userID = object.value("userId").toString().toStdString();
            }

            if (object.contains("bitrateCap")) {
                bitrateLimit = object.value("bitrateCap").toInt();
            }

            if (object.contains("clientIdleDisconnectMin")) {
                timeoutAction.timeoutSec = object.value("clientIdleDisconnectMin").toInt() * 60;
            }

            if (object.contains("clientIdleTimeoutAction")) {
                timeoutAction.action = object.value("clientIdleTimeoutAction").toString();
            }

            if (object.contains("proxies") && object["proxies"].isArray()) {
                QJsonArray proxyObj  = object["proxies"].toArray();
                for (const QJsonValue &proxyValue : proxyObj)
                {
                    QJsonObject proxyObject = proxyValue.toObject();
                    if(proxyObject["port"].toString().size()>0 && proxyObject["mapping_port"].toString().size() > 0)
                    {
                        if(proxyObject["protocol"].toString() == "TCP")
                        {
                            vTcp.insert(std::pair(proxyObject["port"].toString().toStdString(), 
                                proxyObject["mapping_port"].toString().toStdString()));
                        }
                        else   if(proxyObject["protocol"].toString() == "UDP")
                        {
                            vUdp.insert(std::pair(proxyObject["port"].toString().toStdString(),
                                proxyObject["mapping_port"].toString().toStdString()));
                        }
                    }
                }

            }
        }
    }

}

struct SessionParam {
    QString endpoint;
    QString token;
    QString clientVersion;
    QString clientIP;

    // Method to convert the struct to a JSON string
    QString toJson() const {
        QJsonObject jsonObject;
        jsonObject["endpoint"] = endpoint;
        jsonObject["token"] = token;

        // Create nested JSON for Body
        QJsonObject attributesObject;
        attributesObject["clientVersion"] = clientVersion;
        attributesObject["clientIP"] = clientIP;

        QJsonObject bodyObject;
        bodyObject["attributes"] = attributesObject;

        jsonObject["body"] = bodyObject;

        // Convert to JSON document and then to string
        QJsonDocument jsonDoc(jsonObject);
        return jsonDoc.toJson(QJsonDocument::Compact);
    }
};
std::vector<int> splitVersion(const std::string& version) {
    std::vector<int> versionParts;
    std::stringstream ss(version);
    std::string part;

    while (std::getline(ss, part, '.')) {
        versionParts.push_back(std::stoi(part));
    }

    return versionParts;
}

int checkVersions(const std::pair<std::string,std::string>& requiredVersion, const std::string& version) {
    std::vector<int> verMin = splitVersion(requiredVersion.first);
    std::vector<int> verMax = splitVersion(requiredVersion.second);
    std::vector<int> ver = splitVersion(version);
    return (verMin.empty() ? true : verMin <= ver) &&
           (verMax.empty() ? true : ver <= verMax);
}

std::string GeneratePinString() {
    std::uniform_int_distribution<int> dist(1111, 9999);
    std::random_device rd;
    std::mt19937 engine(rd());
    int randomNumber = dist(engine); // Generate the random number
    return std::to_string(randomNumber); // Convert to string
}

bool isPortOpen(const QString &ip, int port) {
    int retryTime = 3;
    while (retryTime--) {
        QTcpSocket socket;
        socket.connectToHost(ip, port);
        if (socket.waitForConnected(2000)) {
            socket.disconnectFromHost();
            return true; // Port is open
        }
        QThread::msleep(100);//阻塞延时100ms
    }
    return false; // Port is closed
}

#endif // GWSCLI_H
