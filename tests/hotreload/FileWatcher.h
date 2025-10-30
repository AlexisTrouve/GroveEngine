#pragma once

#include <string>
#include <unordered_map>
#include <sys/stat.h>
#include <chrono>

namespace grove {

/**
 * @brief Simple file modification watcher
 *
 * Watches files for modifications by checking their mtime.
 * Efficient for hot-reload scenarios where we check a few specific files.
 */
class FileWatcher {
private:
    struct FileInfo {
        timespec lastModified;
        bool exists;
    };

    std::unordered_map<std::string, FileInfo> watchedFiles;

    timespec getModificationTime(const std::string& path) {
        struct stat fileStat;
        if (stat(path.c_str(), &fileStat) == 0) {
            return fileStat.st_mtim;
        }
        return {0, 0};
    }

    bool timesEqual(const timespec& a, const timespec& b) {
        return a.tv_sec == b.tv_sec && a.tv_nsec == b.tv_nsec;
    }

public:
    /**
     * @brief Start watching a file
     * @param path Path to file to watch
     */
    void watch(const std::string& path) {
        FileInfo info;
        info.lastModified = getModificationTime(path);
        info.exists = (info.lastModified.tv_sec != 0 || info.lastModified.tv_nsec != 0);
        watchedFiles[path] = info;
    }

    /**
     * @brief Check if a file has been modified since last check
     * @param path Path to file to check
     * @return True if file was modified
     */
    bool hasChanged(const std::string& path) {
        auto it = watchedFiles.find(path);
        if (it == watchedFiles.end()) {
            // Not watching this file yet
            return false;
        }

        FileInfo& oldInfo = it->second;
        timespec currentMod = getModificationTime(path);
        bool currentExists = (currentMod.tv_sec != 0 || currentMod.tv_nsec != 0);

        // Check if existence changed
        if (oldInfo.exists != currentExists) {
            oldInfo.lastModified = currentMod;
            oldInfo.exists = currentExists;
            return true;
        }

        // Check if modification time changed
        if (!timesEqual(oldInfo.lastModified, currentMod)) {
            oldInfo.lastModified = currentMod;
            return true;
        }

        return false;
    }

    /**
     * @brief Reset a file's tracked state (useful after processing change)
     * @param path Path to file to reset
     */
    void reset(const std::string& path) {
        auto it = watchedFiles.find(path);
        if (it != watchedFiles.end()) {
            it->second.lastModified = getModificationTime(path);
        }
    }

    /**
     * @brief Stop watching a file
     * @param path Path to file to stop watching
     */
    void unwatch(const std::string& path) {
        watchedFiles.erase(path);
    }

    /**
     * @brief Stop watching all files
     */
    void unwatchAll() {
        watchedFiles.clear();
    }
};

} // namespace grove
