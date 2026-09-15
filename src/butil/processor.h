// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

#ifndef BUTIL_PROCESSOR_H
#define BUTIL_PROCESSOR_H

#include "butil/build_config.h"

// Pause instruction to prevent excess processor bus usage, only works in GCC
#ifndef cpu_relax
#if defined(ARCH_CPU_ARM_FAMILY)
#define cpu_relax() asm volatile("yield\n": : :"memory")
#elif defined(ARCH_CPU_RISCV_FAMILY)
// Use the pause hint (Zihintpause extension). Encoding 0x0100000F
// (fence 0, 1) is a HINT on all RISC-V implementations: it never traps
// and is ignored on CPUs without Zihintpause. On CPUs with Zihintpause
// it provides a multi-cycle stall hint that reduces power and improves
// resource fairness during spin-wait loops. Matches the Linux kernel's
// RISC-V cpu_relax() behavior. .word is used instead of .insn or the
// pause mnemonic for maximum assembler compatibility.
# define cpu_relax() asm volatile(".word 0x0100000f\n": : :"memory")
#elif defined(ARCH_CPU_LOONGARCH64_FAMILY)
# define cpu_relax() asm volatile("nop\n": : :"memory")
#else
# define cpu_relax() asm volatile("pause\n": : :"memory")
#endif
#endif // cpu_relax

// Compile read-write barrier
#ifndef barrier
#define barrier() asm volatile("": : :"memory")
#endif // barrier

#endif // BUTIL_PROCESSOR_H