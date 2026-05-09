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

#ifndef __MS_TX_H__
#define __MS_TX_H__

#include <unordered_map>
#include <map>
#include <set>
#include <vector>
#include <mutex>
#include <thread>
#include "utils/Future.h"
#include "runtime/inject_helpers/ProfConfig.h"
#include "runtime.h"

namespace MstxAPI {
using MstxRangeId = uint64_t;
using AclrtStream = void *;
using MstxFuncPointer = void (*)(void);
using MstxFuncTable = MstxFuncPointer** ;

struct MstxDomainRegistration {};
struct MstxMemHeap {};
struct MstxMemRegion {};
using MstxRegionHandle = MstxMemRegion*;

/** @brief Structure to describe a memory virtual range
 * @member deviceId - device id which the memory belongs to
 * @member ptr - memory pointer
 * @member size - memory size
 */
struct MstxMemVirtualRangeDesc {
    uint32_t deviceId;
    void const *ptr;
    uint64_t size;
};

/** @brief Usage characteristics of the heap
 * Usage characteristics help tools like memcheckers, sanitizer,
 * as well as other debugging and profiling tools to determine some
 * special behaviors they should apply to the heap and it's regions.
 */
enum class MstxMemHeapUsageType {
     /** @brief This heap is a sub-allocator
     * Heap created with this usage should not be accessed by the user until regions are registered.
     */
    MSTX_MEM_HEAP_USAGE_TYPE_SUB_ALLOCATOR = 0,
};

/** @brief Memory type characteristics of the heap
 * The 'type' indicates how to interpret the ptr field of the heapDesc.
 * This is intended to support many additional types of memory, beyond
 * standard process virtual memory, such as API specific memory only
 * addressed by handles or multi-dimensional memory requiring more complex
 * descriptions to handle features like strides, tiling, or interlace.
 */
enum class MstxMemType {
     /** @brief Standard process userspace virtual addresses for linear allocations.
     * mstxMemHeapRegister receives a heapDesc of type MstxMemVirtualRangeDesc
     */
    MSTX_MEM_TYPE_VIRTUAL_ADDRESS = 0,
};

/** @brief Structure to describe a memory heap register description
 * @member usage - usage characteristics of the heap
 * @member type - memory type characteristics of the heap
 * @member typeSpecificDesc - memory heap description for specific memory type
 */
struct MstxMemHeapDesc {
    MstxMemHeapUsageType usage;
    MstxMemType type;
    void const *typeSpecificDesc;
};

struct MstxMemHeapRangeDesc {
    MstxMemHeapUsageType usage;
    MstxMemType type;
    MstxMemVirtualRangeDesc rangeDesc;

    MstxMemHeapRangeDesc() : usage{MstxMemHeapUsageType::MSTX_MEM_HEAP_USAGE_TYPE_SUB_ALLOCATOR},
        type{MstxMemType::MSTX_MEM_TYPE_VIRTUAL_ADDRESS}, rangeDesc{{}} {}

    explicit MstxMemHeapRangeDesc(const MstxMemHeapDesc &heapDesc)
        : usage{heapDesc.usage}, type{heapDesc.type},
        rangeDesc{*static_cast<MstxMemVirtualRangeDesc const *>(heapDesc.typeSpecificDesc)} {}
};

/** @brief Description for a batch of memory regions
 * @member heap - the memory pool which the memory regions belong to
 * @member regionType - memory type characteristics of the heap
 * @member regionCount - amount of memory region descriptions
 * @member regionDescArray - array of memory region descriptions
 * @member regionHandleArrayOut - [out] array of handles that stands for registered memory regions
 */
struct MstxMemRegionsRegisterBatch {
    MstxMemHeap* heap;
    MstxMemType regionType;
    size_t regionCount;
    void const *regionDescArray;
    MstxMemRegion **regionHandleArrayOut;
};

enum class MstxMemRegionRefType {
    MSTX_MEM_REGION_REF_TYPE_POINTER = 0,
    MSTX_MEM_REGION_REF_TYPE_HANDLE
};

/** @brief Memory region reference
  * @member refType - the way the reference is described
  * @member pointer - reference is described via pointer, and the pointer is saved
  * @member handle - reference is described via handle, and the handle is saved
  */
struct MstxMemRegionRef {
    MstxMemRegionRefType refType;
    union {
        void const *pointer;
        MstxMemRegion *handle;
    };
};

struct MstxMemRegionsUnregisterBatch {
    size_t refCount;
    MstxMemRegionRef const *refArray;
};

struct MstxMemPermissionsAssignRegionsDesc {
    uint32_t flags;
    MstxMemRegionRef region;
};

struct MstxMemPermissionsAssignBatch {
    size_t regionCount;
    MstxMemPermissionsAssignRegionsDesc const *regionDescArray;
};

constexpr int MSTX_SUCCESS = 0;
constexpr int MSTX_FAIL = 1;
constexpr int MAX_MESSAGE_LENGTH = 1023;
constexpr int MAX_DOMAIN_LENGTH = 1023;
constexpr int MAX_KERNEL_NAME_LENGTH = 1023;

constexpr uint64_t MSTX_TOOL_MSSANITIZER_ID = 0x1002;
constexpr uint64_t MSTX_TOOL_MSPROFOP_ID = 0x1001;

enum class MstxFuncModule {
    MSTX_API_MODULE_INVALID     = 0,
    MSTX_API_MODULE_CORE        = 1,
    MSTX_API_MODULE_CORE_DOMAIN = 2,
    MSTX_API_MODULE_CORE_MEM    = 3,
    MSTX_API_MODULE_SIZE,       // end of the enum, new enum items must be added before this
    MSTX_API_MODULE_FORCE_INT               = 0x7fffffff
};

enum class MstxImplCoreMemFuncId : uint32_t {
    MSTX_API_CORE_MEM_INVALID               = 0,
    MSTX_API_CORE_MEMHEAP_REGISTER          = 1,
    MSTX_API_CORE_MEMHEAP_UNREGISTER        = 2,
    MSTX_API_CORE_MEM_REGIONS_REGISTER      = 3,
    MSTX_API_CORE_MEM_REGIONS_UNREGISTER    = 4,
    MSTX_API_CORE_MEM_PERMISSIONS_ASSIGN    = 5,
    MSTX_API_CORE_MEM_SIZE,                   // end of the enum, new enum items must be added before this
    MSTX_API_CORE_MEM_FORCE_INT             = 0x7fffffff
};

enum class MstxImplCoreFuncId : uint32_t {
    MSTX_API_CORE_INVALID           =  0U,
    MSTX_API_CORE_MARK_A            =  1U,
    MSTX_API_CORE_RANGE_START_A     =  2U,
    MSTX_API_CORE_RANGE_END         =  3U,
    MSTX_API_CORE_GET_TOOL_ID       =  4U,
    MSTX_API_CORE_FORCE_INT         = 0x7fffffffU,
};

enum class MstxImplCoreDomainFuncId : uint32_t {
    MSTX_API_CORE2_INVALID                 =  0,
    MSTX_API_CORE2_DOMAIN_CREATE_A         =  1,
    MSTX_API_CORE2_DOMAIN_DESTROY          =  2,
    MSTX_API_CORE2_DOMAIN_MARK_A           =  3,
    MSTX_API_CORE2_DOMAIN_RANGE_START_A    =  4,
    MSTX_API_CORE2_DOMAIN_RANGE_END        =  5,
    MSTX_API_CORE2_SIZE,                   // end of the enum, new enum items must be added before this
    MSTX_API_CORE2_FORCE_INT = 0x7fffffff
};

/// region归属的heap信息
struct MemRegionsBelongInfo {
    uint64_t addr{0};
    uint64_t size{0};
    bool success{false}; // 表示是否有归属的heap信息
};

using MstxGetModuleFuncTableFunc = int (*)(MstxFuncModule module, MstxFuncTable *outTable,
    unsigned int *outSize);
}

/*
 * MsTx类用于管理mstx api内部状态
 */
class MsTx {
public:
    static MsTx &Instance()
    {
        static MsTx instance;
        return instance;
    }

    MstxAPI::MstxRangeId MstxRangeAdd(const std::string& message);
    void MstxRangeEnd(const MstxAPI::MstxRangeId& rangeId);
    bool IsInMstxRange();
    bool IsMessageEnable(const std::string& msg);
    MstxAPI::MstxDomainRegistration* MstxDomainCreateA(const std::string &domainName);
    bool IsDomainExist(MstxAPI::MstxDomainRegistration *domain) const;
    MstxAPI::MstxMemHeap* MstxMemHeapRegister(MstxAPI::MstxDomainRegistration *domain,
        MstxAPI::MstxMemHeapDesc const *desc);
    bool MstxMemHeapUnregister(MstxAPI::MstxDomainRegistration *domain, MstxAPI::MstxMemHeap *heap,
        MstxAPI::MstxMemHeapRangeDesc *desc);
    MstxAPI::MemRegionsBelongInfo MstxMemRegionsRegister(MstxAPI::MstxDomainRegistration *domain,
        MstxAPI::MstxMemRegionsRegisterBatch const *desc);
    bool MstxMemRegionsUnregister(MstxAPI::MstxDomainRegistration *domain,
                                  MstxAPI::MstxMemRegionsUnregisterBatch const *desc,
                                  std::vector<MstxAPI::MstxMemVirtualRangeDesc> &rangeDescVec);
    bool MstxMemPermissionsAssign(MstxAPI::MstxDomainRegistration *domain,
                                  MstxAPI::MstxMemPermissionsAssignBatch const *desc,
                                  std::vector<MstxAPI::MstxMemVirtualRangeDesc> &rangeDescVec);

    MsTx(const MsTx&) = delete;
    MsTx& operator=(MsTx const&) & = delete;
    MsTx(MsTx &&) = delete;
    MsTx& operator=(MsTx&&) & = delete;

private:
    MsTx();
    void MstxAttrReset(bool forceReset = false);
    void UpdateRegionsUnregisterInfo(MstxAPI::MstxDomainRegistration const *domain,
        MstxAPI::MstxMemRegionsUnregisterBatch const *desc,
        std::vector<MstxAPI::MstxMemVirtualRangeDesc> &rangeDescVec, MstxAPI::MstxMemRegion *targetHandle);

    bool isMstxInit_ = false;
    std::mutex mstxAttrMutex_;
    bool isMstxEnabledMessageInit_ = false;
    std::set<std::string> mstxEnabledMessageList_;
    // mstxRangeMessageUsageCountMap_: map<threadId, map<mstxMessage, count>>
    std::unordered_map<std::thread::id, std::unordered_map<std::string, uint64_t>> mstxRangeMessageUsageCountMap_;
    std::unordered_map<MstxAPI::MstxRangeId, std::string> mstxRangeActiveId2MessageMap_;
    std::set<MstxAPI::MstxRangeId> rangeReplayInvalidRangeIds_;
    MstxAPI::MstxRangeId mstxRangeId_ = 0;
    std::map<std::string, std::unique_ptr<MstxAPI::MstxDomainRegistration>> mstxName2DomainMap_;
    std::map<MstxAPI::MstxDomainRegistration *, std::string> mstxDomain2NameMap_;
    std::map<std::pair<MstxAPI::MstxDomainRegistration *, MstxAPI::MstxMemHeap *>, MstxAPI::MstxMemHeapRangeDesc>
        mstxDomainMemHeapDescMap_;
    std::map<MstxAPI::MstxDomainRegistration *, std::vector<std::unique_ptr<MstxAPI::MstxMemHeap>>>
        mstxDomainMemHeapMap_;
    std::map<const MstxAPI::MstxDomainRegistration *, std::vector<std::unique_ptr<MstxAPI::MstxMemRegion>>>
        mstxDomainRegionsMap_;
    std::map<MstxAPI::MstxMemRegion *, MstxAPI::MstxMemVirtualRangeDesc> mstxRegionRangeDescMap_;
};
#endif // __MS_TX_H__
