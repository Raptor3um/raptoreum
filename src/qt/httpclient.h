//
// Created by tri on 8/7/24.
//

#ifndef RAPTOREUM_HTTPCLIENT_H
#define RAPTOREUM_HTTPCLIENT_H

#include <QObject>
#include <QUrl>
#include <QString>
#include <QByteArray>
#include <QNetworkAccessManager>
#include <string>

namespace Ui {
    class HttpClient;
}

QT_BEGIN_NAMESPACE
class QNetworkAccessManager;
QT_END_NAMESPACE

class HttpClient : public QObject {
    Q_OBJECT

public:
    explicit HttpClient(QObject* parent = nullptr);
    ~HttpClient();
    void sendFileRequest(const QUrl& url, const QString& filePath);
    void sendGetRequest(const std::string& cid, std::function<void(const QByteArray&)> callback);
//private slots:
//    void onFinished(QNetworkReply* reply);

private:
    QNetworkAccessManager* manager;
};

#endif //RAPTOREUM_HTTPCLIENT_H
