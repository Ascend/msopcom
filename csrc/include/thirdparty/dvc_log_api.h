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

#ifndef __AIC_LOG_API_H__
#define __AIC_LOG_API_H__

#include <cstdint>

#ifndef MODEL_API
#define MODEL_API __attribute__((visibility("default")))
#endif

typedef struct DvcInstrLogEntry {
    uint32_t core_id;
    uint32_t sub_core_id;
    uint64_t pc;
    const char* decode_descr;
    const char* exec_descr;
} DvcInstrLogEntry_t;

typedef struct DvcMteLogEntry {
    uint32_t core_id;
    uint32_t sub_core_id; // for mte, sub_core_id is 0
    const char* op;

    union DataT {
        // op: send_cmd, recv_data, recv_cmd_rsp, recv_wr_data_rsp
        // intf: BRIF, BWIF, (mte intf name)
        struct BifOpInfoT {
            const char* intf;
            uint32_t port;
            uint64_t addr;
            uint64_t size;
            uint64_t gid;
            uint32_t retire_set;
            uint64_t instr_id;
            uint32_t req_id;
        } bif_op_info;
        // recv_rsp, send_req, send_data
        // intf: (mte intf name)
        struct IntfOpInfoT {
            const char* intf;
            uint32_t port;
            uint64_t addr;
            uint64_t size;
            uint32_t tag_id;
            uint64_t instr_id;
            uint32_t req_id;
        } intf_op_info;
        // op: brif_biu_uop_split
        struct BiuUopSplitInfoT {
            uint32_t thread_id;
            uint64_t read_size;
            uint32_t retire_set;
            uint32_t instr_id;
        } biu_uop_split_info;
        // op: bwif_send_data
        struct SendDataInfoT {
            uint32_t port;
            uint32_t thread_id;
            uint64_t addr;
            uint64_t size;
            uint64_t gid;
            uint32_t stbidx;
            uint32_t retire_set;
            uint32_t instr_id;
        } send_data_info;
        // op: early_retire
        struct EarlyRetireInfoT {
            uint32_t sub_id;
            uint32_t thread_id;
            uint32_t pipe;
            uint64_t instr_id;
        } early_retire_info;
        // op: retire
        struct RetireInfoT {
            uint64_t instr_id;
        } retire_info;
    } data;
} DvcMteLogEntry_t;

typedef struct DvcIcacheLogEntry {
    uint32_t core_id;
    uint32_t sub_core_id;
    const char* op;

    union DataT {
        // op: preload
        struct PreloadInfoT {
            uint64_t gid;
            uint64_t addr;
            uint32_t size;
            uint8_t last;
        } preload_info;
        // op: miss_read
        struct MissReadInfoT {
            uint64_t addr;
            uint32_t size;
            uint32_t type;
            uint8_t last;
        } miss_read_info;
        // op: prefetch
        struct PrefetchInfoT {
            uint64_t gid;
            uint64_t addr;
            uint32_t size;
            uint8_t last;
        } prefetch_info;
        // op: hit_read
        struct HitReadInfoT {
            uint64_t addr;
            uint32_t size;
        } hit_read_info;
        // op: send
        struct SendInfoT {
            uint64_t addr;
            uint64_t gid;
        } send_info;
        // op: fetch_req
        struct FetchReqInfoT {
            uint64_t addr;
        } fetch_req_info;
        // op: prefetch_req
        struct PrefetchReqInfoT {
            uint64_t addr;
        } prefetch_req_info;
        // op: preload_req
        struct PreloadReqInfoT {
            uint64_t addr;
        } preload_req_info;
        // op: send_req
        struct SendReqInfoT {
            uint64_t addr;
            uint32_t size;
        } send_req_info;
        // op: refill_req
        struct RefillReqInfoT {
            uint64_t gid;
            uint64_t addr;
            uint32_t idx_addr;
            uint32_t victim;
        } refill_req_info;
        // op: send_refill
        struct SendRefillInfoT {
            uint64_t addr;
            uint64_t gid;
        } send_refill_info;
    } data;
} DvcIcacheLogEntry_t;

typedef struct DvcCcuLogEntry {
    uint32_t core_id;
    uint32_t sub_core_id;
    const char* op; // DECODE, D1_STAGE, ISSUE, EXECUTE, BRANCH, BARRIER, etc.

    union DataT {
        // op: DECODE
        struct DecodeInfoT {
            uint32_t isa_type;
            uint32_t isa_latency;
            uint64_t pc;
            uint64_t instr_id;
        } decode_info;
        // op: D1_STAGE (D1 Stage to I1 success)
        struct D1StageInfoT {
            const char* instr_name;
            uint64_t pc;
            uint64_t instr_id;
        } d1_stage_info;
        // op: ISSUE (issue instr with name/pc/id or hazard/stall)
        struct IssueInfoT {
            const char* instr_name;
            uint64_t pc;
            uint64_t instr_id;
            uint8_t hazard;       // 0: no hazard, 1: general hazard, 2: spr hazard, 3: data hazard
            uint8_t hazard_type;  // spr RAW/WAR/WAW sub-type
            uint32_t hazard_spr;  // spr index when spr hazard
        } issue_info;
        // op: EXECUTE
        struct ExecuteInfoT {
            const char* instr_name;
            uint64_t pc;
            uint64_t instr_id;
        } execute_info;
        // op: BRANCH
        struct BranchInfoT {
            uint64_t target_pc;
            uint64_t instr_pc;
            uint64_t instr_id;
            uint8_t is_mispredict;
        } branch_info;
        // op: BARRIER
        struct BarrierInfoT {
            const char* instr_name;
            uint64_t pc;
            uint64_t instr_id;
        } barrier_info;
        // op: PUSH_QUEUE
        struct PushQueueInfoT {
            const char* instr_name;
            const char* pipe;
            uint64_t pc;
            uint64_t instr_id;
        } push_queue_info;
        // op: VF_INSTR
        struct VfInstrInfoT {
            uint64_t pc;
            uint64_t instr_id;
            uint64_t vpc;
            uint32_t slot_id;
        } vf_instr_info;
        // op: PUSH_PB
        struct PushPbInfoT {
            uint64_t pc;
            uint64_t instr_id;
            uint32_t slot_id;
        } push_pb_info;
        // op: GET_BUF / RLS_BUF
        struct BufInfoT {
            uint32_t buf_id;
            uint64_t pc;
            const char* pipe;
        } buf_info;
        // op: SET_FLAG / WAIT_FLAG
        struct FlagInfoT {
            uint64_t pc;
            uint64_t instr_id;
            uint32_t flag_id;
        } flag_info;
        // op: END
        struct EndInfoT {
            uint64_t pc;
            uint32_t sub_core_id;
        } end_info;
        // op: ENDLABEL
        struct EndLabelInfoT {
            // no extra data needed
            uint32_t dummy;
        } endlabel_info;
        // op: INTRA_BLOCK
        struct IntraBlockInfoT {
            uint64_t pc;
            uint64_t instr_id;
            uint32_t block_id;
            uint32_t connect_id;
        } intra_block_info;
        // op: DSB
        struct DsbInfoT {
            uint32_t dsb_pb_map_size;
            uint32_t dsb_ub_map_size;
        } dsb_info;
        // op: LOOP
        struct LoopInfoT {
            uint64_t pc;
            uint64_t start_pc;
            uint32_t iter;
        } loop_info;
        // op: DCCI
        struct DcciInfoT {
            uint64_t pc;
            uint64_t instr_id;
        } dcci_info;
        // op: ISSUE_SUCCESS (is_single_issue / do_issue success)
        struct IssueSuccessInfoT {
            uint64_t pc;
            const char* instr_name;
            const char* pipe;
        } issue_success_info;
    } data;
} DvcCcuLogEntry_t;

typedef struct DvcIfuLogEntry {
    uint32_t core_id;
    uint32_t sub_core_id;
    const char* op;

    union DataT {
        // op: fetch
        struct FetchInfoT {
            uint64_t pc;
            uint32_t fetch_size;
        } fetch_info;
        // op: dispatch
        struct DispatchInfoT {
            const char* instr_name;
            uint64_t pc;
            uint64_t instr_id;
        } dispatch_info;
        // op: update_pred_info
        struct UpdatePredInfoT {
            uint64_t start_pc;
            uint32_t instr_num;
            uint32_t start_pos;
            uint32_t pred_loc;
            uint32_t end_pos;
        } update_pred_info;
        // op: decode
        struct DecodeInfoT {
            uint64_t pc;
            uint32_t raw;
            const char* instr_name;
            uint64_t instr_id;
        } decode_info;
        // op: get_predict_pc
        struct GetPredictPCInfoT {
            uint64_t start_pc;
            uint64_t pred_pc;
            uint32_t predict_loc;
        } get_predict_pc_info;
        // op: check_branch_instr_type
        struct CheckBranchInstrTypeInfoT {
            uint64_t pc;
            uint64_t instr_id;
            uint8_t is_branch_xn;
        } check_branch_instr_type_info;
        // op: handle_branch_instr
        struct HandleBranchInstrInfoT {
            uint64_t pc;
            uint64_t instr_id;
            uint32_t pht_index;
            uint64_t pred_pc;
            uint64_t next_pc;
        } handle_branch_instr_info;
        // op: set_branch_target
        struct SetBranchTargetInfoT {
            uint64_t target;
            uint64_t pc;
        } set_branch_target_info;
    } data;
} DvcIfuLogEntry_t;

// Callback function types
// Arguments: time, entry
typedef void (*DvcInstrLogCb_t)(uint64_t, const DvcInstrLogEntry_t*);
typedef void (*DvcMteLogCb_t)(uint64_t, const DvcMteLogEntry_t*);
typedef void (*DvcIcacheLogCb_t)(uint64_t, const DvcIcacheLogEntry_t*);
typedef void (*DvcIfuLogCb_t)(uint64_t, const DvcIfuLogEntry_t*);
typedef void (*DvcCcuLogCb_t)(uint64_t, const DvcCcuLogEntry_t*);

typedef enum DvcLogType {
    DVC_INSTR_POPPED_LOG = 0,
    DVC_INSTR_LOG = 1,
    DVC_MTE_LOG = 2,
    DVC_ICACHE_LOG = 3,
    DVC_IFU_LOG = 4,
    DVC_CCU_LOG = 5,
    DVC_UNDEF = 0xff
} DvcLogType_t;

typedef union DvcLogCbFnUnion {
    DvcInstrLogCb_t instrLogCb;
    DvcMteLogCb_t mteLogCb;
    DvcIcacheLogCb_t icacheLogCb;
    DvcIfuLogCb_t ifuLogCb;
    DvcCcuLogCb_t ccuLogCb;
} DvcLogCbFnUnion_t;

extern "C" {
// trace = 0, debug = 1, info = 2, warn = 3, error = 4, critical = 5, off = 6
MODEL_API void DvcSetLogLevel(
    const uint32_t file_print_level, const uint32_t screen_print_level, const uint32_t flush_level
);
MODEL_API void DvcSetLogRotation(const uint64_t rotating_file_size, const uint32_t rotating_file_number);
MODEL_API void DvcAttachLogCallback(DvcLogType_t log_type, DvcLogCbFnUnion_t fn_union);

} // extern "C"

#endif // __AIC_LOG_API_H__
