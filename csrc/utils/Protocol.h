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
#include "runtime.h"

#include <cstdint>

#include "core/PlatformConfig.h"
#include "utils/Struct.h"

/* 此文件用于定义 Sanitizer 工具对应的 LocalProcess 与 RemoteProcess
 * 之间进行通信的协议。协议的基本形式由协议头和协议载荷组成：
 * <PacketHead> -- <PacketPayload>
 */

// GM地址写入时，优先写入RecordType，再写入Record；RecordType为32bit目的是为了后续写入GM时的地址对齐
// 这里将不同种类的记录分成若干区间，之后新增记录时，只从每类的最后添加，这样可以保证枚举的底层值
// 不变，从而避免device/host/工具侧版本不同造成的解析错误

// 设置 blockDim 的上限，防止 GM 内存耗尽
constexpr uint64_t MAX_BLOCKDIM_NUMS = 100;
// 每个 block 的记录内存大小
constexpr uint64_t RECORD_BUF_SIZE_EACH_BLOCK = 100 * 1024 * 1024;
/// 头部安全校检值，对应二进制：0b101101001011010010110100101101001011010010110100101101001011010
/// 尽量保证高比特位包含足够多的1，减小碰撞机率
constexpr uint64_t RECORD_HEAD_SECURITY_VALUE = 0x5a5a5a5a5a5a5a5a;

/// mssanitizer 单核检测默认检测的核数，-1代表检测所有核心
constexpr int16_t CHECK_ALL_BLOCK = -1;

/// mssanitizer 默认记录对应的gm size，单位为M
constexpr uint32_t DEFAULT_CACHE_SIZE = 100;

/// 动态插桩插件路径的最大长度
constexpr uint16_t PLUGIN_PATH_MAX = 256;
/// soc version 的最大长度
constexpr uint16_t SOC_VERSION_MAX = 256;
/// 算子 kernel name 的最大长度
constexpr uint16_t KERNEL_NAME_MAX = 2048;
/// 算子 dump path 的最大长度
constexpr uint16_t DUMP_PATH_MAX = 256;

// 用作在block record缓存区域末尾留出余量，避免后一个block修改blockHead时篡改前一个cache的末尾数据
constexpr uint64_t CACHE_LINE_SIZE = 64ULL;

// simt最大线程数
constexpr uint16_t SIMT_THREAD_MAX_SIZE = 2048;

// simt记录占总cache-size比例
constexpr float SIMT_CACHE_SIZE_RATIO  = 0.1;

// shadow memory占总cache-size比例
constexpr float SHADOW_MEM_CACHE_SIZE_RATIO = 0.5;

// shadow memory能正常运行所需GM的最小size,12MB
constexpr uint64_t SHADOW_MEM_MIN_BYTE_SIZE = 12 * 1024 * 1024;

// 非法的地址信息
constexpr uint64_t ILLEGAL_ADDR = 0xFFFFFFFFFFFFFFFFULL;

namespace OnlineShadowMemory {
// gm建模地址范围0 ~ 0xFFFF FFFF FFFF (48 bits)
constexpr uint64_t ONLINE_GLOBAL_MEM_MASK = 0xFFFFFFFFFFFFULL;
// 片上内存建模地址范围0 ~ 0xF FFFF FFFF (32 bits)
constexpr uint64_t ONLINE_LOCAL_MEM_MASK = 0xFFFFFFFFULL;
// 用于标记GM上定义的数据来源于host
constexpr uint64_t ONLINE_ONE_SM_STAND_FOR_BYTE = 0xFFFFULL + 1; // 64KB

enum MemoryByteStatus : uint8_t {
    DEFAULT = 0,
    READ,
    GLOBAL_READ,
    WRITE,
    RACE,
};

enum class OnlineMemoryType : uint8_t {
    GM = 0,
    UB,
};

/*
协议设计如下：
    [63:32]: pc
    [31:31]: sync threads state
    [30:30]: memoryType 当前内存表示ub还是gm，默认为gm
    [11:14]: memory status表示当前内存上的状态，分为DEFAULT/READ/GLOBAL_READ/WRITE/RACE ...
    [10:0]: threadId
*/
constexpr uint64_t PC_START_BIT = 32;
constexpr uint64_t PC_MASK = 0xFFFFU;
constexpr uint64_t SYNC_STATE_START_BIT = 31;
constexpr uint64_t SYNC_STATE_MASK = 0x1U;
constexpr uint64_t MEMORY_TYPE_START_BIT = 30;
constexpr uint64_t MEMORY_TYPE_MASK = 0x1U;
constexpr uint64_t MEMORY_STATUS_START_BIT = 11;
constexpr uint64_t MEMORY_STATUS_MASK = 0xFU;
constexpr uint64_t THREAD_ID_MASK = 0x7FFU;
} // namespace OnlineShadowMemory

// 在线shadow memory的单字节状态
enum class OnlineSmAddrStatus : uint64_t {
    LOCKED_BY_OTHER_THREADS = 1U,
    UNALLOCATABLE = UINT64_MAX, // 内存不足，无法再分配
};

/// argsSize的最大值
constexpr size_t MAX_ALL_PARAM_SIZE = 1ULL * 1024 * 1024 * 1024;
/// 单个参数的最大值
constexpr size_t MAX_SINGLE_PARAM_SIZE = 4096;
/// AclrtMemcpy2d的高度最大值
constexpr size_t MAX_MEMORY_RECORD_HEIGHT = 60ULL * 1024 * 1024 * 1024;

enum class PacketType : uint32_t {
    // 各工具通用的协议
    DEVICE_SUMMARY = 0,  // 设备信息
    KERNEL_SUMMARY,      // 算子运行时信息
    KERNEL_BINARY,       // 算子二进制
    LOG_STRING,          // 子进程日志信息

    // sanitizer 特有的协议
    MEMORY_RECORD = 1000,  // Host 侧内存操作记录
    KERNEL_RECORD,         // Kernel 侧指令记录
    IPC_RECORD,            // IPC 类操作记录

    TEXT = 2000,

    // 用于server向client发送的消息
    IPC_RESPONSE = 3000,
    KERNEL_RECORD_RESPONSE,

    INVALID = ~0U,
};

enum class MaskMode : uint8_t {
    MASK_NORM = 0U,
    MASK_COUNT,
};

struct PacketHead {
    PacketType type;
};

/// 设备信息 Payload
struct DeviceSummary {
    uint32_t device;
    uint32_t blockSize;
    uint32_t blockNum;
    int32_t deviceId;
};

struct KernelSummary {
    uint64_t pcStartAddr;
    uint32_t blockDim;
    KernelType kernelType;
    bool isKernelWithDBI = false;
    bool hasDebugLine = false;
    char kernelName[KERNEL_NAME_MAX];
};

struct VaRegister {
    uint64_t l64;
    uint64_t h64;
};

struct ParaBaseRegister {
    uint64_t addr = ILLEGAL_ADDR;
    uint64_t size;
};

enum class RegisterValueType : uint64_t {
    VAL_UINT64 = 0,
    VAL_HALF,
    VAL_FLOAT,
    VAL_INT32
};

struct RegisterPayload {
    RegisterValueType regValType;
    uint64_t regVal;    // 动态插桩场景无法感知具体类型，统一使用uint64_保存和打印寄存器值，使用时根据 regValType 取值按对应格式解析
    int64_t regIdx;     // 物理核映射的idx，用于GlobalHead内持久化和工具侧回放时索引数组
};

struct Register {
    uint64_t fmatrix;
    uint64_t fmatrixB;
    uint64_t l3dRpt;
    uint64_t vectorMask0 = ~0ULL;
    uint64_t vectorMask1 = ~0ULL;
    uint64_t ndParaConfig;
    uint64_t cmpMaskAddr = ~0UL;
    MaskMode maskMode;
    VaRegister va0;
    VaRegister va1;
    VaRegister va2;
    VaRegister va3;
    VaRegister va4;
    VaRegister va5;
    VaRegister va6;
    VaRegister va7;
    uint64_t sprLoopSizeUb2Out;
    uint64_t sprLoopSizeOut2Ub;
    uint64_t sprLoop1StrideUb2Out;
    uint64_t sprLoop1StrideOut2Ub;
    uint64_t sprLoop2StrideUb2Out;
    uint64_t sprLoop2StrideOut2Ub;
    uint64_t sprPadCntNdDma;
    uint64_t sprLoop0StrideNdDma;
    uint64_t sprLoop1StrideNdDma;
    uint64_t sprLoop2StrideNdDma;
    uint64_t sprLoop3StrideNdDma;
    uint64_t sprLoop4StrideNdDma;
    uint64_t sprLoopSizeOut2L1;
    uint64_t sprLoop1StrideOut2L1;
    uint64_t sprLoop2StrideOut2L1;
    uint64_t sprMte2NzPara;
    uint64_t sprMTE2SrcPara;
    uint64_t sprLoop3Para;
    uint64_t sprChannelPara;
    uint64_t sprFmatrix;
    uint64_t sprFmatrixB;
    uint64_t sprFmatrixDual0;
    uint64_t sprFmatrixDual1;
    uint64_t sprL3dRpt;
    uint64_t sprL3dRptB;
    uint64_t sprPadding;
    uint64_t sprPaddingB;
    uint64_t ctrl = 0x08ULL;
    uint64_t fftsBaseAddr;
    uint64_t fpc;
    uint64_t quantPre;
    uint64_t quantPost;
    RegisterPayload lreluAlpha;
    uint64_t rsv[5];    // 64字节对齐
};

enum class BlockType : uint8_t {
    AIVEC,
    AICUBE,
};

/// 该结构体主要包含当前kernel包含的信息
struct KernelInfo {
    uint64_t totalBlockDim{};                         // 工具根据业务逻辑计算得到的blockDim
    uint64_t totalCacheSize{};
    uint32_t kernelParamNum{};                       // kernel入参个数
    KernelType kernelType{};                          // 当前算子的kernel类型，保存在0核头部
    uint64_t l2CacheOffset;
};

/// 该结构体主要包含当前block包含的信息，保存在每个核的头部
struct BlockInfo {
    uint64_t simtSyncThreadCount{};                   // 当前核上simt单元多少个线程已经运行了sync_thread指令
    uint16_t blockId{};
    uint16_t threadXDim{};
    uint16_t threadYDim{};
    uint16_t threadZDim{};
    BlockType blockType{};                            // 当前block的类型，代表当前核记录的信息属于VEC还是CUBE
    uint8_t vecSubBlockDim{};                         // 当前算子一个blockDim使用的vec核心数，保存在每个核的头部
};

/// 该结构体主要包含命令行传到kernel侧的参数信息，只保存在0核的头部
struct CheckParmsInfo {
    uint32_t cacheSize = DEFAULT_CACHE_SIZE;          // 单个核申请的记录大小，单位为M
    int16_t checkBlockId = CHECK_ALL_BLOCK;           // 检查的blockId, 默认检测所有核的记录
    bool defaultcheck{};                              // 是否开启内存/未初始化/软件栈检测
    bool racecheck{};                                 // 是否开启竞争检测
    bool initcheck{};                                 // 是否开启未初始化检测
    bool synccheck{};                                 // 是否开启同步检测
    bool registerCheck{};                             // 是否开启寄存器检测
};

struct HostMemoryInfo {
    uint64_t addr;
    uint64_t size;
    bool operator<(const HostMemoryInfo& other) const
    {
        if (addr != other.addr) {
            return addr < other.addr;
        }
        return size < other.size;
    }
};

struct SimtInfo {
    uint64_t offset{};                                // simt信息记录的起始偏移
    uint64_t threadByteSize{};                        // 每个thread最多存储多少个字节
    uint64_t shadowMemoryOffset{};                    // shadow memory 起始偏移地址
    uint64_t shadowMemoryByteSize{};                  // shadow memory 最多使用多少字节
    uint32_t ubDynamicSize{};
};

struct SimtRecordBlockHeadImpl {
    uint64_t recordCount{};                 // 记录数量
    uint64_t recordWriteCount{};            // 已经写入的记录数量
    uint64_t offset{};                      // 所有记录对应的offset
    uint64_t writeOffset{};                 // 已经写入的记录对应的offset
};

using SimtRecordBlockHead = StructAlignBy<SimtRecordBlockHeadImpl, 64UL>;
static_assert(sizeof(SimtRecordBlockHead) % 64UL == 0UL, "SimtRecordBlockHead size should aligned by 64 bytes");

constexpr int64_t C220_A2_A3_MAXCORE_NUM = 75;
struct RecordGlobalHeadImpl {
    uint64_t securityVal = RECORD_HEAD_SECURITY_VALUE;
    CheckParmsInfo checkParms{};
    KernelInfo kernelInfo{};
    SimtInfo simtInfo{};
    bool supportSimt{false};                // 当前芯片类型是否支持simt

    Register registers[C220_A2_A3_MAXCORE_NUM]; // 保存核上寄存器状态，数组下标对应coreID
};

using RecordGlobalHead = StructAlignBy<RecordGlobalHeadImpl, 64UL>;
static_assert(sizeof(Register) % 64UL == 0UL, "Register size should aligned by 64 bytes");
static_assert(sizeof(RecordGlobalHead) % 64UL == 0UL, "RecordGlobalHead size should aligned by 64 bytes");

/// kernelType表示当前算子的类型，只记录在了0核的头部；blockType代表当前核记录的信息属于VEC还是CUBE
/// 后续更改该结构体需保证securityVal位于结构体的第一个元素
/// MemInfoIsInvalid函数将该结构体指针头部的8个字节数据和RECORD_HEAD_SECURITY_VALUE做了相等判断
/// 为了保证版本向后兼容，需要通过reserve数组将RecordBlockHead控制在128B，否则会出现record解析错误，
/// 因此在更新完此数据结构时，同时更新reserve字段
struct RecordBlockHeadImpl {
    uint64_t recordCount{};                 // 记录数量
    uint64_t recordWriteCount{};            // 已经写入的记录数量
    uint64_t offset{};                      // 所有记录对应的offset
    uint64_t writeOffset{};                 // 已经写入的记录对应的offset
    BlockInfo blockInfo{};
    uint32_t hostMemoryNum{};               // 算子host侧memory输入个数
#if defined(__CCE_IS_AICORE__) && __CCE_IS_AICORE__ == 1
    __gm__ HostMemoryInfo *hostMemoryInfoPtr{nullptr};     // 算子host侧memory输入数据，kernel侧为gm地址
#else
    HostMemoryInfo *hostMemoryInfoPtr{nullptr};            // 算子host侧memory输入数据，host侧为堆地址
#endif
    uint32_t mstxFuseScopeDepth{};          // 融合语义深度
    bool extraWriteSuccess{false};          // extra地址信息是否写入成功
    ParaBaseRegister paraBase;
};

using RecordBlockHead = StructAlignBy<RecordBlockHeadImpl, 64UL>;
static_assert(sizeof(RecordBlockHead) % 64UL == 0UL, "RecordBlockHead size should aligned by 64 bytes");

/**
 * shadow memory动态分配空间管理
*/
struct ShadowMemoryHeapHeadImpl {
    uint64_t startAddr{0U};
    uint64_t size{0U};
    uint64_t current{0U};
    uint64_t lock{0U};
};

using ShadowMemoryHeapHead = StructAlignBy<ShadowMemoryHeapHeadImpl, 64UL>;
static_assert(sizeof(ShadowMemoryHeapHead) % 64UL == 0UL, "ShadowMemoryHeapHead size should aligned by 64 bytes");

/// MemOpRecord 使用的内存操作类型枚举
enum class MemOpType : uint32_t {
    MALLOC = 0U,
    FREE,
    MEMCPY_BLOCKS,
    LOAD,
    STORE,
    INVALID,
};

/// 内存空间枚举，MemOpRecord 使用，后续逐步弃用
enum class AddressSpace : int32_t { // 待修改为int8_t
    PRIVATE = 0,
    GM,
    L1,
    L0A,
    L0B,
    L0C,
    UB,
    BT, // bias table
    FB, // fixPipe buffer
    INVALID = -1,
};

struct SimtThreadLocation {
    uint16_t idX;
    uint16_t idY;
    uint16_t idZ;
    bool operator==(const SimtThreadLocation &rhs) const
    {
        return this->idX == rhs.idX &&
               this->idY == rhs.idY &&
               this->idZ == rhs.idZ;
    }
};

struct Location {
    uint64_t fileNo;
    uint64_t lineNo;
    uint64_t pc;
    uint16_t blockId;
    bool operator==(const Location &rhs) const
    {
        return this->fileNo == rhs.fileNo &&
               this->lineNo == rhs.lineNo &&
               this->pc == rhs.pc &&
               this->blockId == rhs.blockId;
    }
};

struct ShadowMemoryRecordHead {
    uint32_t type = 40010;
    uint64_t recordCount;
};

struct ShadowMemoryRecord {
    uint64_t addr;
    uint64_t size;
    Location location;
    SimtThreadLocation threadLoc;
    AddressSpace space;
    MemOpType opType;
};

/// 内存分配信息来源，优先级为 MANUAL > EXTRA > ACL > RT > HAL
enum class MemInfoSrc : uint8_t {
    BYPASS = 0,  // 一些内存分配信息不参与优先级运算，使用此类型
    HAL,         // 使用来自底层驱动实际的内存分配信息，此信息通过劫持 hal 接口拿到
    RT,          // 使用来自 runtime 实际的内存分配信息，此信息通过劫持 runtime 接口拿到
    ACL,         // 使用来自 acl 实际的内存分配信息，此信息通过劫持 acl 接口拿到
    EXTRA,       // 使用框架上报的额外内存信息，包含各 Tensor 的地址和长度
    MANUAL,      // 使用用户通过 API 手动上报的内存信息对算子进行检测
    MSTX_HEAP,   // 使用MSTX HEAP拓展接口分配的内存信息
    MSTX_REGION, // 使用MSTX REGION拓展接口分配的内存信息
};

/// 内存信息描述，主要用于描述当前内存是input/tiling/二级指针等
enum class MemInfoDesc : uint8_t {
    DEFAULT = 0,                // 默认值，暂时无用
    INPUT,                      // 算子输入
    TILING,                     // 算子tiling
    DOUBLE_PTR,                 // 二级指针
    HCCL_MC2_CONTEXT,           // 算子的mc2_context内存信息
    SECTION,                    // 算子.o中的section信息
    IPC_MEMORY,                 // IPCg共享内存
    OVERFLOW_ADDR,              // overflow地址信息
    PARA_BASE,                  // para_base地址信息
};

/// Host 侧内存操作记录 Payload
struct HostMemRecord {
    MemOpType type;
    MemInfoSrc infoSrc;
    MemInfoDesc infoDesc;
    uint64_t srcAddr;
    uint64_t dstAddr;
    uint64_t memSize;
    uint64_t paramsNo; // 第几个入参
    uint64_t rootAddr; // 当前host侧的内存记录对应归属地址，主要用于上报mstx内存信息时，将heap和region关联使用
};

enum class IPCOperationType : uint32_t { SET_INFO = 0, DESTROY_INFO, MAP_INFO, UNMAP_INFO };

struct IPCMemorySetInfo {
    uint64_t addr;  // 共享内存在源设备上的地址
    uint64_t size;  // 共享内存的长度
    char name[64];  // 共享内存被设定的名称
};

struct IPCMemoryDestroyInfo {
    char name[64];  // 共享内存被设定的名称
};

struct IPCMemoryMapInfo {
    uint64_t addr;  // 共享内存在目的设备上打开后的虚拟地址
    char name[64];  // 共享内存被设定的名称
};

/** @brief 共享内存解除映射信息
 * 在 `rtIpcCloseMemory` 被调用后发送
 */
struct IPCMemoryUnmapInfo {
    uint64_t addr;  // 要解除的共享内存虚拟地址
};

struct IPCMemRecord {
    IPCOperationType type;
    union {
        IPCMemorySetInfo setInfo;
        IPCMemoryDestroyInfo destroyInfo;
        IPCMemoryMapInfo mapInfo;
        IPCMemoryUnmapInfo unmapInfo;
    };
};

enum class DemangleMode : uint8_t {
    FULL_DEMANGLED_NAME = 0,
    SIMPLE_DEMANGLED_NAME,
    MANGLED_NAME,
};

struct SanitizerConfig {
    bool defaultCheck;
    bool memCheck;
    bool raceCheck;
    bool initCheck;
    bool syncCheck;
    bool registerCheck;
    bool checkDeviceHeap;
    bool checkCannHeap;
    bool leakCheck;
    bool checkUnusedMemory;
    bool checkCrossNpuRaces;
    bool isPrintFullStack{false};
    int16_t checkBlockId = CHECK_ALL_BLOCK;
    uint32_t cacheSize = DEFAULT_CACHE_SIZE;
    DemangleMode demangleMode{DemangleMode::FULL_DEMANGLED_NAME};
    char pluginPath[PLUGIN_PATH_MAX];
    char kernelName[KERNEL_NAME_MAX];
    char dumpPath[DUMP_PATH_MAX];
};

enum class ResponseStatus : uint32_t { SUCCESS = 0, FAIL = 1000 };

struct IPCResponse {
    IPCOperationType type;
    ResponseStatus status;
};

struct KernelRecordResponse {
    uint32_t blockIdx;
    ResponseStatus status;
};
