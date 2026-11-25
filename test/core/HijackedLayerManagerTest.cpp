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


#include <gtest/gtest.h>

#include "core/HijackedLayerManager.h"

TEST(HijackedLayerManager, pop_on_empty_hijacked_layer_manager_expect_success)
{
    HijackedLayerManager &manager = HijackedLayerManager::Instance();
    manager.Layers().clear();
    manager.Pop();
    ASSERT_TRUE(manager.Layers().empty());
}

TEST(HijackedLayerManager, push_then_pop_expect_get_empty_layers)
{
    HijackedLayerManager &manager = HijackedLayerManager::Instance();
    manager.Layers().clear();
    manager.Push("aclrtMallocImpl");
    manager.Pop();
    ASSERT_TRUE(manager.Layers().empty());
}

TEST(HijackedLayerManager, push_twice_then_pop_once_expect_get_one_layer_remain)
{
    HijackedLayerManager &manager = HijackedLayerManager::Instance();
    manager.Layers().clear();
    manager.Push("aclrtMallocImpl");
    manager.Push("aclrtFreeImpl");
    manager.Pop();
    ASSERT_EQ(manager.Layers().size(), 1UL);
    ASSERT_EQ(manager.Layers().front(), "aclrtMallocImpl");
}

TEST(HijackedLayerManager, empty_layers_expect_mask_nothing)
{
    HijackedLayerManager &manager = HijackedLayerManager::Instance();
    manager.Layers().clear();
    ASSERT_FALSE(manager.ParentInCallStack("rtMalloc"));
}

TEST(HijackedLayerManager, push_higher_layer_expect_mask_corresponding_lower_layer)
{
    HijackedLayerManager &manager = HijackedLayerManager::Instance();
    manager.Layers().clear();
    manager.Push("aclrtMallocImpl");
    manager.Push("aclrtFreeImpl");
    ASSERT_TRUE(manager.ParentInCallStack("rtFree"));
    manager.Pop();
    manager.Pop();
}

TEST(HijackedLayerManager, push_higher_layer_expect_not_mask_other_lower_layer)
{
    HijackedLayerManager &manager = HijackedLayerManager::Instance();
    manager.Layers().clear();
    manager.Push("aclrtMallocImpl");
    ASSERT_FALSE(manager.ParentInCallStack("rtFree"));
    manager.Pop();
}

TEST(HijackedLayerManager, layer_guard_expect_tack_effect_in_scope)
{
    HijackedLayerManager &manager = HijackedLayerManager::Instance();
    manager.Layers().clear();
    {
        LayerGuard guard(manager, "aclrtMallocImpl");
        ASSERT_TRUE(manager.ParentInCallStack("rtMalloc"));
    }
    ASSERT_FALSE(manager.ParentInCallStack("rtMalloc"));
}

TEST(HijackedLayerManager, synchronous_acl_free_masks_only_nested_rt_free) {
    HijackedLayerManager manager;
    EXPECT_FALSE(manager.ParentInCallStack("rtFree"));
    {
        LayerGuard guard(manager, "aclrtFreeWithDevSyncImpl");
        EXPECT_TRUE(manager.ParentInCallStack("rtFree"));
        EXPECT_FALSE(manager.ParentInCallStack("rtMalloc"));
    }
    EXPECT_FALSE(manager.ParentInCallStack("rtFree"));
}
