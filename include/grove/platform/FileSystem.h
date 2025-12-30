#pragma once

/**
 * Platform-independent filesystem utilities
 * Replaces std::filesystem to avoid MinGW static initialization crash
 */

#include <string>
#include <vector>
#include <fstream>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#endif

namespace grove {
namespace fs {

/**
 * Check if a file or directory exists
 */
inline bool exists(const std::string& path) {
#ifdef _WIN32
    DWORD attrs = GetFileAttributesA(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES;
#else
    struct stat st;
    return stat(path.c_str(), &st) == 0;
#endif
}

/**
 * Check if path is a directory
 */
inline bool isDirectory(const std::string& path) {
#ifdef _WIN32
    DWORD attrs = GetFileAttributesA(path.c_str());
    return (attrs != INVALID_FILE_ATTRIBUTES) && (attrs & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return false;
    return S_ISDIR(st.st_mode);
#endif
}

/**
 * Check if path is a regular file
 */
inline bool isFile(const std::string& path) {
#ifdef _WIN32
    DWORD attrs = GetFileAttributesA(path.c_str());
    return (attrs != INVALID_FILE_ATTRIBUTES) && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return false;
    return S_ISFILE(st.st_mode);
#endif
}

/**
 * Create a single directory
 */
inline bool createDirectory(const std::string& path) {
#ifdef _WIN32
    return _mkdir(path.c_str()) == 0 || errno == EEXIST;
#else
    return mkdir(path.c_str(), 0755) == 0 || errno == EEXIST;
#endif
}

/**
 * Create directories recursively
 */
inline bool createDirectories(const std::string& path) {
    std::string currentPath;
    for (size_t i = 0; i < path.size(); ++i) {
        char c = path[i];
        currentPath += c;
        if (c == '/' || c == '\\') {
            if (!currentPath.empty() && currentPath != "/" && currentPath != "\\") {
                createDirectory(currentPath);
            }
        }
    }
    if (!currentPath.empty()) {
        return createDirectory(currentPath);
    }
    return true;
}

/**
 * Get the parent path
 */
inline std::string parentPath(const std::string& path) {
    size_t pos = path.find_last_of("/\\");
    if (pos == std::string::npos) return "";
    if (pos == 0) return path.substr(0, 1);
    return path.substr(0, pos);
}

/**
 * Get the filename from a path
 */
inline std::string filename(const std::string& path) {
    size_t pos = path.find_last_of("/\\");
    if (pos == std::string::npos) return path;
    return path.substr(pos + 1);
}

/**
 * Get the file extension (including the dot)
 */
inline std::string extension(const std::string& path) {
    std::string name = filename(path);
    size_t pos = name.find_last_of('.');
    if (pos == std::string::npos) return "";
    return name.substr(pos);
}

/**
 * Get the filename without extension (stem)
 */
inline std::string stem(const std::string& path) {
    std::string name = filename(path);
    size_t pos = name.find_last_of('.');
    if (pos == std::string::npos) return name;
    return name.substr(0, pos);
}

/**
 * Get file size
 */
inline size_t fileSize(const std::string& path) {
#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &fad)) {
        return 0;
    }
    return (static_cast<size_t>(fad.nFileSizeHigh) << 32) | fad.nFileSizeLow;
#else
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return 0;
    return static_cast<size_t>(st.st_size);
#endif
}

/**
 * List files in a directory
 */
inline std::vector<std::string> listDirectory(const std::string& path) {
    std::vector<std::string> result;
#ifdef _WIN32
    WIN32_FIND_DATAA fd;
    std::string searchPath = path + "\\*";
    HANDLE hFind = FindFirstFileA(searchPath.c_str(), &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            std::string name = fd.cFileName;
            if (name != "." && name != "..") {
                result.push_back(name);
            }
        } while (FindNextFileA(hFind, &fd));
        FindClose(hFind);
    }
#else
    DIR* dir = opendir(path.c_str());
    if (dir) {
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            std::string name = entry->d_name;
            if (name != "." && name != "..") {
                result.push_back(name);
            }
        }
        closedir(dir);
    }
#endif
    return result;
}

/**
 * Copy a file
 */
inline bool copyFile(const std::string& from, const std::string& to) {
#ifdef _WIN32
    return CopyFileA(from.c_str(), to.c_str(), FALSE) != 0;
#else
    std::ifstream src(from, std::ios::binary);
    std::ofstream dst(to, std::ios::binary);
    if (!src || !dst) return false;
    dst << src.rdbuf();
    return true;
#endif
}

} // namespace fs
} // namespace grove
