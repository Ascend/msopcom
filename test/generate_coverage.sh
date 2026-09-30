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
echo "***************Generate Coverage*****************"
date

CUR_DIR=$(dirname $(readlink -f $0))
TOP_DIR=${CUR_DIR}/..
COVERAGE_PATH=${TOP_DIR}/build_ut/coverage

if [ -d "${COVERAGE_PATH}" ]; then
    rm -rf ${COVERAGE_PATH}
fi

mkdir -p ${COVERAGE_PATH}

lcov_opt="--rc branch_coverage=1 --rc no_exception_branch=1 --no-external --quiet"
SET_SRC_DIR=`find ${TOP_DIR}/csrc -type d -not -path '**/tools**' | awk '{print "-d "$1}'`
SET_TOOLS_SRC_DIR=`find ${TOP_DIR}/csrc/tools -type d | awk '{print "-d "$1}'`

lcov -c $SET_SRC_DIR -d ${TOP_DIR}/build_ut/test/CMakeFiles/core_test_obj.dir -o ${COVERAGE_PATH}/core_test_obj.info $lcov_opt
lcov -c $SET_SRC_DIR -d ${TOP_DIR}/build_ut/csrc/CMakeFiles/core_obj.dir/ -o ${COVERAGE_PATH}/core_obj.info $lcov_opt
lcov -c $SET_SRC_DIR -d ${TOP_DIR}/build_ut/csrc/CMakeFiles/injection_test_obj.dir/ -o ${COVERAGE_PATH}/injection_test_obj.info $lcov_opt
lcov -c $SET_TOOLS_SRC_DIR -d ${TOP_DIR}/build_ut/csrc/tools/kernel_launcher/CMakeFiles/kernel_launcher_obj.dir -o ${COVERAGE_PATH}/kernel_launcher_obj.info $lcov_opt
lcov $SET_SRC_DIR -a ${COVERAGE_PATH}/core_test_obj.info -a ${COVERAGE_PATH}/injection_test_obj.info -a ${COVERAGE_PATH}/core_obj.info -a ${COVERAGE_PATH}/kernel_launcher_obj.info -o ${COVERAGE_PATH}/funcInjectionCoverage.info $lcov_opt

genhtml ${COVERAGE_PATH}/funcInjectionCoverage.info -o ${COVERAGE_PATH}/report --branch-coverage --rc genhtml_branch_coverage=1

cd ${COVERAGE_PATH}
tar -zcvf report.tar.gz ${COVERAGE_PATH}/report

date
echo "***************Generate Coverage Done*****************"
