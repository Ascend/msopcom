#!/bin/bash
# -------------------------------------------------------------------------
# This file is part of the MindStudio project.
# Copyright (c) 2026 Huawei Technologies Co.,Ltd.
#
# MindStudio is licensed under Mulan PSL v2.
# You can use this software according to the terms and conditions of the Mulan PSL v2.
# You may obtain a copy of Mulan PSL v2 at:
#
#          http://license.coscl.org.cn/MulanPSL2
#
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
# EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
# MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
# See the Mulan PSL v2 for more details.
# -------------------------------------------------------------------------
set -euo pipefail

CUR_DIR=$(dirname "$(readlink -f "$0")")
TOP_DIR=$(readlink -f "${CUR_DIR}/..")
COVERAGE_PATH=${TOP_DIR}/build_ut/coverage

lcov_opt="--rc branch_coverage=1 --rc no_exception_branch=1 --no-external --quiet"
SET_SRC_DIR=$(find "${TOP_DIR}/csrc" -type d -not -path '**/tools**' | awk '{print "-d "$1}')
SET_TOOLS_SRC_DIR=$(find "${TOP_DIR}/csrc/tools" -type d | awk '{print "-d "$1}')

collect_coverage() {
    local out_dir=$1
    mkdir -p "${out_dir}"

    lcov -c $SET_SRC_DIR -d "${TOP_DIR}/build_ut/test/CMakeFiles/core_test_obj.dir" -o "${out_dir}/core_test_obj.info" $lcov_opt
    lcov -c $SET_SRC_DIR -d "${TOP_DIR}/build_ut/csrc/CMakeFiles/core_obj.dir/" -o "${out_dir}/core_obj.info" $lcov_opt
    lcov -c $SET_SRC_DIR -d "${TOP_DIR}/build_ut/csrc/CMakeFiles/injection_test_obj.dir/" -o "${out_dir}/injection_test_obj.info" $lcov_opt
    lcov -c $SET_TOOLS_SRC_DIR -d "${TOP_DIR}/build_ut/csrc/tools/kernel_launcher/CMakeFiles/kernel_launcher_obj.dir" -o "${out_dir}/kernel_launcher_obj.info" $lcov_opt
    lcov $SET_SRC_DIR -a "${out_dir}/core_test_obj.info" -a "${out_dir}/injection_test_obj.info" -a "${out_dir}/core_obj.info" -a "${out_dir}/kernel_launcher_obj.info" -o "${out_dir}/funcInjectionCoverage.info" $lcov_opt
}

# 覆盖率门禁模式：仅输出 JSON 汇总，不生成报告文件。
if [ "${1:-}" = "--check" ]; then
    CHECK_DIR=$(mktemp -d)
    trap 'rm -rf "${CHECK_DIR}"' EXIT
    collect_coverage "${CHECK_DIR}"
    read -r TOTAL COVERED < <(awk -F: '/^LF:/{lf+=$2} /^LH:/{lh+=$2} END{printf "%d %d\n", lf, lh}' \
        "${CHECK_DIR}/funcInjectionCoverage.info")
    printf '{"line_total": %d, "line_covered": %d}\n' "${TOTAL}" "${COVERED}"
    exit 0
fi

echo "***************Generate Coverage*****************"
date

if [ -d "${COVERAGE_PATH}" ]; then
    rm -rf "${COVERAGE_PATH}"
fi

collect_coverage "${COVERAGE_PATH}"

genhtml "${COVERAGE_PATH}/funcInjectionCoverage.info" -o "${COVERAGE_PATH}/report" --branch-coverage --rc genhtml_branch_coverage=1

cd "${COVERAGE_PATH}"
tar -zcvf report.tar.gz ./report

date
echo "***************Generate Coverage Done*****************"
