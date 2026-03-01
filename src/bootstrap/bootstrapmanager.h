#pragma once
#include <string>
#include <functional>

// Callback types for progress bar
using ProgressCallback = std::function<void(const std::string& status, int percent)>;

class BootstrapManager {
public:
    // Returns true if bootstrap was necessary and successful, false if not needed or failed
    static bool RunIfNeeded(const std::string& dataDir, ProgressCallback onProgress);

private:
    static bool IsFirstRun(const std::string& dataDir);
    static bool DownloadFile(const std::string& url, const std::string& dest, ProgressCallback onProgress);
    static bool ExtractArchive(const std::string& archivePath, const std::string& destDir, ProgressCallback onProgress);
};