/* -------------------------------------------------------------------------
 * This file is part of the MindStudio project.
 * Copyright (c) 2026 Huawei Technologies Co.,Ltd.
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

#include <algorithm>
#include <experimental/filesystem>
#include <cstdlib>
#include <fstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <sys/stat.h>

#include "camodel/CamodelRawInstrDumper.h"

namespace {
std::string MakeRawDumpTestDir() {
    char path[] = "/tmp/msopprof_callback_dump_XXXXXX";
    char *result = mkdtemp(path);
    return result == nullptr ? "" : result;
}

std::string GetDumpPath(
    const std::string &outputDir, uint32_t coreId, const std::string &subCore, const std::string &logType) {
    return outputDir + "/core" + std::to_string(coreId) + "." + subCore + "." + logType + "_log.dump";
}

std::vector<std::string> ReadLines(const std::string &filePath) {
    std::ifstream input(filePath);
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(input, line)) {
        lines.emplace_back(std::move(line));
    }
    return lines;
}

void ExpectFileMode640(const std::string &filePath) {
    struct stat fileStat {};
    ASSERT_EQ(stat(filePath.c_str(), &fileStat), 0);
    EXPECT_EQ(fileStat.st_mode & 0777, static_cast<mode_t>(0640));
}

template <size_t N> void CopyText(const std::string &source, char (&destination)[N]) {
    const size_t length = std::min(source.size(), N - 1);
    std::copy_n(source.data(), length, destination);
    destination[length] = '\0';
}

Common::DvcInstrLogV2 MakeInstrLog(uint64_t time, uint64_t pc, uint32_t coreId, uint32_t subCoreId,
    const std::string &decode, const std::string &extend = "") {
    Common::DvcInstrLogV2 log{time, pc, coreId, subCoreId};
    CopyText(decode, log.decodeDescr);
    CopyText(extend, log.extendParamsJson);
    return log;
}
}

class CamodelRawInstrDumperTest : public testing::Test {
protected:
    void TearDown() override { CamodelRawInstrDumper::Instance().Stop(); }
};

TEST_F(CamodelRawInstrDumperTest, record_uses_native_dump_file_names_and_line_format) {
    std::string outputDir = MakeRawDumpTestDir();
    ASSERT_FALSE(outputDir.empty());
    const std::string cubeDecode = "(PC: 0x9000d0d000) SCALAR   : (Binary: 0x02004880) (ID: 000000) MOV_XD_SPR";
    const std::string vecDecode =
        "(PC: 0x9000d0d154) MTE2     : (Binary: 0x74b8d61a) (ID: 000789) MOV_SRC_TO_DST_ALIGNv2";
    const std::string vecExtend = R"({"burst_len":128,"dst_mem":"UB"})";
    const std::string vec1Decode = "(PC: 0x9000d0d338) RVECLD   : (Binary: 0x120c0008e0040080 ) (ID: 002124) SIMT_LDS";
    auto cubeEntry = MakeInstrLog(100, 0x9000d0d000ULL, 3, 0, cubeDecode);
    auto vecEntry = MakeInstrLog(200, 0x9000d0d154ULL, 4, 1, vecDecode, vecExtend);
    auto vec1Entry = MakeInstrLog(300, 0x9000d0d338ULL, 5, 2, vec1Decode);
    auto invalidSubCoreEntry = MakeInstrLog(400, 1, 6, 3, "decode");
    auto emptyEntry = MakeInstrLog(500, 1, 7, 1, "");

    ASSERT_TRUE(CamodelRawInstrDumper::Instance().Start(outputDir));
    CamodelRawInstrDumper::Instance().Record(RawInstrCallbackType::POPPED, cubeEntry);
    CamodelRawInstrDumper::Instance().Record(RawInstrCallbackType::COMPLETE, vecEntry);
    CamodelRawInstrDumper::Instance().Record(RawInstrCallbackType::COMPLETE, vec1Entry);
    CamodelRawInstrDumper::Instance().Record(RawInstrCallbackType::COMPLETE, invalidSubCoreEntry);
    CamodelRawInstrDumper::Instance().Record(RawInstrCallbackType::COMPLETE, emptyEntry);
    CamodelRawInstrDumper::Instance().Stop();

    const std::string cubeInstrPath = GetDumpPath(outputDir, 3, "cubecore0", "instr");
    const std::string cubePoppedPath = GetDumpPath(outputDir, 3, "cubecore0", "instr_popped");
    const std::string vecInstrPath = GetDumpPath(outputDir, 4, "veccore0", "instr");
    const std::string vecPoppedPath = GetDumpPath(outputDir, 4, "veccore0", "instr_popped");
    const std::string vec1InstrPath = GetDumpPath(outputDir, 5, "veccore1", "instr");
    const std::string vec1PoppedPath = GetDumpPath(outputDir, 5, "veccore1", "instr_popped");

    EXPECT_TRUE(ReadLines(cubeInstrPath).empty());
    EXPECT_EQ(ReadLines(cubePoppedPath), std::vector<std::string>({"[info] [00000100] " + cubeDecode}));
    EXPECT_EQ(ReadLines(vecInstrPath), std::vector<std::string>({"[info] [00000200] " + vecDecode + "  " + vecExtend}));
    EXPECT_TRUE(ReadLines(vecPoppedPath).empty());
    EXPECT_EQ(ReadLines(vec1InstrPath), std::vector<std::string>({"[info] [00000300] " + vec1Decode}));
    EXPECT_TRUE(ReadLines(vec1PoppedPath).empty());
    EXPECT_FALSE(std::experimental::filesystem::exists(outputDir + "/a5_instr_callback_raw.jsonl"));
    EXPECT_FALSE(std::experimental::filesystem::exists(GetDumpPath(outputDir, 6, "unknowncore3", "instr")));

    for (const auto &filePath :
        {cubeInstrPath, cubePoppedPath, vecInstrPath, vecPoppedPath, vec1InstrPath, vec1PoppedPath}) {
        ExpectFileMode640(filePath);
    }
    std::experimental::filesystem::remove_all(outputDir);
}

TEST_F(CamodelRawInstrDumperTest, concurrent_callbacks_are_written_as_complete_dump_lines) {
    std::string outputDir = MakeRawDumpTestDir();
    ASSERT_FALSE(outputDir.empty());
    const std::string decode = "(PC: 0x9000d0d000) SCALAR   : (Binary: 0x02004880) (ID: 000000) MOV_XD_SPR";
    auto entry = MakeInstrLog(0, 0x9000d0d000ULL, 0, 1, decode);
    ASSERT_TRUE(CamodelRawInstrDumper::Instance().Start(outputDir));

    constexpr int THREAD_COUNT = 4;
    constexpr int RECORDS_PER_THREAD = 25;
    std::vector<std::thread> workers;
    for (int threadId = 0; threadId < THREAD_COUNT; ++threadId) {
        workers.emplace_back([threadId, &entry]() {
            for (int i = 0; i < RECORDS_PER_THREAD; ++i) {
                auto currentEntry = entry;
                currentEntry.time = threadId * RECORDS_PER_THREAD + i;
                CamodelRawInstrDumper::Instance().Record(RawInstrCallbackType::COMPLETE, currentEntry);
            }
        });
    }
    for (auto &worker : workers) {
        worker.join();
    }
    CamodelRawInstrDumper::Instance().Stop();

    auto lines = ReadLines(GetDumpPath(outputDir, 0, "veccore0", "instr"));
    ASSERT_EQ(lines.size(), static_cast<size_t>(THREAD_COUNT * RECORDS_PER_THREAD));
    for (const auto &line : lines) {
        EXPECT_EQ(line.find("[info] ["), 0U);
        EXPECT_NE(line.find(decode), std::string::npos);
        EXPECT_EQ(line.find('\n'), std::string::npos);
    }
    EXPECT_TRUE(ReadLines(GetDumpPath(outputDir, 0, "veccore0", "instr_popped")).empty());
    std::experimental::filesystem::remove_all(outputDir);
}

TEST_F(CamodelRawInstrDumperTest, consecutive_launches_use_independent_dump_files) {
    std::string firstDir = MakeRawDumpTestDir();
    std::string secondDir = MakeRawDumpTestDir();
    ASSERT_FALSE(firstDir.empty());
    ASSERT_FALSE(secondDir.empty());
    const std::string decode = "(PC: 0x1) SCALAR   : (Binary: 0x02004880) (ID: 000000) MOV_XD_SPR";
    auto firstEntry = MakeInstrLog(1, 1, 0, 0, decode);
    auto secondEntry = MakeInstrLog(2, 1, 0, 0, decode);

    ASSERT_TRUE(CamodelRawInstrDumper::Instance().Start(firstDir));
    CamodelRawInstrDumper::Instance().Record(RawInstrCallbackType::POPPED, firstEntry);
    ASSERT_TRUE(CamodelRawInstrDumper::Instance().Start(secondDir));
    CamodelRawInstrDumper::Instance().Record(RawInstrCallbackType::COMPLETE, secondEntry);
    CamodelRawInstrDumper::Instance().Stop();

    EXPECT_EQ(ReadLines(GetDumpPath(firstDir, 0, "cubecore0", "instr_popped")),
        std::vector<std::string>({"[info] [00000001] " + decode}));
    EXPECT_TRUE(ReadLines(GetDumpPath(firstDir, 0, "cubecore0", "instr")).empty());
    EXPECT_EQ(ReadLines(GetDumpPath(secondDir, 0, "cubecore0", "instr")),
        std::vector<std::string>({"[info] [00000002] " + decode}));
    EXPECT_TRUE(ReadLines(GetDumpPath(secondDir, 0, "cubecore0", "instr_popped")).empty());
    std::experimental::filesystem::remove_all(firstDir);
    std::experimental::filesystem::remove_all(secondDir);
}

TEST_F(CamodelRawInstrDumperTest, start_failure_disables_recording_without_throwing) {
    std::string outputDir = MakeRawDumpTestDir();
    ASSERT_FALSE(outputDir.empty());
    std::string missingDir = outputDir + "/missing";
    auto entry = MakeInstrLog(1, 1, 0, 0, "decode");

    EXPECT_FALSE(CamodelRawInstrDumper::Instance().Start(missingDir));
    EXPECT_NO_THROW(CamodelRawInstrDumper::Instance().Record(RawInstrCallbackType::COMPLETE, entry));
    EXPECT_FALSE(std::experimental::filesystem::exists(missingDir));
    std::experimental::filesystem::remove_all(outputDir);
}
