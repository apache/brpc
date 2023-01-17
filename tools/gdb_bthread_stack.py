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
    1. gdb attach <pid>
    2. source gdb_bthread_stack.py
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

when call bthread_frame, registers will be modified,
remember to call bthread_end after debug, or process will be corrupted

after call bthread_frame, you can call `bt`/`up`/`down`, or other gdb command
"""

import gdb

bthreads = []
status = False

def get_bthread_num():
    root_agent = gdb.parse_and_eval("&(((((*bthread::g_task_control)._nbthreads)._combiner)._agents).root_)")
    global_res = int(gdb.parse_and_eval("((*bthread::g_task_control)._nbthreads)._combiner._global_result"))
    get_agent = "(*(('bvar::detail::AgentCombiner<long, long, bvar::detail::AddTo<long> >::Agent' *){}))"
    last_node = root_agent
    while True:
        agent = gdb.parse_and_eval(get_agent.format(last_node))
        if last_node != root_agent:
            val = int(agent["element"]["_value"]["_M_i"])
            global_res += val
        if agent["next_"] == root_agent:
            return global_res
        last_node = agent["next_"]

def get_all_bthreads(total):
    global bthreads
    bthreads = []
    count = 0
    groups = int(gdb.parse_and_eval("'butil::ResourcePool<bthread::TaskMeta>::_ngroup'")["val"])
    for group in range(groups):
        blocks = int(gdb.parse_and_eval("(*((*((('butil::static_atomic<butil::ResourcePool<bthread::TaskMeta>::BlockGroup*>' *)('butil::ResourcePool<bthread::TaskMeta>::_block_groups')) + {})).val)).nblock._M_i".format(group)))
        for block in range(blocks):
            items = int(gdb.parse_and_eval("(*(*(('butil::atomic<butil::ResourcePool<bthread::TaskMeta>::Block*>' *)((*((*((('butil::static_atomic<butil::ResourcePool<bthread::TaskMeta>::BlockGroup*>' *)('butil::ResourcePool<bthread::TaskMeta>::_block_groups')) + {})).val)).blocks) + {}))._M_b._M_p).nitem".format(group, block)))
            for item in range(items):
                task_meta = gdb.parse_and_eval("*(('bthread::TaskMeta' *)((*(*(('butil::atomic<butil::ResourcePool<bthread::TaskMeta>::Block*>' *)((*((*((('butil::static_atomic<butil::ResourcePool<bthread::TaskMeta>::BlockGroup*>' *)('butil::ResourcePool<bthread::TaskMeta>::_block_groups')) + {})).val)).blocks) + {}))._M_b._M_p).items) + {})".format(group, block, item))
                version_tid = (int(task_meta["tid"]) >> 32)
                version_butex = gdb.parse_and_eval("*(uint32_t *){}".format(task_meta["version_butex"]))
                if version_tid == int(version_butex) and int(task_meta["attr"]["stack_type"]) != 0:
                    bthreads.append(task_meta)
                    count += 1
                    if count >= total:
                        return

class BthreadListCmd(gdb.Command):
    """list all bthreads, print format is 'id\ttid\tfunction\thas stack'"""
    def __init__(self):
        gdb.Command.__init__(self, "bthread_list", gdb.COMMAND_STACK, gdb.COMPLETE_NONE)

    def invoke(self, arg, tty):
        global status
        global bthreads
        if not status:
            print("Not in bthread debug mode")
            return
        print("id\t\ttid\t\tfunction\t\thas stack\t\t\ttotal:{}".format(len(bthreads)))
        for i, t in enumerate(bthreads):
            print("#{}\t\t{}\t\t{}\t\t{}".format(i, t["tid"], t["fn"], "no" if str(t["stack"]) == "0x0" else "yes"))

class BthreadNumCmd(gdb.Command):
    """list active bthreads num"""
    def __init__(self):
        gdb.Command.__init__(self, "bthread_num", gdb.COMMAND_STACK, gdb.COMPLETE_NONE)

    def invoke(self, arg, tty):
        res = get_bthread_num()
        print(res)

class BthreadFrameCmd(gdb.Command):
    """bthread_frame <id>, select bthread frame by id"""
    def __init__(self):
        gdb.Command.__init__(self, "bthread_frame", gdb.COMMAND_STACK, gdb.COMPLETE_NONE)

    def invoke(self, arg, tty):
        global status
        global bthreads
        if not status:
            print("Not in bthread debug mode")
            return
        if not arg:
            print("bthread_frame <id>, see 'bthread_list'")
            return
        bthread_id = int(arg)
        if bthread_id >= len(bthreads):
            print("id {} exceeds max bthread nums {}".format(bthread_id, len(bthreads)))
            return
        stack = bthreads[bthread_id]["stack"]
        if str(stack) == "0x0":
            print("this bthread has no stack")
            return
        context = gdb.parse_and_eval("(*(('bthread::ContextualStack' *){})).context".format(stack))
        rip = gdb.parse_and_eval("*(uint64_t*)({}+7*8)".format(context))
        rbp = gdb.parse_and_eval("*(uint64_t*)({}+6*8)".format(context))
        rsp = gdb.parse_and_eval("{}+8*8".format(context))
        gdb.parse_and_eval("$rip = {}".format(rip))
        gdb.parse_and_eval("$rsp = {}".format(rsp))
        gdb.parse_and_eval("$rbp = {}".format(rbp))

class BthreadRegsCmd(gdb.Command):
    """bthread_regs <id>, print bthread registers"""
    def __init__(self):
        gdb.Command.__init__(self, "bthread_regs", gdb.COMMAND_STACK, gdb.COMPLETE_NONE)

    def invoke(self, arg, tty):
        global status
        global bthreads
        if not status:
            print("Not in bthread debug mode")
            return
        if not arg:
            print("bthread_regs <id>, see 'bthread_list'")
            return
        bthread_id = int(arg)
        if bthread_id >= len(bthreads):
            print("id {} exceeds max bthread nums {}".format(bthread_id, len(bthreads)))
            return
        stack = bthreads[bthread_id]["stack"]
        if str(stack) == "0x0":
            print("this bthread has no stack")
            return
        context = gdb.parse_and_eval("(*(('bthread::ContextualStack' *){})).context".format(stack))
        rip = int(gdb.parse_and_eval("*(uint64_t*)({}+7*8)".format(context)))
        rbp = int(gdb.parse_and_eval("*(uint64_t*)({}+6*8)".format(context)))
        rbx = int(gdb.parse_and_eval("*(uint64_t*)({}+5*8)".format(context)))
        r15 = int(gdb.parse_and_eval("*(uint64_t*)({}+4*8)".format(context)))
        r14 = int(gdb.parse_and_eval("*(uint64_t*)({}+3*8)".format(context)))
        r13 = int(gdb.parse_and_eval("*(uint64_t*)({}+2*8)".format(context)))
        r12 = int(gdb.parse_and_eval("*(uint64_t*)({}+1*8)".format(context)))
        rsp = int(gdb.parse_and_eval("{}+8*8".format(context)))
        print("rip: 0x{:x}\nrsp: 0x{:x}\nrbp: 0x{:x}\nrbx: 0x{:x}\nr15: 0x{:x}\nr14: 0x{:x}\nr13: 0x{:x}\nr12: 0x{:x}".format(rip, rsp, rbp, rbx, r15, r14, r13, r12))

class BthreadMetaCmd(gdb.Command):
    """bthread_meta <id>, print task meta by id"""
    def __init__(self):
        gdb.Command.__init__(self, "bthread_meta", gdb.COMMAND_STACK, gdb.COMPLETE_NONE)

    def invoke(self, arg, tty):
        global status
        global bthreads
        if not status:
            print("Not in bthread debug mode")
            return
        if not arg:
            print("bthread_meta <id>, see 'bthread_list'")
            return
        bthread_id = int(arg)
        if bthread_id >= len(bthreads):
            print("id {} exceeds max bthread nums {}".format(bthread_id, len(bthreads)))
            return
        print(bthreads[bthread_id])

class BthreadBeginCmd(gdb.Command):
    """enter bthread debug mode"""
    def __init__(self):
        gdb.Command.__init__(self, "bthread_begin", gdb.COMMAND_STACK, gdb.COMPLETE_NONE)

    def invoke(self, arg, tty):
        global status
        if status:
            print("Already in bthread debug mode, do not switch thread before exec 'bthread_end' !!!")
            return
        active_bthreads = get_bthread_num()
        scanned_bthreads = active_bthreads
        if arg:
            num_arg = int(arg)
            if num_arg < active_bthreads:
                scanned_bthreads = num_arg
            else:
                print("requested bthreads {} more than actived, will display {} bthreads".format(num_arg, scanned_bthreads))
        print("Active bthreads: {}, will display {} bthreads".format(active_bthreads, scanned_bthreads))
        get_all_bthreads(scanned_bthreads)
        gdb.parse_and_eval("$saved_rip = $rip")
        gdb.parse_and_eval("$saved_rsp = $rsp")
        gdb.parse_and_eval("$saved_rbp = $rbp")
        status = True
        print("Enter bthread debug mode, do not switch thread before exec 'bthread_end' !!!")

class BthreadRegRestoreCmd(gdb.Command):
    """restore registers"""
    def __init__(self):
        gdb.Command.__init__(self, "bthread_reg_restore", gdb.COMMAND_STACK, gdb.COMPLETE_NONE)

    def invoke(self, arg, tty):
        global status
        if not status:
            print("Not in bthread debug mode")
            return
        gdb.parse_and_eval("$rip = $saved_rip")
        gdb.parse_and_eval("$rsp = $saved_rsp")
        gdb.parse_and_eval("$rbp = $saved_rbp")
        print("OK")

class BthreadEndCmd(gdb.Command):
    """exit bthread debug mode"""
    def __init__(self):
        gdb.Command.__init__(self, "bthread_end", gdb.COMMAND_STACK, gdb.COMPLETE_NONE)

    def invoke(self, arg, tty):
        global status
        if not status:
            print("Not in bthread debug mode")
            return
        gdb.parse_and_eval("$rip = $saved_rip")
        gdb.parse_and_eval("$rsp = $saved_rsp")
        gdb.parse_and_eval("$rbp = $saved_rbp")
        status = False
        print("Exit bthread debug mode")

BthreadListCmd()
BthreadNumCmd()
BthreadBeginCmd()
BthreadEndCmd()
BthreadFrameCmd()
BthreadMetaCmd()
BthreadRegRestoreCmd()
BthreadRegsCmd()
