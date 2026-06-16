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

#include "FileSystem.h"

#include <map>
#include <sys/stat.h>
#include <iterator>
#include "utils/InjectLogger.h"
#include "utils/Path.h"
#include "Ustring.h"

using namespace std;

namespace  {
std::map<std::string, std::pair<uint32_t, bool>> GetFilePermission()
{
    // [key,value] = [fileType, {neededPermission, trueIfCheckOwnerPermission}]
     std::map<std::string, std::pair<uint32_t, bool>> filePermission = {
         {"json", {S_IRUSR, true}}, {"cpp", {S_IRUSR, false}},
         {"bin", {S_IRUSR, true}}, {"kernel", {S_IRUSR, true}},
         {"dump", {S_IRUSR, true}}, {"dir", {S_IRUSR, true}},
         {"so", {S_IRUSR, false}}, {"exe", {S_IEXEC | S_IRUSR, true}}
     };
    return filePermission;
}

inline bool IsReadable(const std::string &checkPath)
{
    struct stat fileStat{};
    return (stat(checkPath.c_str(), &fileStat) == 0) && (fileStat.st_mode & S_IRUSR) != 0;
}

/// fileMode: bitmap,
///                   S_IRUSR--set: need read permission, unset: don't confirm read permission
///                   S_IWUSR--set: need write permission, unset: don't confirm write permission
///                   S_IXUSR--set: need execute permission, unset: don't confirm execute permission
bool CheckPathPermission(const std::string &path, unsigned int fileMode)
{
    if ((fileMode & S_IRUSR) != 0 && !IsReadable(path)) {
        ERROR_LOG("path: %s is not readable", path.c_str());
        return false;
    }
    if ((fileMode & S_IWUSR) != 0 && !IsWritable(path)) {
        ERROR_LOG("path: %s is not writable", path.c_str());
        return false;
    }
    if ((fileMode & S_IXUSR) != 0 && !IsExecutable(path)) {
        ERROR_LOG("path: %s is not executable", path.c_str());
        return false;
    }
    return true;
}
}

inline bool IsRootUser()
{
    constexpr __uid_t root = 0;
    return getuid() == root;
}

size_t ReadBinary(std::string const &filename, vector<char> &data)
{
    size_t fileSize = GetFileSize(filename);
    data.resize(fileSize);
    uint8_t *buffer = reinterpret_cast<uint8_t *>(data.data());
    if (!ReadFile(filename, buffer, fileSize)) {
        return 0;
    }
    return fileSize;
}

bool MkdirRecusively(std::string const &path, mode_t mode)
{
    std::vector<std::string> dirs;
    if (path.empty()) {
        return false;
    }
    SplitString(Path(path).PathCanonicalize().ToString(), *PATH_SEP, dirs);
    if (dirs.empty()) {
        ERROR_LOG("Mkdir [%s] failed because path is empty.", path.c_str());
        return false;
    }
    if (IsDir(path)) {
        return true;
    }

    std::string current;
    for (auto it = dirs.cbegin(); it != dirs.cend(); ++it) {
        if (it == dirs.cbegin()) {
            current = *it;
        } else {
            current.append(PATH_SEP + *it);
        }
        if (*it == "") {
            continue;
        }
        if (IsDir(current)) {
            continue;
        }
        if (mkdir(current.c_str(), mode) < 0) {
            std::string msg;
            if (IsDir(current) && CheckOwnerPermission(current, msg)) {
                WARN_LOG("Mkdir dir %s failed, dir already exist, msg : %s", current.c_str(), msg.c_str());
                continue;
            }
            ERROR_LOG("Mkdir [%s] failed", path.c_str());
            return false;
        }
    }
    return true;
}

bool CopyFile(const std::string &srcPath, const std::string &destPath)
{
    try {
        std::experimental::filesystem::copy(srcPath, destPath);
    }
    catch (const std::experimental::filesystem::filesystem_error& e) {
        ERROR_LOG("Failed to copy file,error reason is %s", e.what());
        return false;
    }
    return true;
}

const std::string &GetEnv(const std::string &envKey)
{
    static std::map<std::string, std::string> envMap;
    auto envIter = envMap.find(envKey);
    if (envIter != envMap.end()) {
        return envIter->second;
    }
    char *value = secure_getenv(envKey.c_str());
    std::string env = (value == nullptr) ? "" : std::string(value);
    envMap[envKey] = env;
    return envMap[envKey];
}

bool RollbackPath(std::string &path, uint32_t rollNum)
{
    std::string tmpPath = path;
    while (rollNum > 0) {
        rollNum--;
        std::size_t found = tmpPath.find_last_of('/');
        if (found == std::string::npos) {
            return false;
        }
        tmpPath = tmpPath.substr(0, found);
        size_t len = tmpPath.size();
        while (len <= tmpPath.size() && len > 0 && tmpPath[len - 1] == '/') {
            len--;
        }
        if (len <= tmpPath.size() && len > 0) {
            tmpPath = tmpPath.substr(0, len);
        }
    }
    path = tmpPath.empty() ? "/" : tmpPath;
    return true;
}

bool IsPathExists(const std::string &path)
{
    struct stat buf{};
    return stat(path.c_str(), &buf) == 0;
}

void RemoveAll(const std::string& filePath)
{
    using namespace std::experimental::filesystem;
    if (!IsPathExists(filePath)) {
        return;
    }
    remove_all(filePath);
}

void CreateSymlink(const std::string &src, const std::string &dst)
{
    using namespace std::experimental::filesystem;
    std::string absSrc = Realpath(src);
    if (!IsPathExists(absSrc)) {
        DEBUG_LOG("File not exist,failed to create link");
        return;
    }
    std::error_code error;
    create_symlink(absSrc, dst, error);
    if (error) {
        DEBUG_LOG("Failed create link from %s to %s, error %s", absSrc.c_str(), dst.c_str(),
                  error.message().c_str());
    }
}

size_t GetFileSize(const std::string &filePath)
{
    if (!IsExist(filePath)) {
        return 0;
    }
    struct stat fileStat{};
    if (stat(filePath.c_str(), &fileStat) != 0 || !S_ISREG(fileStat.st_mode)) {
        return 0;
    }
    auto filesize = static_cast<size_t>(fileStat.st_size);
    return filesize;
}

bool ReadFile(const std::string &filePath, uint8_t *buffer, size_t bufferSize, bool checkSize)
{
    if (!CheckInputFileValid(filePath, "bin")) {
        ERROR_LOG("Check file: %s failed", filePath.c_str());
        return false;
    }
    if (buffer == nullptr) {
        ERROR_LOG("The buffer is null.");
        return false;
    }
    size_t fileSize = GetFileSize(filePath);
    if (checkSize && fileSize != bufferSize) {
        ERROR_LOG("The size of file %s is not correct", filePath.c_str());
        return false;
    }
    size_t readSize = min(fileSize, bufferSize);
    std::ifstream file(filePath, std::ios::binary);
    if (file.fail()) {
        ERROR_LOG("Can not open file [%s] for reading", filePath.c_str());
        return false;
    }
    file.read(reinterpret_cast<char *>(buffer), readSize);
    file.close();
    return true;
}

size_t WriteBinary(const std::string &filename, const char *data, uint64_t length, std::ios_base::openmode mode)
{
    std::string realPath = filename;
    if (!CheckWriteFilePathValid(realPath)) {
        return 0;
    }
    std::ofstream ofs(realPath, mode);
    if (!ofs.is_open()) {
        ERROR_LOG("can not open file: %s", realPath.c_str());
        return 0;
    }
    if (!Chmod(realPath, SAVE_DATA_FILE_AUTHORITY)) {
        WARN_LOG("chmod open file %s to %#o failed", realPath.c_str(), SAVE_DATA_FILE_AUTHORITY);
        return 0;
    }
    ofs.write(data, static_cast<std::streamsize>(length));
    if (!ofs.good()) {
        return 0;
    }
    return length;
}

bool Chmod(const std::string &filePath, mode_t fileAuthority)
{
    if (chmod(filePath.c_str(), fileAuthority) != 0) {
        WARN_LOG("Path [%s] chmod failed.", filePath.c_str());
        return false;
    }
    return true;
}

bool IsSoftLinkRecursively(const std::string &path)
{
    std::string nonConstPath = path;
    while (!nonConstPath.empty() && nonConstPath.back() == '/') {
        nonConstPath.pop_back();
    }
    std::vector<std::string> dirs;
    SplitString(nonConstPath, *PATH_SEP, dirs);
    if (dirs.empty()) {
        return false;
    }
    std::string current;
    for (auto it = dirs.cbegin(); it != dirs.cend(); ++it) {
        if (it == dirs.cbegin()) {
            current = *it;
        } else {
            current.append(PATH_SEP + *it);
        }
        if (*it == "." || *it == ".." || (*it).empty()) {
            continue;
        }
        if (IsSoftLink(current)) {
            return true;
        }
    }
    return false;
}

bool PathLenCheckValid(const std::string &checkPath)
{
    if (checkPath.length() > DIR_NAME_LENGTH_LIMIT) {
        return false;
    }
    std::vector<std::string> dirs;
    SplitString(checkPath, *PATH_SEP, dirs);
    for (const auto &it : dirs) {
        if (it.length() > FILE_NAME_LENGTH_LIMIT) {
            return false;
        }
    }
    return true;
}

bool CheckOwnerPermission(std::string &path, std::string &msg)
{
    struct stat fileStat{};
    if (stat(path.c_str(), &fileStat) != 0) {
        msg = "get file" + path + "permission error.";
        return false;
    }
    if (IsRootUser()) {
        return true;
    }
    if ((fileStat.st_mode & (S_IWOTH | S_IWGRP)) != 0) {
        msg = path + " is writable by any other users or group users, which may cause security problems.";
        WARN_LOG("%s", msg.c_str());
    }
    if (fileStat.st_uid == 0 || fileStat.st_uid == static_cast<uint32_t>(getuid())) {
        return true;
    }
    msg = path + " is not owned by the current user, which may cause security problems.";
    WARN_LOG("%s", msg.c_str());
    return true;
}

bool CheckFileSizeValid(const std::string &path, size_t threshold)
{
    struct stat fileStat{};
    if (stat(path.c_str(), &fileStat) != 0 || !S_ISREG(fileStat.st_mode)) {
        return false;
    }
    auto fileSize = GetFileSize(path);
    return fileSize == 0 || fileSize <= threshold;
}

bool GetCurrentPath(std::string &currentPath)
{
    char buf[PATH_MAX] = {'\0'};
    if (getcwd(buf, sizeof(buf)) == nullptr) {
        WARN_LOG("Get current working dir failed.");
        return false;
    }
    currentPath = std::string(buf);
    return true;
}

// 此处传入path为写入文件路径,包含文件名，会校验文件所在目录安全并修改文件路径成绝对路径
bool CheckWriteFilePathValid(std::string &path)
{
    if (path.find(PATH_SEP) == std::string::npos) {
        std::string currentPath;
        if (!GetCurrentPath(currentPath)) {
            ERROR_LOG("Failed to get %s file work dir", path.c_str());
            return false;
        }
        path = JoinPath({currentPath, path});
    }
    std::string fileDir = path;
    if (!RollbackPath(fileDir, 1)) {
        WARN_LOG("Failed to get %s file dir", path.c_str());
        return false;
    }
    std::string file;
    if (!GetLastFile(path, file)) {
        WARN_LOG("Faile to get last file from %s", path.c_str());
        return false;
    }
    std::string realPath = Realpath(fileDir);
    if (realPath == "") {
        WARN_LOG("Failed to get real path of %s", fileDir.c_str());
        return false;
    }
    path = JoinPath({realPath, file});
    return true;
}

// Get the absolute path of so form  LD_LIBRARY_PATH
std::string GetSoFromEnvVar(const std::string &soName)
{
    char const *ldEnv = secure_getenv("LD_LIBRARY_PATH");
    if (ldEnv == nullptr) {
        return "";
    }
    std::string pathFromEnv = ldEnv;
    std::vector<std::string> envs;
    SplitString(pathFromEnv, ':', envs);
    for (const std::string &path : envs) {
        std::string soPath = JoinPath({path, soName});
        std::string realSoPath = Realpath(soPath);
        if (realSoPath.empty()) {
            continue;
        }
        return realSoPath;
    }
    return "";
}


bool CheckInputFileValid(const std::string &path, const std::string &fileType, size_t threshold, std::string paramName)
{
    if (!paramName.empty()) { paramName = " " + paramName; }
    std::string absPath = Path(path).PathCanonicalize().ToString();
    if (absPath.empty()) {
        ERROR_LOG("Input path %s can not get absolute path.", path.c_str());
        return false;
    }
    std::string errorMsg;
    if (!IsStringCharValid(absPath, errorMsg)) {
        ERROR_LOG("Input parameter %s path contains %s, which is invalid", paramName.c_str(), errorMsg.c_str());
        return false;
    }
    if (IsSoftLinkRecursively(absPath)) {
        WARN_LOG("Input parameter %s path contains softlink, may cause security problems", paramName.c_str());
    }
    if (!PathLenCheckValid(absPath)) {
        ERROR_LOG("Input parameter %s path length is too long.", paramName.c_str());
        return false;
    }
    if (!IsExist(absPath)) {
        ERROR_LOG("Input parameter %s path does not exist", paramName.c_str());
        return false;
    }
    bool expectDir = (fileType == "dir");
    bool gotDir = IsDir(absPath);
    if (expectDir != gotDir) {
        const char *printType = expectDir ? "dir" : "file";
        ERROR_LOG("Input parameter %s path: %s is not a %s", paramName.c_str(), absPath.c_str(),  printType);
        return false;
    }
    auto filePermission = GetFilePermission();
    if (filePermission.count(fileType) == 0) {
        ERROR_LOG("File type not in check map");
        return false;
    }
    uint32_t fileMode = filePermission.at(fileType).first;
    if (!CheckPathPermission(absPath, fileMode)) {
        return false;
    }
    if (filePermission.at(fileType).second && !CheckOwnerPermission(absPath, errorMsg)) {
        ERROR_LOG("%s", errorMsg.c_str());
        return false;
    }
    if (!expectDir && (fileMode == S_IRUSR) && !CheckFileSizeValid(absPath, threshold)) {
        ERROR_LOG("Input parameter %s file size is too large, max file size: %zu", paramName.c_str(), threshold);
        return false;
    }
    return true;
}
