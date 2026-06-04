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

// 这个文件将被劫持的接口改名

#ifndef __ASCENDCL_ORIGIN_H__
#define __ASCENDCL_ORIGIN_H__

#include <string>

#include "acl.h"
#include "core/FunctionLoader.h"

void AscendclOriginCtor();

aclError aclrtSetOpExecuteTimeOutOrigin(uint32_t timeout);

aclError aclrtCreateEventOrigin(aclrtEvent *event);

aclError aclrtStreamWaitEventOrigin(aclrtStream stream, aclrtEvent event);

aclError aclrtRecordEventOrigin(aclrtEvent event, aclrtStream stream);

aclError aclrtResetEventOrigin(aclrtEvent event, aclrtStream stream);

aclError aclrtDestroyEventOrigin(aclrtEvent event);

#endif // __ASCENDCL_ORIGIN_H__
