
#include "openssl/pem.h"
#include "openssl/rsa.h"
#include "streaming/encryption.h"

#include "openssl/aes.h"
#include "openssl/md5.h"
#include "openssl/sha.h"

#include <QUuid>
#include <iostream>


#include <QDir>
#include <QFile>
#include <QXmlStreamWriter>


#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QProcess>
#include <QScreen>
#include <QStandardPaths>
#include <iosfwd>
#include <sstream>

using namespace std;

#define KEY_LENGTH 1024

QString result(QString cmd) {
    QProcess wmic;
    wmic.start("cmd", QStringList() << "/c" << cmd);
    wmic.waitForFinished();
    wmic.readLine();
    QString ret = wmic.readLine().simplified();
    wmic.close();
    return ret;
}

QString csUUID() {
    QString cmd = "wmic CSPRODUCT get uuid";
    return result(cmd);
}

std::string KeyParse(QString json, QString UIUid, QString Key, QString &httpCurl, QString &token,
                     QJsonDocument &retJson) {
    QJsonParseError jsonerror;
    QJsonDocument doc = QJsonDocument::fromJson(json.toLatin1(), &jsonerror);
    if (!doc.isNull() && jsonerror.error == QJsonParseError::NoError) {
        if (doc.isObject()) {
            QJsonObject object = doc.object();
            if (object.contains("endpoint") && object["endpoint"].isString()) {
                QJsonValue value = object.value("endpoint");
                httpCurl = value.toString();
            }
            if (object.contains("token") && object["token"].isString()) {
                QJsonValue value = object.value("token");
                token = value.toString();
            }
        }
    }
    QJsonObject attributes;
    QJsonObject sessionid;
    sessionid.insert("guid", UIUid);
    sessionid.insert("publicKey", Key);

    QJsonDocument newdoc(sessionid);
    retJson = newdoc;

    QByteArray newjson = newdoc.toJson();
    qDebug() << "newjson = " << newdoc.toJson();

    return newjson.toStdString();
}

std::string ClientResolutionParse(QString json, QString szGuid, QString szFingerprint, QString szAgentid,
                                  QString &httpCurl, QString &token, QJsonDocument &retJson) {
    QJsonParseError jsonerror;
    QJsonDocument doc = QJsonDocument::fromJson(json.toLatin1(), &jsonerror);
    if (!doc.isNull() && jsonerror.error == QJsonParseError::NoError) {
        if (doc.isObject()) {
            QJsonObject object = doc.object();
            if (object.contains("endpoint") && object["endpoint"].isString()) {
                QJsonValue value = object.value("endpoint");
                httpCurl = value.toString();
            }
            if (object.contains("token") && object["token"].isString()) {
                QJsonValue value = object.value("token");
                token = value.toString();
            }
        }
    }

    QJsonObject interestObj;
    // 插入元素，对应键值对
    interestObj.insert("agentId", szAgentid);
    interestObj.insert("guid", szGuid);
    interestObj.insert("fingerprint", szFingerprint);

    QJsonObject sessionid;
    QJsonArray arr_phone;
    const int baseValue = 96;
    QList<QScreen *> list_screen = QGuiApplication::screens();
    // 通过循环可以遍历每个显示器
    for (int i = 0; i < list_screen.size(); i++) {
        QString szName = "dispay";
        QRect rect = list_screen.at(i)->geometry();
        double fps = list_screen.at(i)->refreshRate();
        // 100%时为96
        qreal dpiVal = list_screen.at(i)->logicalDotsPerInch();
        qreal ratioVal = list_screen.at(i)->devicePixelRatio();
        double dMul = dpiVal * ratioVal / baseValue;
        int desktop_width = rect.width() * dMul;
        int desktop_height = rect.height() * dMul;
        QString szStr = QString::number(desktop_width) + "*" + QString::number(desktop_height);
        szName.append(QString::number(i + 1));
        sessionid.insert("name", szName);
        sessionid.insert("resolution", szStr);
        sessionid.insert("fps", fps);
        arr_phone.append(sessionid);
    }

    QJsonObject chidren;
    chidren.insert("displays", arr_phone);

    interestObj.insert("clientConfig", chidren);
    QJsonDocument newdoc(interestObj);

    retJson = newdoc;
    qDebug() << "newjson = " << newdoc.toJson();
    return newdoc.toJson().toStdString();
}

void WriteXml(std::string &UUid, std::vector<std::string> &vUrl) {
    std::string szConfPath = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation).toStdString();
    szConfPath.append("/GWS/Client/Data");;


    std::string uudidPath = szConfPath + "/" + UUID_FILE;
    QFile uuidfile(uudidPath.c_str());

    if (uuidfile.exists()) {
        uuidfile.remove();
    }
    uuidfile.close();
    if (!uuidfile.open(QIODevice::ReadWrite)) {
        qWarning() << "uuidfile open the file for writing.";
        return;
    }
    QXmlStreamWriter xml(&uuidfile);
    xml.setAutoFormatting(true);
    xml.writeStartDocument();
    xml.writeStartElement("Gleam");
    xml.writeTextElement("UUID", UUid.c_str());
    for (std::vector<std::string>::size_type i = 0; i < vUrl.size(); ++i) {
        xml.writeTextElement("url", vUrl[i].c_str());
    }

    xml.writeEndDocument();
    uuidfile.close();
}
const char *AESkey = "Gleam1346798";
bool tesRSAKey(std::string strKey[2], QFile& public_file, QFile& privete_file) {
    static std::string szTestStr = "1234567890123456";
    qDebug() << "open public privete pem file.";
    if (!public_file.open(QIODevice::ReadWrite)) {
        qWarning() << "read public key error.";
        return false;
    }
    if (!privete_file.open(QIODevice::ReadWrite)) {
        qWarning() << "read privae key error.";
        return false;
    }
    QByteArray publicKeyStr = public_file.readAll();
    QByteArray priveteKeystr = privete_file.readAll();

    std::vector<char> vecPub, vecPri;
    bool bpubAesFlag = aes_decrypt(publicKeyStr.data(), publicKeyStr.size(), AESkey, vecPub);
    bool bpriAesFlag = aes_decrypt(priveteKeystr.data(), priveteKeystr.size(), AESkey, vecPri);
    if (!bpubAesFlag || !bpriAesFlag) {
        qWarning() << "public private key AesKey dencrypt error!";
        return "";
    }
    strKey[0] = string(vecPub.data(), vecPub.size());
    strKey[1] = string(vecPri.data(), vecPri.size());

    std::vector<unsigned char> encrypt_vec;
    bool rasflag = RsaPubEncrypt((char *)szTestStr.c_str(), szTestStr.size(), strKey[0], encrypt_vec);

    std::vector<unsigned char> dencrypt_vec;
    rasflag = RsaPriDecrypt((unsigned char *)encrypt_vec.data(), encrypt_vec.size(), strKey[1], dencrypt_vec);
    std::string szDest((char *)dencrypt_vec.data(), dencrypt_vec.size());
    return rasflag && szTestStr == szDest;
}

std::string generateRSAKey(std::string strKey[2], std::vector<std::string> &vUrl, bool &bNew) {
    bool NewCreate = false;
/*
#if defined(Q_OS_WIN32)
    std::string szConfPath = "C:/Gws/Gleam/Data";
#else // macos
    std::string szConfPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation).toStdString();
#endif
*/
    std::string szConfPath = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation).toStdString();
    szConfPath.append("/GWS/Client/Data");

    qDebug() << "szConfPath =" << szConfPath;
    QDir folder;
    bool exist = folder.exists(szConfPath.c_str());
    if (!exist) {
        bool ok = folder.mkpath(szConfPath.c_str());
        if (!ok) {
            qWarning() << "create  " << szConfPath << "error!";
            return "";
        } else {
            qInfo() << "create  " << szConfPath << "sucess!";
        }
    }
    std::string uudidPath = szConfPath + "/" + UUID_FILE;
    std::string publicKeyPath = szConfPath + "/" + PUB_KEY_FILE;
    std::string privateKeyPath = szConfPath + "/" + PRI_KEY_FILE;

    QFile uuidfile(uudidPath.c_str());
    QFile public_file(publicKeyPath.c_str());
    QFile privete_file(privateKeyPath.c_str());
    std::string uuidStr;

    if (!public_file.exists() || !privete_file.exists() || !uuidfile.exists()) {
        NewCreate = true;
    }

    if (NewCreate && uuidfile.exists()) {
        uuidfile.remove();
        qInfo() << "remove uuid xml file.";
    }

    if (uuidfile.exists()) // find
    {
        if (!uuidfile.open(QIODevice::ReadWrite | QIODevice::Text)) {
            qWarning() << "uuid xml file open the file for writing.";
            return std::string();
        }

        QXmlStreamReader reader(&uuidfile);
        while (!reader.atEnd()) {
            reader.readNextStartElement();
            std::string nodename = reader.name().toString().toStdString();
            if (nodename == "UUID") {
                uuidStr = reader.readElementText().toStdString();
            } else if (nodename == "url") {
                vUrl.push_back(reader.readElementText().toStdString());
            }
            reader.readNext();
        }
        if (reader.hasError()) {
            qWarning() << reader.errorString();
        }
        if (uuidStr.size() != 32) {
            uuidfile.remove();

            NewCreate = true;
        }
    } else {
        NewCreate = true;
    }
    if (NewCreate == false && public_file.exists() && privete_file.exists()) {
        if (tesRSAKey(strKey, public_file, privete_file)) {
            public_file.close();
            privete_file.close();
            return uuidStr;
        } else {
            NewCreate = true;
        }
    }
    bNew = NewCreate;
    if (bNew) {
        uuidfile.close();
        if (!uuidfile.open(QIODevice::ReadWrite)) {
            qWarning() << "uuidfile open the file for writing.";
            return std::string();
        }
        QUuid quu = QUuid::createUuid();
        uuidStr = quu.toString(QUuid::WithoutBraces).toStdString();
        uuidStr.erase(std::remove_if(uuidStr.begin(), uuidStr.end(), [](char ch) { return ch == '-'; }), uuidStr.end());

        QXmlStreamWriter xml(&uuidfile);
        xml.setAutoFormatting(true);
        xml.writeStartDocument();
        xml.writeStartElement("Gleam");
        xml.writeTextElement("UUID", uuidStr.c_str());
        xml.writeEndDocument();
        uuidfile.close();
        vUrl.clear();

        if (public_file.exists()) {
            public_file.remove();
            qInfo() << "remove public  pem.";
        }
        if (privete_file.exists()) {
            privete_file.remove();
            qInfo() << "remove privete pem.";
        }
    } else if (!bNew) {
        if (public_file.exists() && privete_file.exists()) {

            if (tesRSAKey(strKey, public_file, privete_file)) {
                public_file.close();
                privete_file.close();
                return uuidStr;
            }
        }
    }

    size_t pri_len;
    size_t pub_len;
    char *pri_key = NULL;
    char *pub_key = NULL;
    RSA *keypair = RSA_generate_key(KEY_LENGTH, RSA_F4, NULL, NULL);
    BIO *pri = BIO_new(BIO_s_mem());
    BIO *pub = BIO_new(BIO_s_mem());
    PEM_write_bio_RSAPrivateKey(pri, keypair, NULL, NULL, 0, NULL, NULL);
    PEM_write_bio_RSAPublicKey(pub, keypair);
    pri_len = BIO_pending(pri);
    pub_len = BIO_pending(pub);
    pri_key = (char *)malloc(pri_len + 1);
    pub_key = (char *)malloc(pub_len + 1);
    BIO_read(pri, pri_key, pri_len);
    BIO_read(pub, pub_key, pub_len);
    pri_key[pri_len] = '\0';
    pub_key[pub_len] = '\0';
    strKey[0] = pub_key;
    strKey[1] = pri_key;
    if (!public_file.open(QIODevice::ReadWrite)) {
        qWarning() << "public key file Failed to open the file for writing.";
        return "";
    }
    if (!privete_file.open(QIODevice::ReadWrite)) {
        qWarning() << "private key file Failed to open the file for writing.";
        return "";
    }

    std::vector<char> vecPub, vecPri;
    bool bpubAesFlag = aes_encrypt(pub_key, pub_len, AESkey, vecPub);
    bool bpriAesFlag = aes_encrypt(pri_key, pri_len, AESkey, vecPri);
    if (!bpubAesFlag || !bpriAesFlag) {
        qWarning() << "public private key AesKey encrypt error!";
        return "";
    }
    public_file.write(vecPub.data(), vecPub.size());
    privete_file.write(vecPri.data(), vecPri.size());
    public_file.close();
    privete_file.close();
    // free memcpy
    RSA_free(keypair);
    BIO_free_all(pub);
    BIO_free_all(pri);
    free(pri_key);
    free(pub_key);
    qInfo() << "create pub private pem sucess!";
    return uuidStr;
}

// public key encrypt
bool RsaPubEncrypt(const char *clear_text, int size, const std::string &pub_key,
                   std::vector<unsigned char> &outDataStr) {
    BIO *keybio = BIO_new_mem_buf((unsigned char *)pub_key.c_str(), -1);
    RSA *rsa = RSA_new();
    if (rsa == NULL) {
        return false;
    }
    rsa = PEM_read_bio_RSAPublicKey(keybio, &rsa, NULL, NULL);
    if (rsa == NULL) {
        return false;
    }
    // 获取RSA单次可以处理的数据块的最大长度
    int key_len = RSA_size(rsa);
    int block_len = key_len - 11; // 因为填充方式为RSA_PKCS1_PADDING
    static char stemp[128] = {0};
    char *sub_text = stemp;

    bool bNewFlag = false;
    if (key_len > 128) {
        sub_text = new char[key_len];
        memset(sub_text, 0, key_len);
        bNewFlag = true;
    }

    int ret = 0;
    int pos = 0;
    std::string sub_str;
    const char *pstr = NULL;
    // 对数据进行分段加密（返回值是加密后数据的长度）
    std::vector<unsigned char> vecTemp;
    while (pos < size) {
        pstr = clear_text + pos; // clear_text.substr(pos, block_len);

        memset(sub_text, 0, key_len);
        if ((size - pos) >= block_len) {
            ret = RSA_public_encrypt(block_len, (const unsigned char *)pstr, (unsigned char *)sub_text,
                    rsa, RSA_PKCS1_OAEP_PADDING);        //RSA_PKCS1_PADDING
        } else {
            block_len = (size - pos);
            ret = RSA_public_encrypt(block_len, (const unsigned char *)pstr, (unsigned char *)sub_text,
                    rsa, RSA_PKCS1_OAEP_PADDING);
        }

        if (ret >= 0) {
            vecTemp.resize(ret);
            memcpy(vecTemp.data(), sub_text, ret);
            outDataStr.insert(outDataStr.end(), vecTemp.begin(), vecTemp.end());

        } else {
            break;
        }
        pos += block_len;
    }
    if (bNewFlag) {
        delete[] sub_text;
    }
    // 释放内存
    BIO_free_all(keybio);
    RSA_free(rsa);
    return true;
}

// private decrypt
bool RsaPriDecrypt(unsigned char *cipher_text, int textSize, const std::string &pri_key,
                   std::vector<unsigned char> &outDataStr) {
    RSA *rsa = RSA_new();
    BIO *keybio;
    keybio = BIO_new_mem_buf((unsigned char *)pri_key.c_str(), -1);

    rsa = PEM_read_bio_RSAPrivateKey(keybio, &rsa, NULL, NULL);
    if (rsa == nullptr) {
        return false;
    }

    // rsa max length
    int key_len = RSA_size(rsa);
    static char stemp[128] = {0};
    char *sub_text = stemp;
    bool bNewFlag = false;
    if (key_len > 128) {
        sub_text = new char[key_len];
        bNewFlag = true;
    }

    int ret = 0;
    unsigned char *sub_str;
    int pos = 0;
    int desLen = key_len;
    std::vector<unsigned char> vecTemp;
    //
    while (pos < textSize) {

        sub_str = &cipher_text[pos];
        memset(sub_text, 0, key_len);
        ret = RSA_private_decrypt(desLen, sub_str, (unsigned char *)sub_text, rsa, RSA_PKCS1_OAEP_PADDING);
        if (ret >= 0) {
            vecTemp.resize(ret);
            memcpy(vecTemp.data(), sub_text, ret);
            outDataStr.insert(outDataStr.end(), vecTemp.begin(), vecTemp.end());
            pos += desLen;
            desLen = textSize - pos;
            if (desLen > key_len) {
                desLen = key_len;
            }
        } else {
            break;
        }
    }

    if (bNewFlag) {
        delete[] sub_text;
    }
    BIO_free_all(keybio);
    RSA_free(rsa);
    return true;
}

/*=====================================AES key=========================================================*/

bool aes_encrypt(char *data_str, int size, const char *key, std::vector<char> &outData) {

    const unsigned char *pt = (const unsigned char *)data_str;

    AES_KEY encKey;

    AES_set_encrypt_key((const unsigned char *)key, 128, &encKey); // 一块为128位，16字节。32字节 256

    unsigned char ivec[16] = "1234567890ABCDE";
    ivec[15] = 'F';
    int length = 0;
    if (size % 16 != 0) {
        length = ((size / 16) + 1) * 16;
    } else {
        length = size;
    }
    outData.resize(length);

    AES_cbc_encrypt((const unsigned char *)pt, (unsigned char *)outData.data(), length, &encKey, ivec, AES_ENCRYPT);
    return true;
}
bool aes_decrypt(char *secert, int len, const char *key, std::vector<char> &out) {

    int length = len;
    out.resize(length);

    AES_KEY deckey;
    unsigned char ivec[16] = "1234567890ABCDE";
    ivec[15] = 'F';

    AES_set_decrypt_key((const unsigned char *)key, 128, &deckey);
    AES_cbc_encrypt((const unsigned char *)secert, (unsigned char *)out.data(), length, &deckey, ivec, AES_DECRYPT);

    return true;
}

void Stringsplit(std::string str, const char split, std::vector<std::string> &res) {
    std::istringstream iss(str);
    std::string token;
    while (std::getline(iss, token, split)) {
        res.push_back(token);
    }
}

void ClientConfigParse(QString json, int &Choosecount, int &DisplayMode) {
    QJsonParseError jsonerror;
    QJsonDocument doc = QJsonDocument::fromJson(json.toLatin1(), &jsonerror);
    QString clientVersion, clientIP;
    if (!doc.isNull() && jsonerror.error == QJsonParseError::NoError) {
        if (doc.isObject()) {
            QJsonObject object = doc.object();
            if (object.contains("choosecount")) {
                QJsonValue value = object.value("choosecount");
                Choosecount = value.toInt();
            }
            if (object.contains("displayMode")) {
                QJsonValue value = object.value("displayMode");
                std::string szStr = value.toString().toStdString();
                DisplayMode = stoi(szStr);
            }
        }
    }
}
