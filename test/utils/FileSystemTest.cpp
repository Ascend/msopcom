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


#include "utils/FileSystem.h"

#include <string>
#include <vector>
#include <gtest/gtest.h>
#include <iostream>
#include <fstream>
#include <iterator>
#include <experimental/filesystem>
#include <sys/stat.h> // for stat
#include <limits.h>   // for PATH_MAX
#include <unistd.h>
#include "mockcpp/mockcpp.hpp"

using namespace std;
namespace fs = std::experimental::filesystem;

class FileSystemTest : public testing::Test {
public:
    static void SetUpTestCase() {}

    void SetUp() override
    {
        path_ = "./tmp";
        // 创建临时文件和目录
        auto currentPath = std::experimental::filesystem::current_path();
        tempDir_ = currentPath.string() + "/test_dir";
        tempFile_ = tempDir_ + "/test_input_file.so";
        tempSoftLink_ = tempDir_ + "/test_softlink.so";

        std::experimental::filesystem::create_directories(tempDir_);
        std::ofstream file(tempFile_);
        file << "Test file content.";
        std::error_code error;
        std::experimental::filesystem::create_symlink(tempFile_, tempSoftLink_, error);

        // 创建临时文件
        tempReadFile_ = currentPath.string() + "/test_file.txt";
        std::ofstream readFile(tempReadFile_);
        readFile << "Hello, World!";
        chmod(tempReadFile_.c_str(), 0640); // 设置可写权限

    }

    void TearDown() override
    {
        RemoveAll(path_);
        std::remove(tempSoftLink_.c_str());
        std::remove(tempFile_.c_str());
        std::experimental::filesystem::remove_all(tempDir_);
        std::remove(tempReadFile_.c_str());
        GlobalMockObject::verify();
    }
    std::string path_;
    std::string tempDir_;
    std::string tempFile_;
    std::string tempSoftLink_;
    std::string tempReadFile_;
};

/**
/* | 用例集 | FileSystemTest
/* |测试函数| AppendStringToFile()
/* | 用例名 | append_records_expect_file_created_and_previous_content_preserved
/* |用例描述| 向不存在文件连续追加两条记录，首次创建文件并保留原内容
*/
TEST_F(FileSystemTest, append_records_expect_file_created_and_previous_content_preserved) {
    const auto path = tempDir_ + "/timeline_mstx.json";
    const std::string first = "{\"device_id\":0}\n";
    const std::string second = "{\"device_id\":1}\n";
    ASSERT_FALSE(IsPathExists(path));
    ASSERT_TRUE(AppendStringToFile(path, first));
    ASSERT_TRUE(AppendStringToFile(path, second));
    ASSERT_TRUE(AppendStringToFile(path, ""));
    std::ifstream stream(path, std::ios::binary);
    ASSERT_TRUE(stream.is_open());
    const std::string content((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    EXPECT_EQ(content, first + second);
}

/**
/* | 用例集 | FileSystemTest
/* |测试函数| AppendStringToFile()
/* | 用例名 | append_with_invalid_path_or_short_write_expect_false
/* |用例描述| 父目录不存在、目标为目录或短写时追加返回false
*/
TEST_F(FileSystemTest, append_with_invalid_path_or_short_write_expect_false) {
    EXPECT_FALSE(AppendStringToFile(tempDir_ + "/missing/record.json", "record"));
    EXPECT_FALSE(AppendStringToFile(tempDir_, "record"));
    MOCKER(&write).expects(once()).will(returnValue(ssize_t{1}));
    const bool appended = AppendStringToFile(tempDir_ + "/short.json", "record");
    GlobalMockObject::verify();
    EXPECT_FALSE(appended);
}

TEST_F(FileSystemTest, cover_checkInputFileValid_invalid_character)
{
    testing::internal::CaptureStdout();
    std::string invalidPath = tempDir_ + "/invalid\"\rfile.txt";
    MOCKER(&Realpath).stubs().will(returnValue(invalidPath));
    bool result = CheckInputFileValid(invalidPath, "file", 100, "paramName");
    std::string capture = testing::internal::GetCapturedStdout();
    EXPECT_FALSE(result);
    EXPECT_TRUE(capture.find("contains invalid character") != std::string::npos);
    GlobalMockObject::verify();
}

TEST_F(FileSystemTest, cover_checkInputFileValid_contains_softlink)
{
    testing::internal::CaptureStdout();
    MOCKER(&Realpath).stubs().will(returnValue(tempSoftLink_));
    bool result = CheckInputFileValid(tempSoftLink_, "so", 100, "paramName");
    std::string capture = testing::internal::GetCapturedStdout();
    EXPECT_TRUE(capture.find("contains softlink") != std::string::npos);
    GlobalMockObject::verify();
}

TEST_F(FileSystemTest, cover_checkInputFileValid_path_too_long)
{
    testing::internal::CaptureStdout();
    std::string longPath = tempDir_ + "/";
    longPath.append(1000, 'a'); // 超过限制长度
    MOCKER(&Realpath).stubs().will(returnValue(longPath));
    bool result = CheckInputFileValid(longPath, "file", 100, "paramName");
    std::string capture = testing::internal::GetCapturedStdout();
    EXPECT_FALSE(result);
    EXPECT_TRUE(capture.find("path length is too long") != std::string::npos);
    GlobalMockObject::verify();
}

TEST_F(FileSystemTest, cover_checkInputFileValid_not_file)
{
    testing::internal::CaptureStdout();
    bool result = CheckInputFileValid(tempDir_, "file", 100, "paramName");
    std::string capture = testing::internal::GetCapturedStdout();
    EXPECT_FALSE(result);
    EXPECT_TRUE(capture.find("is not a file") != std::string::npos);
}

TEST_F(FileSystemTest, cover_checkInputFileValid_not_in_check_map)
{
    testing::internal::CaptureStdout();
    bool result = CheckInputFileValid(tempFile_, "invalid_type", 100, "paramName");
    std::string capture = testing::internal::GetCapturedStdout();
    EXPECT_FALSE(result);
    EXPECT_TRUE(capture.find("not in check map") != std::string::npos);
}

TEST_F(FileSystemTest, cover_checkInputFileValid_not_readable)
{
    testing::internal::CaptureStdout();
    chmod(tempDir_.c_str(), 0300); // 设置无权限
    bool result = CheckInputFileValid(tempDir_, "dir", 100, "paramName");
    std::string capture = testing::internal::GetCapturedStdout();
    EXPECT_TRUE(capture.find("is not readable") != std::string::npos);
    EXPECT_FALSE(result);
    chmod(tempDir_.c_str(), 0750); // 恢复权限
}

TEST_F(FileSystemTest, cover_checkInputFileValid_fileSize_too_large)
{
    testing::internal::CaptureStdout();
    bool result = CheckInputFileValid(tempFile_, "so", 10, "paramName");
    std::string capture = testing::internal::GetCapturedStdout();
    EXPECT_FALSE(result);
    EXPECT_TRUE(capture.find("file size is too large") != std::string::npos);
}

TEST_F(FileSystemTest, cover_readfile_success)
{
    uint8_t buffer[13] = {0};
    bool result = ReadFile(tempReadFile_, buffer, 13);
    EXPECT_TRUE(result);
}

TEST_F(FileSystemTest, cover_readfile_does_not_exist)
{
    testing::internal::CaptureStdout();
    uint8_t buffer[14] = {0};
    bool result = ReadFile("/nonexistent/file.txt", buffer, 14);
    std::string capture = testing::internal::GetCapturedStdout();
    EXPECT_FALSE(result);
    EXPECT_TRUE(capture.find("does not exist") != std::string::npos);
}

TEST_F(FileSystemTest, cover_readfile_buffer_is_null)
{
    testing::internal::CaptureStdout();
    bool result = ReadFile(tempReadFile_, nullptr, 14);
    std::string capture = testing::internal::GetCapturedStdout();
    EXPECT_FALSE(result);
    EXPECT_TRUE(capture.find("The buffer is null") != std::string::npos);
}

TEST_F(FileSystemTest, cover_readfile_size_not_correct)
{
    testing::internal::CaptureStdout();
    uint8_t buffer[10] = {0};
    bool result = ReadFile(tempReadFile_, buffer, 10, true);
    std::string capture = testing::internal::GetCapturedStdout();
    EXPECT_FALSE(result);
    EXPECT_TRUE(capture.find("is not correct") != std::string::npos);
}

TEST_F(FileSystemTest, cover_readfile_not_readable)
{
    testing::internal::CaptureStdout();
    chmod(tempReadFile_.c_str(), 0); // 设置无权限
    uint8_t buffer[14] = {0};
    bool result = ReadFile(tempReadFile_, buffer, 14);
    std::string capture = testing::internal::GetCapturedStdout();
    EXPECT_FALSE(result);
    EXPECT_TRUE(capture.find("is not readable") != std::string::npos);
    chmod(tempReadFile_.c_str(), 0640); // 恢复权限
}

TEST_F(FileSystemTest, file_system_write_data_with_null_file)
{
    std::vector<char> data = {'t', 'e', 's', 't'};
    std::string filename = "";
    ASSERT_NE(WriteBinary(filename, data.data(), data.size()), data.size());
}

TEST_F(FileSystemTest, not_soft_link)
{
    std::string path = path_;
    MkdirRecusively(path);
    ASSERT_FALSE(IsSoftLink(path));
}

TEST_F(FileSystemTest, is_write_able)
{
    std::string path = path_;
    MkdirRecusively(path);
    ASSERT_TRUE(IsWritable(path));
}

TEST_F(FileSystemTest, mkdir_failed)
{
    std::string path = path_;
    MOCKER(&mkdir).stubs().will(returnValue(-1));
    EXPECT_FALSE(MkdirRecusively(path));
}

TEST_F(FileSystemTest, read_binary_failed)
{
    std::string path = path_;
    std::vector<char> data;

    ASSERT_TRUE(ReadBinary(path, data) == 0);
}

TEST_F(FileSystemTest, fake_binary_file_then_read_binary_expect_success)
{
    std::string path = path_;
    size_t dataSize = 100;
    std::vector<char> data(dataSize, 1);
    std::ofstream writer(path, std::ios::out | std::ios::binary);
    ASSERT_TRUE(Chmod(path, 0640));
    ASSERT_TRUE(writer.is_open());
    writer.write(data.data(), data.size());
    writer.close();

    vector<char> expect;
    ASSERT_EQ(ReadBinary(path, expect), dataSize);
}

TEST_F(FileSystemTest, copy_failed_failed)
{
    std::string fPath {"./tmp"};
    std::string tPath {"./tmp"};
    std::vector<char> data;

    ASSERT_FALSE(CopyFile(fPath, tPath));
}

TEST_F(FileSystemTest, mkdir_return_false_due_to_empty_dirs)
{
    std::string path {""};
    ASSERT_FALSE(MkdirRecusively(path));
}

TEST_F(FileSystemTest, roll_back_path_failed_due_to_nops_not_found)
{
    std::string path {""};
    ASSERT_FALSE(RollbackPath(path, 2U));
}

TEST_F(FileSystemTest, create_symlink_return_success)
{
    using namespace std::experimental::filesystem;
    std::string path = "./temp";
    std::string link = "./temp/1";
    ASSERT_TRUE(MkdirRecusively(path));
    CreateSymlink(path, link);
    CreateSymlink(path, link);
    ASSERT_TRUE(is_symlink(link));
    RemoveAll(path);
}

TEST_F(FileSystemTest, ReadFile_expect_failed)
{
    uint8_t a = 1;
    std::string path = path_;
    ASSERT_FALSE(ReadFile(path, nullptr, 0));
    ASSERT_TRUE(MkdirRecusively(path));
    std::string filePath = "./tmp/1.txt";
    WriteStringToFile(filePath, filePath);
    ASSERT_FALSE(ReadFile(path, &a, 1000));
}

TEST_F(FileSystemTest, fake_file_then_read_file_expect_success)
{
    std::string path = path_;
    size_t dataSize = 100;
    std::vector<char> data(dataSize, 1);
    std::ofstream writer(path, std::ios::out | std::ios::binary);
    ASSERT_TRUE(Chmod(path, 0640));
    ASSERT_TRUE(writer.is_open());
    writer.write(data.data(), data.size());
    writer.close();

    vector<uint8_t> expect(dataSize);
    ASSERT_TRUE(ReadFile(path, expect.data(), dataSize));
}

TEST_F(FileSystemTest, write_file_expect_success)
{
    std::string path = path_;
    size_t dataSize = 100;
    vector<char> data(dataSize, 1);
    ASSERT_TRUE(WriteBinary(path, data.data(), dataSize));
}

TEST_F(FileSystemTest, Chmod_expect_failed)
{
    MOCKER(chmod).stubs().will(returnValue(1));
    std::string path = "./tmp";
    ASSERT_FALSE(Chmod(path, 0));
}

TEST_F(FileSystemTest, Chmod_expect_true)
{
    MOCKER(chmod).stubs().will(returnValue(0));
    std::string path = path_;
    ASSERT_TRUE(Chmod(path, 0));
}

TEST_F(FileSystemTest, input_invalid_char_path_then_check_path_expect_error)
{
    std::string path = "./\n007Ftmp";
    std::ofstream writer(path, std::ios::out | std::ios::binary);
    ASSERT_TRUE(writer.is_open());
    writer.close();
    testing::internal::CaptureStdout();
    ASSERT_FALSE(CheckInputFileValid(path, "bin"));
    std::string capture = testing::internal::GetCapturedStdout();
    ASSERT_NE(capture.find("ERROR"), std::string::npos);
    ASSERT_NE(capture.find("contains"), std::string::npos);
    RemoveAll(path);
}

TEST_F(FileSystemTest, mock_softlink_path_then_check_path_valid_expect_error)
{
    std::string path = path_;
    std::ofstream writer(path, std::ios::out | std::ios::binary);
    ASSERT_TRUE(writer.is_open());
    writer.close();
    MOCKER(IsSoftLinkRecursively).stubs().will(returnValue(true));
    testing::internal::CaptureStdout();
    bool result = CheckInputFileValid(path, "bin");
    std::string capture = testing::internal::GetCapturedStdout();
    ASSERT_NE(capture.find("contains softlink"), std::string::npos);
}

TEST_F(FileSystemTest, mock_long_path_then_check_path_valid_expect_too_long_error)
{
    std::string path = path_;
    std::ofstream writer(path, std::ios::out | std::ios::binary);
    ASSERT_TRUE(writer.is_open());
    writer.close();
    MOCKER(PathLenCheckValid).stubs().will(returnValue(false));
    testing::internal::CaptureStdout();
    ASSERT_FALSE(CheckInputFileValid(path, "bin"));
    std::string capture = testing::internal::GetCapturedStdout();
    ASSERT_NE(capture.find("ERROR"), std::string::npos);
    ASSERT_NE(capture.find("length is too long"), std::string::npos);
}

TEST_F(FileSystemTest, mock_file_path_then_check_path_valid_expect_expect_dir_error)
{
    std::string path = path_;
    std::ofstream writer(path, std::ios::out | std::ios::binary);
    ASSERT_TRUE(writer.is_open());
    writer.close();
    testing::internal::CaptureStdout();
    ASSERT_FALSE(CheckInputFileValid(path, "dir"));
    std::string capture = testing::internal::GetCapturedStdout();
    ASSERT_NE(capture.find("not a dir"), std::string::npos);
    ASSERT_NE(capture.find("ERROR"), std::string::npos);
}

TEST_F(FileSystemTest, mock_file_path_then_check_path_valid_expect_permission_invalid)
{
    std::string path = path_;
    std::ofstream writer(path, std::ios::out | std::ios::binary);
    ASSERT_TRUE(writer.is_open());
    writer.close();
    MOCKER(CheckOwnerPermission).stubs().will(returnValue(false));
    testing::internal::CaptureStdout();
    ASSERT_FALSE(CheckInputFileValid(path, "bin"));
    std::string capture = testing::internal::GetCapturedStdout();
    ASSERT_NE(capture.find("ERROR"), std::string::npos);
}

TEST_F(FileSystemTest, fake_file_then_check_path_invalid_expect_not_readable)
{
    std::string path = path_;
    std::ofstream writer(path, std::ios::out | std::ios::binary);
    ASSERT_TRUE(writer.is_open());
    writer.close();
    testing::internal::CaptureStdout();
    ASSERT_TRUE(Chmod(path, 0200));
    ASSERT_FALSE(CheckInputFileValid(path, "bin"));
    std::string capture = testing::internal::GetCapturedStdout();
    ASSERT_NE(capture.find("ERROR"), std::string::npos);
    ASSERT_NE(capture.find("not readable"), std::string::npos);
}

TEST_F(FileSystemTest, fake_file_then_check_path_invalid_expect_not_exec)
{
    std::string path = path_;
    std::ofstream writer(path, std::ios::out | std::ios::binary);
    ASSERT_TRUE(writer.is_open());
    writer.close();
    testing::internal::CaptureStdout();
    ASSERT_TRUE(Chmod(path, 0600));
    ASSERT_FALSE(CheckInputFileValid(path, "exe"));
    std::string capture = testing::internal::GetCapturedStdout();
    ASSERT_NE(capture.find("ERROR"), std::string::npos);
    ASSERT_NE(capture.find("not executable"), std::string::npos);
}

TEST_F(FileSystemTest, fake_empty_file_then_check_owner_permission_expect_file_permission_error)
{
    std::string path = path_;
    string msg{};
    ASSERT_FALSE(CheckOwnerPermission(path, msg));
    ASSERT_NE(msg.find("permission error"), std::string::npos);
}

TEST_F(FileSystemTest, fake_file_then_check_file_size_valid_expect_false)
{
    std::string path = path_;
    std::ofstream writer(path, std::ios::out | std::ios::binary);
    ASSERT_TRUE(writer.is_open());
    size_t fakeSize = 100;
    vector<char> data(fakeSize);
    writer.write(data.data(), data.size());
    writer.close();
    ASSERT_FALSE(CheckFileSizeValid(path, fakeSize - 1));
}

TEST_F(FileSystemTest, mock_file_then_check_write_file_path_valid_expect_rollback_path_fail)
{
    std::string path = path_;
    testing::internal::CaptureStdout();
    MOCKER(RollbackPath).stubs().will(returnValue(false));
    ASSERT_FALSE(CheckWriteFilePathValid(path));
    std::string capture = testing::internal::GetCapturedStdout();
    ASSERT_NE(capture.find("Failed to get"), std::string::npos);
}

TEST_F(FileSystemTest, fake_file_then_check_write_file_path_valid_expect_get_real_path_failed)
{
    std::string path = "./tmp/aaa";
    testing::internal::CaptureStdout();
    ASSERT_FALSE(CheckWriteFilePathValid(path));
    std::string capture = testing::internal::GetCapturedStdout();
    ASSERT_NE(capture.find("Failed to get real path "), std::string::npos);
}

TEST_F(FileSystemTest, get_so_from_path_expect_return_false)
{
    ASSERT_STREQ(GetSoFromEnvVar("libxxxx.so").c_str(), "");
}

TEST_F(FileSystemTest, check_input_file_expect_return_true)
{
    std:string soPath = "/tmp/libxxxtest.so";
    std::ofstream ofs(soPath);
    ofs.write("111", 3);
    Chmod(soPath, 0640);
    EXPECT_TRUE(CheckInputFileValid(soPath, "so"));
    std::remove(soPath.c_str());
}
