#!/usr/bin/env python
# coding=utf-8

# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.

"""
Bthread Stack Print Tool

this only for running process, core dump is not supported.

Get Started:
    1. lldb attach -p <pid>
    2. command script import lldb_bthread_stack.py
    3. bthread_begin
    4. bthread_list
    5. bthread_frame 0
    6. bt / up / down
    7. bthread_end

Commands:
    1. bthread_num: print all bthread nums
    2. bthread_begin <num>: enter bthread debug mode, `num` is max scanned bthreads, default will scan all
    3. bthread_list: list all bthreads
    4. bthread_frame <id>: switch stack to bthread, id will displayed in bthread_list
    5. bthread_meta <id>: print bthread meta
    6. bthread_reg_restore: bthread_frame will modify registers, reg_restore will restore them
    7. bthread_end: exit bthread debug mode
    8. bthread_regs <id>: print bthread registers
    9. bthread_all: print all bthread frames

when call bthread_frame, registers will be modified,
remember to call bthread_end after debug, or process will be corrupted

after call bthread_frame, you can call `bt`/`up`/`down`, or other gdb command
"""

import lldb


class GlobalState():
    def __init__(self):
        self.started: bool = False
        self.bthreads: list = []
        self.saved_regs: dict = {}

    def reset(self) -> None:
        self.started = False
        self.bthreads.clear()

    def get_bthread(self, idx_str: str) -> lldb.SBValue:
        if not self.started:
            print("Not in bthread debug mode")
            return None
        if len(idx_str) == 0:
            print("bthread_frame <id>, see 'bthread_list'")
        try:
            bthread_idx = int(idx_str)
        except ValueError:
            print("please input a valid interger.")
            return None

        if bthread_idx >= len(self.bthreads):
            print("id {} exceeds max bthread nums {}".format(
                bthread_idx, len(self.bthreads)))
            return None
        return self.bthreads[bthread_idx]


global_state = GlobalState()


def get_child(value: lldb.SBValue, childs_value_name: str) -> lldb.SBValue:
    r"""get child value by value name str split by '.'"""
    result = value
    childs_value_list = childs_value_name.split('.')
    for child_value_name in childs_value_list:
        result = result.GetChildMemberWithName(child_value_name)
    return result


def find_global_value(target: lldb.SBTarget, value_name: str) -> lldb.SBValue:
    r""" find global value by value name"""
    name_list = value_name.split('.')
    root_name = name_list[0]
    root_value = target.FindGlobalVariables(
        root_name, 1, lldb.eMatchTypeNormal)[0]
    if (len(name_list) == 1):
        return root_value
    return get_child(root_value, '.'.join(name_list[1:]))


def get_bthreads_num(target: lldb.SBTarget):
    root_agent = find_global_value(
        target, "bthread::g_task_control._nbthreads._combiner._agents.root_")
    global_res = find_global_value(
        target, "bthread::g_task_control._nbthreads._combiner._global_result").GetValueAsSigned()
    long_type = target.GetBasicType(lldb.eBasicTypeLong)

    last_node = root_agent
    # agent_type: bvar::detail::AgentCombiner<long, long, bvar::detail::AddTo<long> >::Agent>
    agent_type: lldb.SBType = last_node.GetType().GetTemplateArgumentType(0)
    while True:
        agent = last_node.Cast(agent_type)
        if (last_node.GetLocation() != root_agent.GetLocation()):
            val = get_child(agent, "element._value").Cast(
                long_type).GetValueAsSigned()
            global_res += val
        if (get_child(agent, "next_").Dereference().GetLocation() == root_agent.GetLocation()):
            return global_res
        last_node = get_child(agent, "next_").Dereference()


def get_all_bthreads(target: lldb.SBTarget, total: int):
    bthreads = []
    groups = find_global_value(
        target, "butil::ResourcePool<bthread::TaskMeta>::_ngroup.val").GetValueAsUnsigned()
    long_type = target.GetBasicType(lldb.eBasicTypeLong)
    uint32_t_type = target.FindFirstType("uint32_t")
    block_groups = find_global_value(
        target, "butil::ResourcePool<bthread::TaskMeta>::_block_groups")
    for group in range(groups):
        block_group = get_child(
            block_groups.GetChildAtIndex(group), "val").Dereference()
        nblock = get_child(block_group, "nblock").Cast(
            long_type).GetValueAsUnsigned()
        blocks = get_child(block_group, "blocks")
        for block in range(nblock):
            # block_type: butil::ResourcePool<bthread::TaskMeta>::Block
            block_type = blocks.GetChildAtIndex(
                block).GetType().GetTemplateArgumentType(0)
            block = blocks.GetChildAtIndex(
                block).Cast(block_type).Dereference()
            nitem = get_child(block, "nitem").GetValueAsUnsigned()
            task_meta_array_type = target.FindFirstType(
                "bthread::TaskMeta").GetArrayType(nitem)
            tasks = get_child(block, "items").Cast(task_meta_array_type)
            for i in range(nitem):
                task_meta = tasks.GetChildAtIndex(i)
                version_tid = get_child(
                    task_meta, "tid").GetValueAsUnsigned() >> 32
                version_butex = get_child(task_meta, "version_butex").Cast(
                    uint32_t_type.GetPointerType()).Dereference().GetValueAsUnsigned()
                # stack_type: bthread::ContextualStack
                stack_type = get_child(
                    task_meta, "attr.stack_type").GetValueAsUnsigned()
                if version_tid == version_butex and stack_type != 0:
                    if len(bthreads) >= total:
                        return bthreads
                    bthreads.append(task_meta)
    return bthreads

# lldb bthread commands
def bthread_begin(debugger, command, result, internal_dict):
    if global_state.started:
        print("Already in bthread debug mode, do not switch thread before exec 'bthread_end' !!!")
        return
    target = debugger.GetSelectedTarget()
    active_bthreads = get_bthreads_num(target)

    if len(command) == 0:
        request_bthreds = active_bthreads
    else:
        try:
            request_bthreds = int(command)
        except ValueError:
            print("please input a valid interger.")
            return

    scanned_bthreds = active_bthreads
    if request_bthreds > active_bthreads:
        print("requested bthreads {} more than actived, will display {} bthreads".format(
            request_bthreds, active_bthreads))
    else:
        scanned_bthreds = request_bthreds
    print("Active bthreads: {}, will display {} bthreads".format(
        active_bthreads, scanned_bthreds))
    global_state.bthreads = get_all_bthreads(target, scanned_bthreds)

    # backup registers
    current_frame = target.GetProcess().GetSelectedThread().GetSelectedFrame()
    saved_regs = dict()
    saved_regs["rip"] = current_frame.FindRegister("rip").GetValueAsUnsigned()
    saved_regs["rsp"] = current_frame.FindRegister("rsp").GetValueAsUnsigned()
    saved_regs["rbp"] = current_frame.FindRegister("rbp").GetValueAsUnsigned()
    global_state.saved_regs = saved_regs

    global_state.started = True
    print("Enter bthread debug mode, do not switch thread before exec 'bthread_end' !!!")


def bthread_list(debugger, command, result, internal_dict):
    r"""list all bthreads, print format is 'id\ttid\tfunction\thas stack'"""
    if not global_state.started:
        print("Not in bthread debug mode")
        return

    print("id\t\ttid\t\tfunction\t\t\t\thas stack\t\t\ttotal:{}".format(
        len(global_state.bthreads)))
    for i, t in enumerate(global_state.bthreads):
        tid = get_child(t, "tid").GetValueAsUnsigned()
        fn = get_child(t, "fn")
        has_stack = get_child(t, "stack").GetLocation() == "0x0"
        print("#{}\t\t{}\t\t{}\t\t{}".format(
            i, tid, fn, "no" if has_stack else "yes"))


def bthread_num(debugger, command, result, internal_dict):
    r"""list active bthreads num"""
    if not global_state.started:
        print("Not in bthread debug mode")
        return

    target = debugger.GetSelectedTarget()
    active_bthreads = get_bthreads_num(target)
    print(active_bthreads)


def bthread_frame(debugger, command, result, internal_dict):
    r"""bthread_frame <id>, select bthread frame by id"""
    bthread = global_state.get_bthread(command)
    if bthread is None:
        return

    stack = bthread.GetChildMemberWithName("stack")
    context = stack.Dereference().GetChildMemberWithName("context")

    target = debugger.GetSelectedTarget()
    uint64_t_type = target.FindFirstType("uint64_t")
    target = debugger.GetSelectedTarget()

    rip = target.CreateValueFromAddress("rip",  lldb.SBAddress(
        context.GetValueAsUnsigned() + 7*8, target), uint64_t_type).GetValueAsUnsigned()
    rbp = target.CreateValueFromAddress("rbp",  lldb.SBAddress(
        context.GetValueAsUnsigned() + 6*8, target), uint64_t_type).GetValueAsUnsigned()
    rsp = context.GetValueAsUnsigned() + 8*8

    debugger.HandleCommand(f"register write rip {rip}")
    debugger.HandleCommand(f"register write rbp {rbp}")
    debugger.HandleCommand(f"register write rsp {rsp}")


def bthread_all(debugger, command, result, internal_dict):
    r"""print all bthread frames"""
    if not global_state.started:
        print("Not in bthread debug mode")
        return

    bthreads = global_state.bthreads
    bthread_num = len(bthreads)
    for i in range(bthread_num):
        bthread_frame(debugger, str(i), result, internal_dict)
        debugger.HandleCommand("bt")


def bthread_meta(debugger, command, result, internal_dict):
    r"""bthread_meta <id>, print task meta by id"""
    bthread = global_state.get_bthread(command)
    if bthread is None:
        return
    print(bthread)


def bthread_regs(debugger, command, result, internal_dict):
    r"""bthread_regs <id>, print bthread registers"""
    bthread = global_state.get_bthread(command)
    if bthread is None:
        return
    target = debugger.GetSelectedTarget()
    stack = get_child(bthread, "stack").Dereference()
    context = get_child(stack, "context")
    ctx_addr = context.GetValueAsUnsigned()
    uint64_t_type = target.FindFirstType("uint64_t")

    rip = target.CreateValueFromAddress("rip",  lldb.SBAddress(
        ctx_addr + 7*8, target), uint64_t_type).GetValueAsUnsigned()
    rbp = target.CreateValueFromAddress("rbp",  lldb.SBAddress(
        ctx_addr + 6*8, target), uint64_t_type).GetValueAsUnsigned()
    rbx = target.CreateValueFromAddress("rbx",  lldb.SBAddress(
        ctx_addr + 5*8, target), uint64_t_type).GetValueAsUnsigned()
    r15 = target.CreateValueFromAddress("r15",  lldb.SBAddress(
        ctx_addr + 4*8, target), uint64_t_type).GetValueAsUnsigned()
    r14 = target.CreateValueFromAddress("r14",  lldb.SBAddress(
        ctx_addr + 3*8, target), uint64_t_type).GetValueAsUnsigned()
    r13 = target.CreateValueFromAddress("r13",  lldb.SBAddress(
        ctx_addr + 2*8, target), uint64_t_type).GetValueAsUnsigned()
    r12 = target.CreateValueFromAddress("r12",  lldb.SBAddress(
        ctx_addr + 1*8, target), uint64_t_type).GetValueAsUnsigned()
    rsp = ctx_addr + 8*8

    print("rip: 0x{:x}\nrsp: 0x{:x}\nrbp: 0x{:x}\nrbx: 0x{:x}\nr15: 0x{:x}\nr14: 0x{:x}\nr13: 0x{:x}\nr12: 0x{:x}".format(
        rip, rsp, rbp, rbx, r15, r14, r13, r12))


def bthread_reg_restore(debugger, command, result, internal_dict):
    r"""restore registers"""
    if not global_state.started:
        print("Not in bthread debug mode")
        return
    for reg_name, reg_value in global_state.saved_regs.items():
        debugger.HandleCommand(f"register write {reg_name} {reg_value}")


def bthread_end(debugger, command, result, internal_dict):
    r"""exit bthread debug mode"""
    if not global_state.started:
        print("Not in bthread debug mode")
        return
    bthread_reg_restore(debugger, command, result, internal_dict)
    global_state.reset()
    print("Exit bthread debug mode")


# And the initialization code to add commands.
def __lldb_init_module(debugger, internal_dict):
    debugger.HandleCommand(
        'command script add -f lldb_bthread_stack.bthread_begin bthread_begin')
    debugger.HandleCommand(
        'command script add -f lldb_bthread_stack.bthread_list bthread_list')
    debugger.HandleCommand(
        'command script add -f lldb_bthread_stack.bthread_frame bthread_frame')
    debugger.HandleCommand(
        'command script add -f lldb_bthread_stack.bthread_num bthread_num')
    debugger.HandleCommand(
        'command script add -f lldb_bthread_stack.bthread_all bthread_all')
    debugger.HandleCommand(
        'command script add -f lldb_bthread_stack.bthread_meta bthread_meta')
    debugger.HandleCommand(
        'command script add -f lldb_bthread_stack.bthread_regs bthread_regs')
    debugger.HandleCommand(
        'command script add -f lldb_bthread_stack.bthread_reg_restore bthread_reg_restore')
    debugger.HandleCommand(
        'command script add -f lldb_bthread_stack.bthread_end bthread_end')
