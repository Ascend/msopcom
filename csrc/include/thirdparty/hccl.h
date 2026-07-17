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

#ifndef HCCL_TYPES_H_
#define HCCL_TYPES_H_

#include <cstdint>
#include "acl.h"

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

/**
 * @brief HCCL functions return value definition
 */
typedef enum {
    HCCL_SUCCESS = 0,               /**< success */
    HCCL_E_PARA = 1,                /**< parameter error */
    HCCL_E_PTR = 2,                 /**< empty pointer */
    HCCL_E_MEMORY = 3,              /**< memory error */
    HCCL_E_INTERNAL = 4,            /**< internal error */
    HCCL_E_NOT_SUPPORT = 5,         /**< not support feature */
    HCCL_E_NOT_FOUND = 6,           /**< not found specific resource */
    HCCL_E_UNAVAIL = 7,             /**< resource unavailable */
    HCCL_E_SYSCALL = 8,             /**< call system interface error */
    HCCL_E_TIMEOUT = 9,             /**< timeout */
    HCCL_E_OPEN_FILE_FAILURE = 10,  /**< open file fail */
    HCCL_E_TCP_CONNECT = 11,        /**< tcp connect fail */
    HCCL_E_ROCE_CONNECT = 12,       /**< roce connect fail */
    HCCL_E_TCP_TRANSFER = 13,       /**< tcp transfer fail */
    HCCL_E_ROCE_TRANSFER = 14,      /**< roce transfer fail */
    HCCL_E_RUNTIME = 15,            /**< call runtime api fail */
    HCCL_E_DRV = 16,                /**< call driver api fail */
    HCCL_E_PROFILING = 17,          /**< call profiling api fail */
    HCCL_E_CCE = 18,                /**< call cce api fail */
    HCCL_E_NETWORK = 19,            /**< call network api fail */
    HCCL_E_AGAIN = 20,              /**< try again */
    HCCL_E_REMOTE = 21,             /**< error cqe */
    HCCL_E_SUSPENDING = 22,         /**< error communicator suspending */
    HCCL_E_RESERVED                 /**< reserved */
} HcclResult;

typedef void *HcclComm;

const uint32_t HCCL_ROOT_INFO_BYTES =  4108; // 4108: root info length
const uint32_t COMM_NAME_MAX_LENGTH = 128; // group name max length
const uint32_t UDI_MAX_LENGTH = 128; // UDI max length
const uint32_t HCCL_COMM_CONFIG_INFO_BYTES = 24;

typedef struct HcclRootInfoDef {
    char internal[HCCL_ROOT_INFO_BYTES];
} HcclRootInfo;

typedef struct HcclCommConfigDef {
    char reserved[HCCL_COMM_CONFIG_INFO_BYTES];
    uint32_t hcclBufferSize;
    uint32_t hcclDeterministic;
    char hcclCommName[COMM_NAME_MAX_LENGTH];
    char hcclUdi[UDI_MAX_LENGTH];
    uint32_t hcclOpExpansionMode;   // 0:默认值  1:host  2:aicpu  3:aiv
    uint32_t hcclRdmaTrafficClass;
    uint32_t hcclRdmaServiceLevel;
} HcclCommConfig;

constexpr uint32_t HCCL_MAX_RANK_NUM = 32U;

struct MemDetails {
    uint64_t size;
    uint64_t addr;
    uint32_t key;
};

struct IbVerbsData {
    MemDetails remoteInput;
    MemDetails remoteOutput;
    MemDetails localInput;
    MemDetails localOutput;
    uint8_t res[24];
};

struct HcclCombinOpParam {
    uint64_t workSpace;
    uint64_t workSpaceSize;
    uint32_t rankId = 0;   // 当前卡rankId
    uint32_t rankNum = 0;
    uint64_t winSize = 0;  // 每个win大小
    uint64_t windowsIn[HCCL_MAX_RANK_NUM];
    uint64_t windowsOut[HCCL_MAX_RANK_NUM];
    uint8_t res[8328];
    uint8_t multiFlag;     // 多机标识，该场景下工具暂不支持
    IbVerbsData *data;     // 多机共享内存地址信息
};

constexpr uint32_t LOCAL_NOTIFY_MAX_NUM = 64;
constexpr uint32_t CUR_LOCAL_STREAM_MAX_NUM = 40U;
constexpr uint32_t RES_LOCAL_STREAM_MAX_NUM = 19U;
constexpr uint32_t AICPU_OP_NOTIFY_MAX_NUM = 2;
constexpr uint32_t AICPU_MAX_RANK_NUM = 128 * 1024;
constexpr uint32_t MAX_RANK_NUM_A3 = 768;
constexpr uint32_t MAX_MODULE_DEVICE_NUM = 32;

struct HcclSignalInfo {
    uint64_t resId; // 在代表event时为eventid，notify时为notifyid
    uint64_t addr;
    uint32_t devId;
    uint32_t tsId;
    uint32_t rankId;
    uint32_t flag;
};

struct ListCommon {
    uint64_t nextHost;
    uint64_t preHost;
    uint64_t nextDevice;
    uint64_t preDevice;
};

struct HcclStreamInfo {
    int32_t streamIds;
    uint32_t sqIds;
    uint32_t cqIds; // 记录物理cqId
    uint32_t logicCqids; // 记录逻辑cqId
};

struct HcclMC2WorkSpace {
    uint64_t workSpace;
    uint64_t workSpaceSize;
};

// 预留占位内存
struct ReservedStruct {
    uint32_t streamNum;
    uint32_t signalNum;
    HcclSignalInfo localSignals[LOCAL_NOTIFY_MAX_NUM];
    HcclStreamInfo streamInfo
        [RES_LOCAL_STREAM_MAX_NUM]; // 19为630版本数组的实际大小，不能使用已经变更为40的LOCAL_NOTIFY_MAX_NUM，否则会有兼容问题
    HcclStreamInfo mainStreamInfo;
    HcclSignalInfo aicpuOpNotify[AICPU_OP_NOTIFY_MAX_NUM]; // 集合通信AICPU展开资源
    ListCommon nextTagRes; // HccltagLocalResV2
};

// 算子计数信息
struct OpCounterInfo {
    uint64_t headCountMem = 0;
    uint64_t tailCountMem = 0;
    uint64_t addOneMem = 0;
    uint32_t memSize = 0;
    bool isEnableCounter = false;
};

// 记录aicpu-custom共享的stream信息
struct HcclStreamParam {
    HcclStreamInfo streamInfo;
    uint64_t sqCqContextAddr = 0; // 记录sqeContext地址
    uint64_t sqCqContextSize = 0; // 记录sqeContext大小
};

struct LocalResInfoV2 {
    uint32_t streamNum;
    uint32_t signalNum;
    HcclSignalInfo localSignals[LOCAL_NOTIFY_MAX_NUM];
    HcclStreamParam streamParam[CUR_LOCAL_STREAM_MAX_NUM];
    HcclStreamParam mainStreamParam;
    HcclSignalInfo aicpuOpNotify[AICPU_OP_NOTIFY_MAX_NUM]; // 集合通信AICPU展开资源
    ListCommon nextTagRes; // HccltagLocalResV2
};

struct HierarchicalAlgInfo {
    uint64_t commplaneSubGroupRankLength; // complanSubGroupRank占用的字节数
    uint64_t commplaneSubGroupRank; // 指针
    uint32_t hierarchicalAlgOptionNum;
    uint64_t hierarchicalAlgOptionVec; // hierarchicalAlgOptionVec数组指针
};

enum class rtFloatOverflowMode_t {
    RT_OVERFLOW_MODE_SATURATION = 0,
    RT_OVERFLOW_MODE_INFNAN,
    RT_OVERFLOW_MODE_UNDEF,
};

struct AlgoTopoInfo {
    uint32_t userRank; // 通信域 RankID
    uint32_t userRankSize; // 通信域的Rank数量
    int32_t deviceLogicId;
    bool isSingleMeshAggregation;
    uint32_t deviceNumPerAggregation; // 每个Module中的Device数量
    uint32_t superPodNum; // 集群中总的超节点数
    uint32_t devicePhyId;
    uint32_t topoType; // TopoType
    uint32_t deviceType;
    uint32_t serverNum;
    uint32_t meshAggregationRankSize;
    uint32_t multiModuleDiffDeviceNumMode;
    uint32_t multiSuperPodDiffServerNumMode;
    uint32_t realUserRank;
    bool isDiffDeviceModule;
    bool isDiffDeviceType;
    uint32_t gcdDeviceNumPerAggregation;
    uint32_t moduleNum;
    uint32_t isUsedRdmaRankPairNum;
    uint64_t isUsedRdmaRankPair;
    uint32_t pairLinkCounterNum;
    uint64_t pairLinkCounter;
    uint32_t nicNum;
    uint64_t nicList; // niclist数组指针
    uint64_t complanRankLength; // complanRank占用的字节数
    uint64_t complanRank; // 指针
    uint64_t bridgeRankNum; // bridgeRank占用的个数
    uint64_t bridgeRank; // 指针
    uint64_t serverAndsuperPodRankLength; // serverAndsuperPodRank占用的字节数
    uint64_t serverAndsuperPodRank; // 指针
};

struct HcclOpConfig {
    uint8_t deterministic; //确定性计算开关
    uint8_t retryEnable; // 是否重执行
    uint8_t highPerfEnable;
    uint8_t padding[5]; // 大小需要64By对齐，未来添加参数时减小padding
    uint8_t linkTimeOut[8]; // 发送超时时长
    uint64_t notifyWaitTime; // 超时时长，同HCCL_EXEC_TIMEOUT
    uint32_t retryHoldTime;
    uint32_t retryIntervalTime;
    bool interHccsDisable = false; //使能rdma开关
    rtFloatOverflowMode_t floatOverflowMode = rtFloatOverflowMode_t::RT_OVERFLOW_MODE_UNDEF;
    uint32_t multiQpThreshold = 512; // 多QP每个QP分担数据量最小阈值
};

struct RemoteResPtr {
    uint64_t nextHostPtr;
    uint64_t nextDevicePtr;
};

struct HcclRankRelationResV2 {
    uint32_t remoteUsrRankId;
    uint32_t remoteWorldRank;
    uint64_t windowsIn;
    uint64_t windowsOut;
    uint64_t windowsExp;
    ListCommon nextTagRes;
};

struct HDCommunicateParams {
    uint64_t hostAddr{0};
    uint64_t deviceAddr{0};
    uint64_t readCacheAddr{0};
    uint32_t devMemSize{0};
    uint32_t buffLen{0};
    uint32_t flag{0};
};

struct MemDetails1 {
    uint64_t size = 0;
    uint64_t addr = 0;
    uint32_t key = 0;
};

struct HcclOpResParam {
    // 本地资源
    HcclMC2WorkSpace mc2WorkSpace;
    uint32_t localUsrRankId; // usrrankid
    uint32_t rankSize; // 通信域内total rank个数
    uint64_t winSize; // 每个win大小，静态图时，可能是0，如果通信域内也有动态图，则可能为非0
    uint64_t localWindowsIn; // 全F为无效值
    uint64_t localWindowsOut; // 全F为无效值
    char hcomId[128];
    // aicore识别remote window
    uint64_t winExpSize;
    uint64_t localWindowsExp;
    uint32_t rWinStart; // 为HcclRankRelationRes起始位置
    uint32_t rWinOffset; // 为HcclRemoteRes的大小
    uint64_t version;
    ReservedStruct reservedStruct;
    AlgoTopoInfo topoInfo;

    // 外部配置参数
    HcclOpConfig config;
    uint64_t hostStateInfo;
    uint64_t aicpuStateInfo;
    uint64_t lockAddr;
    uint32_t rsv[16];
    uint32_t notifysize; // RDMA场景使用，910B/910_93为4B，其余芯片为8B
    uint32_t remoteResNum; // 有效的remoteResNum
    RemoteResPtr remoteRes[AICPU_MAX_RANK_NUM]; //数组指针，指向HcclRankRelationResV2，下标为remoteUserRankId

    // communicate retry
    HDCommunicateParams kfcControlTransferH2DParams;
    HDCommunicateParams kfcStatusTransferD2HParams;
    uint64_t tinyMem; // for all2all
    uint64_t tinyMemSize;
    // 零拷贝场景使用
    uint64_t zeroCopyHeadPtr;
    uint64_t zeroCopyTailPtr;
    uint64_t zeroCopyRingBuffer;
    uint64_t zeroCopyIpcPtrs[MAX_MODULE_DEVICE_NUM]; // 保存集合通信时每个对端的输入输出内存地址
    uint32_t zeroCopyDevicePhyId[MAX_MODULE_DEVICE_NUM]; // 保存每个rank对应的物理卡Id

    bool utraceStatusFlag;
    OpCounterInfo opCounterInfo;
    HierarchicalAlgInfo hierarchicalAlgInfo;
    LocalResInfoV2 localRes;
    uint64_t debugConfig = 0; // 环境变量HCCL_DEBUG_CONFIG, 考虑兼容性放在结构体末尾

    // aicpu和custom进程需要交互的部分信息
    uint64_t aicpuCustomParamAddr;
    uint64_t aicpuCustomParamSize;

    MemDetails1 userMemRes[MAX_RANK_NUM_A3]; // 下标为rank id
    uint32_t userMemType = 0;

    HcclStreamParam aicpuOrderStreamParam; // 按序下发的stream
    uint64_t aicpuOrderNotifyAddr;
    uint64_t aicpuOrderNotifySize;
    // ARS算法属性
    uint32_t multiSuperPodDiffDeviceNumMode;
    bool isARSDoubleRing;
    // 读取HCCL_ENTRY_LOG_ENABLE环境变量，用于增加算子kernel展开信息
    bool opEntry{false};
    uint32_t hcclSdmaQos; // HCCL SDMA QOS TAG
    uint64_t sizeOfAiRMAInfo = 0; //用于内存校验
    uint64_t aiRMAInfo = 0; //HcclAiRMAInfo* 单个结构体指针
};

HcclResult HcclBarrier(HcclComm comm, aclrtStream stream);
HcclResult HcclCommInitClusterInfo(const char *clusterInfo, uint32_t rank, HcclComm *comm);
HcclResult HcclCommInitClusterInfoConfig(const char *clusterInfo, uint32_t rank, HcclCommConfig *config,
                                         HcclComm *comm);
HcclResult HcclCommInitRootInfo(uint32_t nRanks, const HcclRootInfo *rootInfo, uint32_t rank, HcclComm *comm);
HcclResult HcclCommInitRootInfoConfig(uint32_t nRanks, const HcclRootInfo *rootInfo, uint32_t rank,
                                      const HcclCommConfig *config, HcclComm *comm);

#ifdef __cplusplus
}
#endif // __cplusplus
#endif // HCCL_TYPES_H_
