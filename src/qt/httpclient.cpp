//
// Created by tri on 8/7/24.
//

#include <qt/httpclient.h>

#include <QNetworkReply>
#include <QNetworkRequest>
#include <util/system.h>

HttpClient::HttpClient(QObject* parent)  : QObject(parent) {
    manager = new QNetworkAccessManager(this);
    //connect(manager, &QNetworkAccessManager::finished, this, &HttpClient::onFinished);
}

HttpClient::~HttpClient() {
    delete manager;
}

void HttpClient::sendGetRequest(const std::string& cid, std::function<void(const QByteArray&)> callback) {
    std::string downUrl =  gArgs.GetArg("-ipfsservice", DEFAULT_IPFS_SERVICE_URL) + "get/" + cid;
    QNetworkRequest request(QUrl(QString::fromStdString(downUrl)));
    QNetworkReply* reply = manager->get(request);
    connect(reply, &QNetworkReply::finished, this, [callback, reply]() {
        if (reply->error() == QNetworkReply::NoError) {
            QByteArray responseData = reply->readAll();
            callback(responseData);
        } else {
            callback(QByteArray::fromStdString(""));
        }
        reply->deleteLater();
    });
}

void HttpClient::sendFileRequest(const QUrl& url, const QString& filePath) {

}