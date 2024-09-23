//
// Created by tri on 8/2/24.
//

#ifndef RAPTOREUM_UPLOADDOWNLOAD_H
#define RAPTOREUM_UPLOADDOWNLOAD_H
#include <string>
#include <QWidget>
#include <QLineEdit>
#include <iostream>
#include <vector>

std::string pickAndSendFileForIpfs(QWidget *qWidget);
size_t writeCallback(void* contents, size_t size, size_t nmemb, void* userp);
std::string sendFile(const std::string& url, const std::string& file_path);
void downloadFile(const std::string& cid, const std::string& response_data);

#endif //RAPTOREUM_UPLOADDOWNLOAD_H
