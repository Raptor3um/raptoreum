//
// Created by tri on 8/8/24.
//

#ifndef RAPTOREUM_UPLOAD_DOWNLOAD_H
#define RAPTOREUM_UPLOAD_DOWNLOAD_H

#include <util/system.h>

#include <iostream>
#include <vector>
#include <string>
#include <QWidget>

static std::string GET_URI = "/get/";
static std::string UPLOAD_URI = "/upload";
static std::string IPFS_SERVICE_HOST = gArgs.GetArg("-ipfsservice", "ipfsm.raptoreum.com");

void download(const std::string cid, std::string& response_data);
void upload(const std::string& file_path, std::string& response_data);
void pickAndUploadFileForIpfs(QWidget *qWidget, std::string& cid);

#endif //RAPTOREUM_UPLOAD_DOWNLOAD_H
