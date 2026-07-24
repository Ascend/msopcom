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


#ifndef __UTILS_PATH_H__
#define __UTILS_PATH_H__

#include <string>
#include <vector>
#include <sys/stat.h>

class Path {
public:
    Path(void) noexcept;
    explicit Path(std::string path) noexcept;
    Path(Path const &) = default;
    Path(Path &&) = default;
    Path &operator=(Path const &) = default;
    Path &operator=(Path &&) = default;
    ~Path() = default;

    /// 将 Path 对象拼接为原始路径字符串
    std::string ToString(void) const;

    /// 获取路径中最后一个文件或目录名
    std::string Name(void) const;

    /// 获取父路径
    Path Parent(void) const &;
    Path Parent(void) &&;

    /// 获取绝对路径
    Path Absolute(void) const &;
    Path Absolute(void) &&;

    /// 路径正规化
    Path Resolved(void) const &;
    Path Resolved(void) &&;

    /// 路径正规化，与 Resolved 作用相同
    Path PathCanonicalize(void) const & { return this->Resolved(); }
    Path PathCanonicalize(void) && { return std::move(this)->Resolved(); }

    /// 路径拼接
    Path operator/(Path rhs) const &;
    Path operator/(Path rhs) &&;

    /// 获取路径文件状态
    bool GetStat(struct stat &st) const;

    /// 校验路径是否存在
    bool Exists(void) const;

private:
    bool absolute_;
    std::vector<std::string> route_;
};

#endif // __UTILS_PATH_H__
