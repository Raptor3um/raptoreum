#ifndef RAPTOREUM_QT_UPDATECHECKER_H
#define RAPTOREUM_QT_UPDATECHECKER_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QString>

/**
 * Class responsible for checking the latest stable release on GitHub.
 * It compares the remote tag version with the local CLIENT_VERSION.
 */
class UpdateChecker : public QObject {
    Q_OBJECT

public:
    explicit UpdateChecker(QObject *parent = nullptr);
    void checkForUpdates();

private Q_SLOTS:
    /**
     * Handles the network response from GitHub API.
     */
    void handleResponse(QNetworkReply* reply);

private:
    QNetworkAccessManager* manager;
    
    /**
     * Compares two version strings (e.g., "1.2.3" and "1.2.4").
     * Returns true if remote version is higher than local.
     */
    bool isNewer(const QString& remote, const QString& local);
    
    /**
     * Displays a non-modal information dialog if an update is found.
     */
    void showUpdateDialog(const QString& version);
};

#endif // RAPTOREUM_QT_UPDATECHECKER_H