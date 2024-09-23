//
// Created by tri on 8/2/24.
//
#include <qt/upload_download.h>
#include <util/system.h>

#include <logging.h>

#include <curl/curl.h>
#include <QString>
#include <QFileDialog>

std::string pickAndSendFileForIpfs(QWidget *qWidget) {
    QString filePath = QFileDialog::getOpenFileName(qWidget, "Open File", "", "All Files (*.*)");
    if (filePath.isEmpty()) {
        return "";
    }
    std::string filePathStr = filePath.toStdString();
    std::string fileUploadUrl = gArgs.GetArg("-ipfsservice", DEFAULT_IPFS_SERVICE_URL) + "upload";
    return sendFile(fileUploadUrl, filePathStr);
}

size_t writeCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

std::string sendFile(const std::string& url, const std::string& file_path) {
    CURL* curl;
    CURLcode res;
    curl_mime* mime;
    curl_mimepart* part;
    std::string response_string;

    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();

    if (curl) {
        mime = curl_mime_init(curl);
        part = curl_mime_addpart(mime);
        curl_mime_name(part, "file");
        curl_mime_filedata(part, file_path.c_str());
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_string);
        res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            LogPrintf("curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
            return "error uploading file " + file_path + " to ipfs server";
        }
        curl_mime_free(mime);
        curl_easy_cleanup(curl);
    }
    curl_global_cleanup();
    return response_string;
}
void downloadFile(const std::string& cid, const std::string& response_data) {
    CURL* curl;
    CURLcode res;
    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();
    if (curl) {
        std::string downUrl =  gArgs.GetArg("-ipfsservice", DEFAULT_IPFS_SERVICE_URL) + "get/" + cid;
        curl_easy_setopt(curl, CURLOPT_URL, downUrl.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_data);

        res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            LogPrintf("curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
        }
        curl_easy_cleanup(curl);
    }
    curl_global_cleanup();
}
