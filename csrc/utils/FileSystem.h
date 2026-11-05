/* -------------------------------------------------------------------------
 * This file is part of the MindStudio project.
 * Copyright (c) 2025 Huawei Technologies Co.,Ltd.
 *
 * MindStudio is licensed under Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *
 *          http://license.coscl.org.cn/MulanPSL2
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 * ------------------------------------------------------------------------- */

#pragma once
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fstream>
#include <experimental/filesystem>
#include <iostream>

#include "Ustring.h"
#include "Singleton.h"

constexpr mode_t DIR_DEFAULT_MOD = 0750;
constexpr char const *PATH_SEP = "/";
constexpr uint64_t MAX_MEM_BYTE_SIZE = 32212254720; // 30G for file and memory malloc
const mode_t SAVE_DATA_FILE_AUTHORITY = 0640;
constexpr int DIR_NAME_LENGTH_LIMIT = 4096;
constexpr int FILE_NAME_LENGTH_LIMIT = 255;

enum class PATH_TYPE {
    DIR = 0,
    FILE
};

enum class FILE_TYPE {
    READ = 0,
    WRITE,
    EXECUTE
};

bool Chmod(const std::string &filePath, mode_t fileAuthority);

inline bool IsExecutable(const std::string &checkPath)
{
    struct stat fileStat{};
    return (stat(checkPath.c_str(), &fileStat) == 0) && (fileStat.st_mode & S_IXUSR) != 0;
}

inline std::string JoinPath(const std::vector<std::string> &pathVectorList)
{
    return Join(pathVectorList.begin(), pathVectorList.end(), PATH_SEP);
}

inline bool IsDir(const std::string &checkPath)
{
    struct stat st{};
    return stat(checkPath.c_str(), &st) == 0 && (st.st_mode & S_IFDIR) != 0;
}

inline bool IsSoftLink(const std::string &path)
{
    struct stat buf{};
    std::fill_n(reinterpret_cast<char *>(&buf), sizeof(buf), 0);
    return (lstat(path.c_str(), &buf) == 0) && ((S_IFMT & buf.st_mode) == S_IFLNK);
}

inline bool IsExist(const std::string &checkPath)
{
    struct stat fileStat{};
    return stat(checkPath.c_str(), &fileStat) == 0;
}

inline bool IsWritable(const std::string &checkPath)
{
    struct stat fileStat{};
    return (stat(checkPath.c_str(), &fileStat) == 0) && (fileStat.st_mode & S_IWUSR) != 0;
}

inline std::string Realpath(const std::string &rawPath)
{
    char tmp[PATH_MAX] = {'\0'};
    auto rpath = realpath(rawPath.c_str(), tmp);
    if (rpath) {
        return rpath;
    }
    return "";
}

inline bool GetLastFile(const std::string &filePath, std::string &file)
{
    if (filePath.empty()) {
        return false;
    }
    size_t right = filePath.size();
    if (right - 1 == filePath.rfind('/')) {
        file = "";
        return true;
    }
    size_t left = filePath.rfind('/', right - 1);
    if (left == std::string::npos) {
        return false;
    }
    file = filePath.substr(left + 1, right - left - 1);
    return true;
}

size_t ReadBinary(std::string const &filename, std::vector<char> &data);
bool MkdirRecusively(std::string const &path, mode_t mode = DIR_DEFAULT_MOD);
bool CopyFile(const std::string &srcPath, const std::string &destPath);
void CreateSymlink(const std::string &src, const std::string &dst);
const std::string &GetEnv(const std::string &envKey);
// 函数功能为路径回退
bool RollbackPath(std::string &path, uint32_t rollNum);
bool IsPathExists(const std::string &path);
void RemoveAll(const std::string& filePath);
size_t GetFileSize(const std::string &filePath);
bool ReadFile(const std::string &filePath, uint8_t *buffer, size_t bufferSize, bool checkSize = false);
size_t WriteBinary(const std::string &filename, const char *data, uint64_t length,
                   std::ios_base::openmode mode = std::ios::out | std::ios::binary | std::ios::trunc);
bool IsSoftLinkRecursively(const std::string &path);
bool PathLenCheckValid(const std::string &checkPath);
bool CheckOwnerPermission(std::string &path, std::string &msg);
bool CheckFileSizeValid(const std::string &path, size_t threshold);
bool CheckWriteFilePathValid(std::string &path);
bool GetCurrentPath(std::string &currentPath);
std::string GetSoFromEnvVar(const std::string &soName);
bool CheckInputFileValid(const std::string &path, const std::string &fileType,
    size_t threshold = MAX_MEM_BYTE_SIZE, std::string paramName = "");

inline bool WriteStringToFile(const std::string &filePath, const std::string &data,
                              std::fstream::openmode openMode = std::fstream::out | std::fstream::trunc,
                              mode_t fileAuthority = SAVE_DATA_FILE_AUTHORITY)
{
    return WriteBinary(filePath, data.data(), data.length(), openMode);
}

// 临时文件/目录统一清理器（进程级单例）。
// 各线程创建的临时目录（如动态插桩的 tmp_<pid>_<timestamp> 根目录，持有者为
// thread_local 单例）在创建时通过 AddPath 登记，进程退出路径调用 CleanAll
// 统一清理，避免无法触发的线程析构导致目录残留。
// AddPath/RemovePath/CleanAll 内部加锁，可在任意普通线程上下文调用，
// 但严禁在信号 handler 中调用（内部含 mutex 与文件 IO，非 async-signal-safe）。
class TempFileCleaner : public Singleton<TempFileCleaner, false> {
    friend class Singleton<TempFileCleaner, false>;

public:
    // 登记待清理路径；keep 为 true 时 CleanAll 跳过该路径（调用方自行保留）
    void AddPath(const std::string &path, bool keep) {
        if (path.empty()) {
            return;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        paths_[path] = keep;
    }

    void RemovePath(const std::string &path) {
        if (path.empty()) {
            return;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        paths_.erase(path);
    }

    // 清理所有已登记路径（keep 为 true 的按原语义保留）。
    // 先在锁内取快照并清空登记表，再在锁外做文件 IO，避免持锁执行
    // remove_all；单个路径删除失败不阻断其余路径的清理。
    // CleanAll 天然幂等，可被析构路径与退出路径重复调用。
    void CleanAll() {
        std::map<std::string, bool> snapshot;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            snapshot.swap(paths_);
        }
        for (const auto &entry : snapshot) {
            if (entry.second || !IsExist(entry.first)) {
                continue;
            }
            try {
                RemoveAll(entry.first);
            } catch (...) {
                // 进程即将退出，删除失败（如文件被占用）时跳过该路径继续清理
            }
        }
    }

#ifdef __BUILD_TESTS__
    // 仅供 UT 观察登记表状态
    bool Contains(const std::string &path) {
        std::lock_guard<std::mutex> lock(mutex_);
        return paths_.find(path) != paths_.end();
    }

    size_t Size() {
        std::lock_guard<std::mutex> lock(mutex_);
        return paths_.size();
    }
#endif

private:
    TempFileCleaner() = default;
    std::mutex mutex_{};
    std::map<std::string, bool> paths_{}; // path → keep
};
