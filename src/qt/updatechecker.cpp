#include <qt/updatechecker.h>
#include <clientversion.h>

#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QUrl>
#include <QNetworkRequest>
#include <QDesktopServices>

UpdateChecker::UpdateChecker(QObject *parent) : QObject(parent) {
    manager = new QNetworkAccessManager(this);
}

void UpdateChecker::checkForUpdates() {
    // GitHub API endpoint for the latest stable release
    QUrl url("https://api.github.com/repos/Raptor3um/raptoreum/releases/latest");
    QNetworkRequest request(url);
    
    // GitHub requires a User-Agent header for API requests
    request.setHeader(QNetworkRequest::UserAgentHeader, "Raptoreum-Update-Checker");

    connect(manager, &QNetworkAccessManager::finished, this, &UpdateChecker::handleResponse);
    manager->get(request);
}

void UpdateChecker::handleResponse(QNetworkReply* reply) {
    // Error handling: if the API is unreachable or the user is offline, fail silently
    if (reply->error() != QNetworkReply::NoError) {
        reply->deleteLater();
        return;
    }

    // Check HTTP status code (should be 200 OK)
    int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (statusCode != 200) {
        reply->deleteLater();
        return;
    }

    // Parse JSON response
    QByteArray response = reply->readAll();
    QJsonDocument jsonDoc = QJsonDocument::fromJson(response);
    if (jsonDoc.isNull() || !jsonDoc.isObject()) {
        reply->deleteLater();
        return;
    }

    QJsonObject jsonObj = jsonDoc.object();
    QString remoteTag = jsonObj["tag_name"].toString(); // Expected format: "v1.2.3" or "1.2.3"

    // Clean version string (remove leading 'v' if present)
    QString cleanRemote = remoteTag;
    if (cleanRemote.startsWith('v')) cleanRemote.remove(0, 1);

    // Build local version string from clientversion.h macros
    QString localVersion = QString("%1.%2.%3")
                            .arg(CLIENT_VERSION_MAJOR)
                            .arg(CLIENT_VERSION_MINOR)
                            .arg(CLIENT_VERSION_REVISION, 2, 10, QChar('0'));

    // If remote version is higher, notify the user
    if (isNewer(cleanRemote, localVersion)) {
        showUpdateDialog(remoteTag);
    }

    reply->deleteLater();
}

bool UpdateChecker::isNewer(const QString& remote, const QString& local) {
    // Regular expression to find version numbers (e.g., 2.0.03)
    QRegularExpression re("(\\d+)\\.(\\d+)\\.(\\d+)");
    
    QRegularExpressionMatch remoteMatch = re.match(remote);
    QRegularExpressionMatch localMatch = re.match(local);

    if (!remoteMatch.hasMatch() || !localMatch.hasMatch()) {
        // If we can't parse numbers, fallback to basic string comparison
        return remote > local;
    }

    // Compare Major, Minor, and Revision
    for (int i = 1; i <= 3; ++i) {
        int remoteNum = remoteMatch.captured(i).toInt();
        int localNum = localMatch.captured(i).toInt();

        if (remoteNum > localNum) return true;
        if (remoteNum < localNum) return false;
    }

    // If we are here, versions are identical or remote has an extra build number
    return false; 
}

void UpdateChecker::showUpdateDialog(const QString& version) {
    QString url = "https://github.com/Raptor3um/raptoreum/releases/latest";
    
    QMessageBox msgBox;
    msgBox.setWindowTitle("Update Available");
    msgBox.setTextFormat(Qt::RichText);
    msgBox.setText(tr("A new stable version of Raptoreum Core is available: <b>%1</b>").arg(version));
    msgBox.setInformativeText(tr("It is recommended to always use the latest version for security and stability.<br><br>"
                                 "<a href='%1'>View Release on GitHub</a>").arg(url));
    msgBox.setIcon(QMessageBox::Information);
    msgBox.setStandardButtons(QMessageBox::Ok);
    
    // Enable link clicking
    msgBox.exec();
}