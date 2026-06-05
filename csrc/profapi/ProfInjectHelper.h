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

#ifndef __PROF_INJECT_HELPER_H__
#define __PROF_INJECT_HELPER_H__
#include <map>
#include "include/thirdparty/prof.h"
#include "utils/Singleton.h"

class ProfInjectHelper : public Singleton<ProfInjectHelper, false> {
    friend class Singleton<ProfInjectHelper, false>;
public:
    std::map<uint32_t, ProfCommandHandle> handleMap_;
    std::map<int32_t, bool> aicpuHandleCallMap_;
private:
    ProfInjectHelper() = default;
};
#endif // __PROF_INJECT_HELPER_H__
