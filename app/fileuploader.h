#ifndef FILEUPLOADER_H
#define FILEUPLOADER_H

#include <QApplication>
#include <QFileDialog>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QHttpMultiPart>
#include <QProgressDialog>
#include <QVBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QFileInfo>
#include <QtCore/private/qzipwriter_p.h>
#include <QTemporaryDir>

class FileUploader : public QObject {
    Q_OBJECT
public:
    FileUploader(QObject *parent = nullptr) : QObject(parent) {
        m_manager = new QNetworkAccessManager(this);
    }
    ~FileUploader() {
        delete m_manager;
    }
    bool addFileToZip(QZipWriter &zipWriter, const QString &filePath) {
        QFile file(filePath);
        if (file.open(QIODevice::ReadOnly)) {
            zipWriter.addFile(QFileInfo(file.fileName()).fileName(), file.readAll());
            file.close();
            qInfo() << "Added file to zip:" << filePath;
        } else {
            qWarning() << "Failed to open file for reading:" << filePath;
            return false;
        }
        return true;
    }

    bool compressFilesToZip(const QStringList &filePaths, const QString &zipPath) {
        QFile zipFile(zipPath);
        if (!zipFile.open(QIODevice::WriteOnly)) {
            qWarning() << "Cannot open zip file for writing:" << zipPath;
            return false;
        }

        QZipWriter zipWriter(&zipFile);
        zipWriter.setCompressionPolicy(QZipWriter::AutoCompress);

        bool res = false;
        for (auto& filePath : filePaths) {
            if (addFileToZip(zipWriter, filePath) && !res) {
                res = true;
            }
        }

        zipWriter.close();
        zipFile.close();
        return res;
    }

    void uploadFiles(const QStringList &filePaths, const QString &uploadUrl,const QString &token) {
        // 创建进度对话框
        m_progressDialog = new QProgressDialog("Compressing files...", "Cancel", 0, 0, nullptr);
        m_progressDialog->setWindowTitle("File Upload");
        m_progressDialog->setMinimumDuration(0);
        m_progressDialog->setWindowModality(Qt::WindowModal);
        m_progressDialog->setWindowFlags(m_progressDialog->windowFlags() |
                                         Qt::WindowStaysOnTopHint);
        m_progressDialog->setCancelButton(nullptr);  // 隐藏默认取消按钮
        m_progressDialog->show();

        // 创建临时目录（自动删除）
        QTemporaryDir tempDir;
        if (!tempDir.isValid()) {
            qWarning() << "Failed to create temporary directory";
            emit uploadError("Failed to create temporary directory");
            return;
        }

        // 定义压缩文件在临时目录中的路径
        QString zipPath = tempDir.filePath("client_log.zip");

        // 压缩文件到临时目录
        if (!compressFilesToZip(filePaths, zipPath)) {
            emit uploadError("Failed to compress files");
        }

        m_progressDialog->setLabelText("Uploading files...");
        // 创建多部分请求容器（自动管理内存）
        QHttpMultiPart *multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);
        // 2.文件路径，添加为独立 part
        {
            QFile *file = new QFile(zipPath);
            if (!file->open(QIODevice::ReadOnly)) {
                qWarning() << "Failed to open file:" << zipPath;
                emit uploadError("Failed to open file: " + zipPath);
            }

            QHttpPart filePart;
            // 使用 QFileInfo 提取文件名
            QFileInfo fileInfo(file->fileName());
            QString cleanFileName = fileInfo.fileName();
            // 设置 Content-Disposition，name 为 multipartFiles，filename 为实际文件名
            filePart.setHeader(QNetworkRequest::ContentDispositionHeader,
                QVariant(QString("form-data; name=\"multipartFiles\"; filename=\"%1\"").arg(cleanFileName)));
            // 设置文件内容
            filePart.setBodyDevice(file);
            multiPart->append(filePart);

            // 注意：QHttpMultiPart 会接管 file 对象的所有权，无需手动删除
        }

        // 3. 发送POST请求
        QNetworkRequest request(uploadUrl);
        std::string Bearer = "Bearer ";
        Bearer += token.toStdString();
        request.setRawHeader("Authorization", Bearer.c_str());
        m_reply = m_manager->post(request, multiPart);

        multiPart->setParent(m_reply); // 确保multiPart随reply销毁
        connect(m_manager, &QNetworkAccessManager::finished, this, &FileUploader::handleUploadResult);

        QEventLoop eventLoop(this);
        connect(m_reply, &QNetworkReply::finished, &eventLoop, &QEventLoop::quit);
        connect(this, &FileUploader::uploadFinished, &eventLoop, &QEventLoop::quit);
        connect(this, &FileUploader::uploadError, &eventLoop, &QEventLoop::quit);

        eventLoop.exec(); //事件循环等待
    }

signals:
    void uploadFinished(bool success, const QString &response);
    void uploadError(const QString &error);

private slots:

    void handleUploadResult(QNetworkReply *reply) {
        m_progressDialog->close();
        delete m_progressDialog;
        m_progressDialog = nullptr;
        QString response = reply->readAll();
        qDebug() << "Status Code:" << m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        qDebug() << "Response:" << response;
        if (reply->error() == QNetworkReply::NoError) {
            emit uploadFinished(true, response);
        } else {
            emit uploadError(m_reply->errorString() + "\n" + response);
        }
    }

private:
    QNetworkAccessManager *m_manager = nullptr;
    QNetworkReply *m_reply = nullptr;
    QProgressDialog *m_progressDialog = nullptr;
};

#endif // FILEUPLOADER_H
