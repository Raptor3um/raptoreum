#include "bootstrapmanager.h"
#include <boost/filesystem.hpp>
#include <curl/curl.h>        
#include <archive.h>          
#include <archive_entry.h>

static size_t WriteCallback(void* ptr, size_t size, size_t nmemb, FILE* stream) {
    return fwrite(ptr, size, nmemb, stream);
}

bool BootstrapManager::DownloadFile(const std::string& url, const std::string& dest, ProgressCallback onProgress) {
    CURL* curl = curl_easy_init();
    if (!curl) return false;

    FILE* fp = fopen(dest.c_str(), "wb");
    if (!fp) {
        curl_easy_cleanup(curl);
        return false;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    CURLcode res = curl_easy_perform(curl);
    fclose(fp);
    curl_easy_cleanup(curl);

    return (res == CURLE_OK);
}

bool BootstrapManager::ExtractArchive(const std::string& archivePath, const std::string& destDir, ProgressCallback onProgress) {
    struct archive* a = archive_read_new();
    struct archive* ext = archive_write_disk_new();
    struct archive_entry* entry;

    archive_read_support_format_zip(a);
    archive_read_support_format_tar(a);
    archive_read_support_filter_xz(a);

    if (archive_read_open_filename(a, archivePath.c_str(), 10240) != ARCHIVE_OK) {
        return false;
    }

    while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        // Zieldpfad setzen
        std::string fullPath = destDir + "/" + archive_entry_pathname(entry);
        archive_entry_set_pathname(entry, fullPath.c_str());
        archive_write_header(ext, entry);

        const void* buff;
        size_t size;
        la_int64_t offset;
        while (archive_read_data_block(a, &buff, &size, &offset) == ARCHIVE_OK) {
            archive_write_data_block(ext, buff, size, offset);
        }
    }

    archive_read_close(a);
    archive_read_free(a);
    archive_write_close(ext);
    archive_write_free(ext);

    return true;
}

bool BootstrapManager::RunIfNeeded(const std::string& dataDir, ProgressCallback onProgress) {
    
    // Schritt 1: Bootstrap-Prüfung ankündigen
    onProgress("Checking if bootstrap is needed...", 0);
    
    // Schritt 2: Prüfen ob blocks/ Ordner fehlt
    if (!IsFirstRun(dataDir)) {
        onProgress("Bootstrap not needed, blocks directory already exists.", 100);
        return true;
    }
    
    onProgress("No blockchain data found, starting bootstrap download...", 5);
    
    // Schritt 3: Download
    onProgress("Downloading bootstrap.zip from bootstrap.raptoreum.com...", 10);
    std::string zipPath = dataDir + "/bootstrap.zip";
    if (!DownloadFile("https://bootstrap.raptoreum.com/bootstraps/bootstrap.zip", zipPath, onProgress)) {
        // Schritt 4: Server nicht erreichbar
        onProgress("ERROR: Bootstrap server not reachable or download failed!", -1);
        return false;
    }
    onProgress("Download complete.", 70);

    // Schritt 5: Entpacken
    onProgress("Extracting bootstrap.zip...", 75);
    if (!ExtractArchive(zipPath, dataDir, onProgress)) {
        onProgress("ERROR: Extraction of bootstrap.zip failed!", -1);
        return false;
    }
    onProgress("Extraction complete.", 85);

    // powcache.dat laden
    onProgress("Downloading powcache.dat...", 90);
    if (!DownloadFile("https://bootstrap.raptoreum.com/bootstraps/powcache.dat", 
                       dataDir + "/powcache.dat", onProgress)) {
        onProgress("WARNING: Could not download powcache.dat, continuing without it.", 95);
    } else {
        onProgress("powcache.dat downloaded successfully.", 95);
    }

    onProgress("Bootstrap complete! Starting normal sync...", 100);
    return true;
}

bool BootstrapManager::IsFirstRun(const std::string& dataDir) {
    boost::filesystem::path blockFile(dataDir);
    blockFile /= "blocks/blk00000.dat";
    // Wenn diese Datei fehlt, ist die Chain leer = Bootstrap nötig
    return !boost::filesystem::exists(blockFile);
}