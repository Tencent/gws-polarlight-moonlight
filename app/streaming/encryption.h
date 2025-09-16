#ifndef ENCRYPTION_H
#define ENCRYPTION_H
#include <QDebug>
#include <string>
#include <vector>

#define PUB_KEY_FILE "_p.pem" // public key
#define PRI_KEY_FILE "_s.pem" // private key
#define UUID_FILE "Gleam.xml"

std::string KeyParse(QString json, QString UIUid, QString Key, QString &httpCurl, QString &token,
                     QJsonDocument &retJson);
std::string ClientResolutionParse(QString json, QString szGuid, QString szFingerprint, QString szAgentid,
                                  QString &httpCurl, QString &token, QJsonDocument &retJson);
//=====================================public private key=========================================================
// crate public praivte key string

void WriteXml(std::string &UUid, std::vector<std::string> &vUrl);
std::string generateRSAKey(std::string strKey[2], std::vector<std::string> &vUrl, bool &bNew);

bool RsaPubEncrypt(const char *clear_text, int size, const std::string &pub_key,
                   std::vector<unsigned char> &outDataStr);
// private dencrypt
bool RsaPriDecrypt(unsigned char *cipher_text, int textSize, const std::string &pri_key,
                   std::vector<unsigned char> &outDataStr);
//=====================================public private key=========================================================

bool aes_encrypt(char *data_str, int size, const char *key, std::vector<char> &outData);
bool aes_decrypt(char *secert, int len, const char *key, std::vector<char> &out);
//=====================================Aes key=========================================================

void Stringsplit(std::string str, const char split, std::vector<std::string> &res);

void ClientConfigParse(QString json, int &Choosecount, int &DisplayMode);

QString csUUID();
QString result(QString cmd);
#endif // ENCRYPTION_H
