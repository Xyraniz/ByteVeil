//
// Created by xgladius on 8/6/22.
//
#pragma once
#include <memory>
#include "../luau/VM/src/lvm.h"
#include "../luau/VM/src/lstate.h"
#include "../luau/VM/src/ltable.h"
#include "../luau/VM/src/lfunc.h"
#include "../luau/VM/src/lstring.h"
#include "../luau/VM/src/lgc.h"
#include "../luau/VM/src/lmem.h"
#include "../luau/VM/src/lbytecode.h"
#include "../luau/VM/src/lapi.h"
#include "../luau/VM/src/lobject.h"
#include "../luau/Common/include/Luau/Bytecode.h"
#include "../luau/Bytecode/include/Luau/BytecodeBuilder.h"
#include "Luau/Ast.h"

// Luau removed these opcodes when FORGLOOP gained AUX mode bits and
// constant comparisons moved to JUMPXEQK*. Keep out-of-range sentinels so
// the legacy lifter can still describe old in-memory cases without matching
// any opcode in current serialized chunks.
namespace Luau {
constexpr LuauOpcode LOP_FORGLOOP_INEXT = static_cast<LuauOpcode>(LOP__COUNT + 1);
constexpr LuauOpcode LOP_FORGLOOP_NEXT = static_cast<LuauOpcode>(LOP__COUNT + 2);
constexpr LuauOpcode LOP_JUMPIFEQK = static_cast<LuauOpcode>(LOP__COUNT + 3);
constexpr LuauOpcode LOP_JUMPIFNOTEQK = static_cast<LuauOpcode>(LOP__COUNT + 4);
}
