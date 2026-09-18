#include "IR.h"
#include "Restructure.h"
#include <algorithm>
#include <functional>
#include <iomanip>
#include <iterator>
#include <limits>
#include <set>
#include <sstream>

namespace Luau::Decompiler::IR {
namespace {

static int opLength(LuauOpcode op)
{
    switch (op)
    {
    case LOP_GETGLOBAL: case LOP_SETGLOBAL: case LOP_GETIMPORT: case LOP_GETTABLEKS:
    case LOP_SETTABLEKS: case LOP_NAMECALL: case LOP_JUMPIFEQ: case LOP_JUMPIFLE:
    case LOP_JUMPIFLT: case LOP_JUMPIFNOTEQ: case LOP_JUMPIFNOTLE: case LOP_JUMPIFNOTLT:
    case LOP_NEWTABLE: case LOP_SETLIST: case LOP_FORGLOOP: case LOP_LOADKX:
    case LOP_JUMPIFEQK: case LOP_JUMPIFNOTEQK: case LOP_FASTCALL2: case LOP_FASTCALL2K:
        return 2;
    default: return 1;
    }
}

static bool hasAux(LuauOpcode op) { return opLength(op) == 2; }

static bool isJump(LuauOpcode op)
{
    switch (op)
    {
    case LOP_JUMP: case LOP_JUMPBACK: case LOP_JUMPIF: case LOP_JUMPIFNOT:
    case LOP_JUMPIFEQ: case LOP_JUMPIFLE: case LOP_JUMPIFLT: case LOP_JUMPIFNOTEQ:
    case LOP_JUMPIFNOTLE: case LOP_JUMPIFNOTLT: case LOP_FORNPREP: case LOP_FORNLOOP:
    case LOP_FORGLOOP: case LOP_FORGPREP_INEXT: case LOP_FORGLOOP_INEXT:
    case LOP_FORGPREP_NEXT: case LOP_FORGLOOP_NEXT: case LOP_JUMPX:
    case LOP_JUMPIFEQK: case LOP_JUMPIFNOTEQK: case LOP_FORGPREP:
    case LOP_LOADB: case LOP_FASTCALL: case LOP_FASTCALL1: case LOP_FASTCALL2: case LOP_FASTCALL2K:
        return true;
    default: return false;
    }
}

static bool isConditional(LuauOpcode op)
{
    switch (op)
    {
    case LOP_JUMPIF: case LOP_JUMPIFNOT: case LOP_JUMPIFEQ:
    case LOP_JUMPIFLE: case LOP_JUMPIFLT: case LOP_JUMPIFNOTEQ: case LOP_JUMPIFNOTLE:
    case LOP_JUMPIFNOTLT: case LOP_JUMPIFEQK: case LOP_JUMPIFNOTEQK: case LOP_FORNPREP:
    case LOP_FORNLOOP: case LOP_FORGLOOP: case LOP_FORGLOOP_INEXT: case LOP_FORGLOOP_NEXT:
    case LOP_FASTCALL: case LOP_FASTCALL1: case LOP_FASTCALL2: case LOP_FASTCALL2K:
        return true;
    default: return false;
    }
}

static bool isReturn(LuauOpcode op) { return op == LOP_RETURN; }

static bool hasRegisterA(LuauOpcode op)
{
    switch (op)
    {
    case LOP_NOP: case LOP_BREAK: case LOP_JUMP: case LOP_JUMPBACK: case LOP_JUMPX:
    case LOP_FASTCALL: case LOP_FASTCALL1: case LOP_FASTCALL2: case LOP_FASTCALL2K:
    case LOP_COVERAGE: case LOP_CAPTURE: case LOP_PREPVARARGS:
        return false;
    default:
        return true;
    }
}

static int jumpTarget(const uint32_t raw, int pc)
{
    LuauOpcode op = LuauOpcode(LUAU_INSN_OP(raw));
    if (op == LOP_JUMPX) return pc + LUAU_INSN_E(raw) + 1;
    if (isJump(op)) return pc + LUAU_INSN_D(raw) + 1;
    if ((op == LOP_LOADB || op == LOP_FASTCALL || op == LOP_FASTCALL1 || op == LOP_FASTCALL2 || op == LOP_FASTCALL2K) && LUAU_INSN_C(raw))
        return pc + LUAU_INSN_C(raw) + 1;
    return -1;
}

static std::string esc(const std::string& s)
{
    std::string r;
    for (char c : s)
    {
        if (c == '\\' || c == '"') r += '\\';
        if (c == '\n') r += "\\n"; else if (c == '\r') r += "\\r"; else r += c;
    }
    return r;
}

static std::string tag(LuauOpcode op)
{
    if (isJump(op)) return "control-flow";
    switch (op)
    {
    case LOP_LOADNIL: case LOP_LOADN: case LOP_LOADK: case LOP_LOADKX: case LOP_MOVE:
    case LOP_GETGLOBAL: case LOP_GETUPVAL: case LOP_GETIMPORT:
        return "load";
    case LOP_ADD: case LOP_SUB: case LOP_MUL: case LOP_DIV: case LOP_MOD: case LOP_POW:
    case LOP_ADDK: case LOP_SUBK: case LOP_MULK: case LOP_DIVK: case LOP_MODK: case LOP_POWK:
    case LOP_AND: case LOP_OR: case LOP_ANDK: case LOP_ORK: case LOP_CONCAT:
        return "binary";
    case LOP_NOT: case LOP_MINUS: case LOP_LENGTH:
        return "unary";
    case LOP_GETTABLE: case LOP_GETTABLEKS: case LOP_GETTABLEN:
        return "table-read";
    case LOP_SETTABLE: case LOP_SETTABLEKS: case LOP_SETTABLEN: case LOP_NEWTABLE:
    case LOP_DUPTABLE: case LOP_SETLIST:
        return "table-write";
    case LOP_SETGLOBAL: case LOP_SETUPVAL: case LOP_CLOSEUPVALS:
        return "store";
    case LOP_CALL: case LOP_NAMECALL:
        return "call";
    case LOP_NEWCLOSURE: case LOP_DUPCLOSURE: case LOP_CAPTURE:
        return "closure";
    case LOP_RETURN: return "return";
    case LOP_GETVARARGS: case LOP_PREPVARARGS: return "vararg";
    case LOP_NOP: case LOP_BREAK: case LOP_COVERAGE: return "metadata";
    default: return "unknown";
    }
}

static void annotateInstruction(Instruction& i, int registerCount)
{
    LuauOpcode op = LuauOpcode(i.opcode);
    i.isAuxiliary = i.hasAux;
    if (op == LOP_LOADK || op == LOP_DUPCLOSURE || op == LOP_DUPTABLE || op == LOP_GETIMPORT) i.constantIndex = i.d;
    else if (op == LOP_GETGLOBAL || op == LOP_SETGLOBAL || op == LOP_GETTABLEKS || op == LOP_SETTABLEKS ||
             op == LOP_NAMECALL || op == LOP_LOADKX || op == LOP_JUMPIFEQK || op == LOP_JUMPIFNOTEQK ||
             op == LOP_FASTCALL2K) i.constantIndex = int(i.aux);
    else if (op == LOP_ADDK || op == LOP_SUBK || op == LOP_MULK || op == LOP_DIVK ||
             op == LOP_MODK || op == LOP_POWK || op == LOP_ANDK || op == LOP_ORK) i.constantIndex = i.c;
    else i.constantIndex = -1;

    auto addUse = [&](int r) {
        if (r >= 0 && r < registerCount && std::find(i.uses.begin(), i.uses.end(), r) == i.uses.end())
            i.uses.push_back(r);
    };
    auto addDef = [&](int r) {
        if (r >= 0 && r < registerCount && std::find(i.definitions.begin(), i.definitions.end(), r) == i.definitions.end())
            i.definitions.push_back(r);
    };
    auto addRange = [&](auto&& add, int first, int count) {
        for (int n = 0; n < count && first + n < registerCount; ++n) add(first + n);
    };

    switch (op)
    {
    case LOP_MOVE: addUse(i.b); addDef(i.a); break;
    case LOP_GETTABLE: addUse(i.b); addUse(i.c); addDef(i.a); break;
    case LOP_SETTABLE: addUse(i.a); addUse(i.b); addUse(i.c); break;
    case LOP_ADD: case LOP_SUB: case LOP_MUL: case LOP_DIV: case LOP_MOD: case LOP_POW:
    case LOP_AND: case LOP_OR:
        addUse(i.b); addUse(i.c); addDef(i.a); break;
    case LOP_CONCAT:
        if (i.b <= i.c) addRange(addUse, i.b, i.c - i.b + 1);
        addDef(i.a); break;
    case LOP_GETTABLEKS: case LOP_GETTABLEN:
        addUse(i.b); addDef(i.a); break;
    case LOP_SETTABLEKS: case LOP_SETTABLEN:
        addUse(i.a); addUse(i.b); break;
    case LOP_NOT: case LOP_MINUS: case LOP_LENGTH: addUse(i.b); addDef(i.a); break;
    case LOP_ADDK: case LOP_SUBK: case LOP_MULK: case LOP_DIVK: case LOP_MODK: case LOP_POWK:
    case LOP_ANDK: case LOP_ORK: addUse(i.b); addDef(i.a); break;
    case LOP_NAMECALL:
        addUse(i.b); addDef(i.a); addDef(i.a + 1); break;
    case LOP_CALL:
        addRange(addUse, i.a, i.b == 0 ? registerCount - i.a : i.b);
        if (i.c == 0) addRange(addDef, i.a, registerCount - i.a);
        else if (i.c > 1) addRange(addDef, i.a, i.c - 1);
        break;
    case LOP_RETURN:
        addRange(addUse, i.a, i.b == 0 ? registerCount - i.a : std::max(0, i.b - 1));
        break;
    case LOP_JUMPIF: case LOP_JUMPIFNOT: case LOP_JUMPIFEQK: case LOP_JUMPIFNOTEQK:
        addUse(i.a); break;
    case LOP_JUMPIFEQ: case LOP_JUMPIFLE: case LOP_JUMPIFLT: case LOP_JUMPIFNOTEQ:
    case LOP_JUMPIFNOTLE: case LOP_JUMPIFNOTLT:
        addUse(i.a); addUse(int(i.aux)); break;
    case LOP_FORNPREP:
        addRange(addUse, i.a, 3); addRange(addDef, i.a, 3); break;
    case LOP_FORNLOOP:
        addRange(addUse, i.a, 3); addDef(i.a + 2); break;
    case LOP_FORGPREP:
        addRange(addUse, i.a, 3); addRange(addDef, i.a, 3); break;
    case LOP_FORGPREP_INEXT: case LOP_FORGPREP_NEXT:
        addRange(addUse, i.a, 3); addDef(i.a + 2); break;
    case LOP_FORGLOOP: {
        addRange(addUse, i.a, 3);
        int variables = std::max(1, int(i.aux & 0xff));
        addDef(i.a + 2); addRange(addDef, i.a + 3, variables);
        break;
    }
    case LOP_FORGLOOP_INEXT: case LOP_FORGLOOP_NEXT:
        addRange(addUse, i.a, 3); addDef(i.a + 2); addRange(addDef, i.a + 3, 2); break;
    case LOP_SETGLOBAL: case LOP_SETUPVAL: addUse(i.a); break;
    case LOP_CLOSEUPVALS: addRange(addUse, i.a, registerCount - i.a); break;
    case LOP_SETLIST:
        addUse(i.a); addRange(addUse, i.b, i.c == 0 ? registerCount - i.b : std::max(0, i.c - 1)); break;
    case LOP_CAPTURE:
        if (i.a == LCT_VAL || i.a == LCT_REF) addUse(i.b);
        break;
    case LOP_FASTCALL1: case LOP_FASTCALL2K: addUse(i.b); break;
    case LOP_FASTCALL2: addUse(i.b); addUse(int(i.aux & 0xff)); break;
    case LOP_GETVARARGS:
        addRange(addDef, i.a, i.b == 0 ? registerCount - i.a : std::max(0, i.b - 1)); break;
    case LOP_GETGLOBAL: case LOP_GETIMPORT: case LOP_GETUPVAL: case LOP_LOADNIL: case LOP_LOADB:
    case LOP_LOADN: case LOP_LOADK: case LOP_LOADKX: case LOP_NEWTABLE: case LOP_DUPTABLE:
    case LOP_NEWCLOSURE: case LOP_DUPCLOSURE:
        addDef(i.a);
        break;
    case LOP_NOP: case LOP_BREAK: case LOP_JUMP: case LOP_JUMPBACK: case LOP_JUMPX:
    case LOP_FASTCALL: case LOP_COVERAGE: case LOP_PREPVARARGS:
        break;
    default:
        i.hasSideEffects = true;
        break;
    }
    if (!i.definitions.empty()) i.destinationRegister = i.definitions.front();
    if (op == LOP_CALL || op == LOP_NAMECALL || op == LOP_RETURN || op == LOP_SETGLOBAL || op == LOP_SETUPVAL ||
        op == LOP_SETTABLE || op == LOP_SETTABLEKS || op == LOP_SETTABLEN || op == LOP_SETLIST ||
        op == LOP_CLOSEUPVALS || isJump(op))
        i.hasSideEffects = true;
    i.isPure = !i.hasSideEffects && op != LOP_GETTABLE && op != LOP_GETTABLEKS;
}

static bool validateOne(const Proto* p, std::string& error, int depth, int& total, int maxDepth, int maxInstructions, int functionId)
{
    if (!p) { error = "function " + std::to_string(functionId) + ": null prototype"; return false; }
    if (depth > maxDepth) { error = "function " + std::to_string(functionId) + ": prototype nesting exceeds limit"; return false; }
    if (p->sizecode < 0 || p->sizecode > maxInstructions || p->sizep < 0 || p->sizep > 100000 || p->sizek < 0 || p->sizek > 1000000)
    { error = "function " + std::to_string(functionId) + ": prototype size exceeds safe limits"; return false; }
    if (p->maxstacksize == 0 || p->maxstacksize > 255 || p->numparams > p->maxstacksize || p->nups > 255)
    { error = "function " + std::to_string(functionId) + ": invalid register or parameter metadata"; return false; }
    if (!p->code && p->sizecode) { error = "function " + std::to_string(functionId) + " offset 0: missing instruction storage"; return false; }
    if (!p->p && p->sizep) { error = "function " + std::to_string(functionId) + ": missing child prototype storage"; return false; }
    if (!p->k && p->sizek) { error = "function " + std::to_string(functionId) + ": missing constant storage"; return false; }

    // Build instruction boundaries while decoding. A jump into an AUX word is
    // not a valid CFG edge; accepting it would desynchronise later passes.
    std::set<int> instructionBoundaries;
    std::vector<std::pair<int, int>> jumpTargets;
    std::vector<std::pair<int, int>> decoded;
    for (int pc = 0; pc < p->sizecode;)
    {
        instructionBoundaries.insert(pc);
        ++total;
        if (total > maxInstructions) { error = "instruction limit exceeded"; return false; }
        uint32_t raw = p->code[pc];
        int op = int(LUAU_INSN_OP(raw));
        if (op < 0 || op >= LOP__COUNT) { error = "function " + std::to_string(functionId) + " offset " + std::to_string(pc) + ": unknown opcode " + std::to_string(op); return false; }
        int len = opLength(LuauOpcode(op));
        if (pc + len > p->sizecode) { error = "function " + std::to_string(functionId) + " offset " + std::to_string(pc) + ": missing AUX instruction"; return false; }
        if (hasRegisterA(LuauOpcode(op)) && LUAU_INSN_A(raw) >= p->maxstacksize)
        { error = "function " + std::to_string(functionId) + " offset " + std::to_string(pc) + ": register A out of range"; return false; }
        bool directConstant = op == LOP_LOADK || op == LOP_DUPCLOSURE || op == LOP_DUPTABLE || op == LOP_GETIMPORT;
        bool cConstant = op == LOP_ADDK || op == LOP_SUBK || op == LOP_MULK || op == LOP_DIVK ||
                         op == LOP_MODK || op == LOP_POWK || op == LOP_ANDK || op == LOP_ORK;
        if ((directConstant && LUAU_INSN_D(raw) >= p->sizek) || (cConstant && LUAU_INSN_C(raw) >= p->sizek))
        { error = "function " + std::to_string(functionId) + " offset " + std::to_string(pc) + ": constant index out of range"; return false; }
        bool auxConstant = op == LOP_GETGLOBAL || op == LOP_SETGLOBAL || op == LOP_GETTABLEKS ||
                           op == LOP_SETTABLEKS || op == LOP_NAMECALL || op == LOP_LOADKX ||
                           op == LOP_JUMPIFEQK || op == LOP_JUMPIFNOTEQK || op == LOP_FASTCALL2K;
        if (auxConstant && p->code[pc + 1] >= uint32_t(p->sizek))
        { error = "function " + std::to_string(functionId) + " offset " + std::to_string(pc) + ": auxiliary constant index out of range"; return false; }
        if ((op == LOP_NEWCLOSURE) && LUAU_INSN_D(raw) >= p->sizep)
        { error = "function " + std::to_string(functionId) + " offset " + std::to_string(pc) + ": prototype index out of range"; return false; }
        int target = jumpTarget(raw, pc);
        bool optionalSkip = (op == LOP_LOADB || op == LOP_FASTCALL || op == LOP_FASTCALL1 ||
                             op == LOP_FASTCALL2 || op == LOP_FASTCALL2K) && LUAU_INSN_C(raw) == 0;
        if (isJump(LuauOpcode(op)) && !optionalSkip) {
            if (target < 0 || target >= p->sizecode) { error = "function " + std::to_string(functionId) + " offset " + std::to_string(pc) + ": jump target out of range"; return false; }
            jumpTargets.emplace_back(pc, target);
        }
        if (op == LOP_CAPTURE && LUAU_INSN_B(raw) >= p->maxstacksize && LUAU_INSN_A(raw) != LCT_UPVAL)
        { error = "function " + std::to_string(functionId) + " offset " + std::to_string(pc) + ": capture register out of range"; return false; }
        auto checkReg = [&](unsigned int reg, const char* field) {
            if (reg >= p->maxstacksize) {
                error = "function " + std::to_string(functionId) + " offset " + std::to_string(pc) + ": register " + field + " out of range";
                return false;
            }
            return true;
        };
        switch (LuauOpcode(op))
        {
        case LOP_MOVE: case LOP_GETTABLE: case LOP_SETTABLE: case LOP_ADD:
        case LOP_SUB: case LOP_MUL: case LOP_DIV: case LOP_MOD: case LOP_POW:
        case LOP_AND: case LOP_OR: case LOP_CONCAT:
            if (!checkReg(LUAU_INSN_B(raw), "B") || !checkReg(LUAU_INSN_C(raw), "C")) return false;
            break;
        case LOP_GETTABLEKS: case LOP_SETTABLEKS: case LOP_GETTABLEN: case LOP_SETTABLEN:
        case LOP_NOT: case LOP_MINUS: case LOP_LENGTH:
            if (!checkReg(LUAU_INSN_B(raw), "B")) return false;
            break;
        case LOP_JUMPIFEQ: case LOP_JUMPIFLE: case LOP_JUMPIFLT: case LOP_JUMPIFNOTEQ:
        case LOP_JUMPIFNOTLE: case LOP_JUMPIFNOTLT:
            if (!checkReg(p->code[pc + 1], "AUX")) return false;
            break;
        case LOP_FASTCALL1: case LOP_FASTCALL2: case LOP_FASTCALL2K:
            if (!checkReg(LUAU_INSN_B(raw), "B")) return false;
            if (op == LOP_FASTCALL2 && !checkReg(p->code[pc + 1] & 0xff, "AUX")) return false;
            break;
        case LOP_NAMECALL:
            if (!checkReg(LUAU_INSN_B(raw), "B") || LUAU_INSN_A(raw) + 1 >= p->maxstacksize)
            { error = "function " + std::to_string(functionId) + " offset " + std::to_string(pc) + ": NAMECALL result range out of range"; return false; }
            break;
        case LOP_FORNPREP: case LOP_FORNLOOP:
            if (LUAU_INSN_A(raw) + 2 >= p->maxstacksize)
            { error = "function " + std::to_string(functionId) + " offset " + std::to_string(pc) + ": numeric-for register range out of range"; return false; }
            break;
        case LOP_FORGPREP: case LOP_FORGPREP_INEXT: case LOP_FORGPREP_NEXT:
            if (LUAU_INSN_A(raw) + 2 >= p->maxstacksize)
            { error = "function " + std::to_string(functionId) + " offset " + std::to_string(pc) + ": generic-for register range out of range"; return false; }
            break;
        case LOP_FORGLOOP: {
            unsigned int variables = p->code[pc + 1];
            if (variables == 0 || variables > 255 || LUAU_INSN_A(raw) + 2 + variables >= p->maxstacksize)
            { error = "function " + std::to_string(functionId) + " offset " + std::to_string(pc) + ": generic-for result range out of range"; return false; }
            break;
        }
        case LOP_FORGLOOP_INEXT: case LOP_FORGLOOP_NEXT:
            if (LUAU_INSN_A(raw) + 4 >= p->maxstacksize)
            { error = "function " + std::to_string(functionId) + " offset " + std::to_string(pc) + ": specialized generic-for result range out of range"; return false; }
            break;
        default:
            break;
        }
        decoded.emplace_back(pc, len);
        pc += len;
    }
    for (const auto& edge : jumpTargets)
    {
        if (!instructionBoundaries.count(edge.second))
        {
            error = "function " + std::to_string(functionId) + " offset " + std::to_string(edge.first) + ": jump target is not an instruction boundary";
            return false;
        }
    }
    for (int i = 0; i < p->sizep; ++i)
        if (!validateOne(p->p[i], error, depth + 1, total, maxDepth, maxInstructions, functionId + i + 1)) return false;
    return true;
}

static void addFunction(const Proto* p, Function& f, int& nextId, int parentId, int protoIndex)
{
    const int id = nextId++;
    f.id = id; f.parentId = parentId; f.prototypeIndex = protoIndex;
    f.parameters = p->numparams; f.registers = p->maxstacksize; f.constants = p->sizek; f.upvalues = p->nups; f.lineDefined = p->linedefined;
    f.nameHint = (p->debugname && p->debugname->data && p->debugname->data[0]) ? p->debugname->data : "function_" + std::to_string(id);
    for (int pc = 0; pc < p->sizecode;)
    {
        uint32_t raw = p->code[pc]; LuauOpcode op = LuauOpcode(LUAU_INSN_OP(raw));
        Instruction i; i.offset = pc; i.opcode = int(op); i.length = opLength(op); i.a = LUAU_INSN_A(raw); i.b = LUAU_INSN_B(raw); i.c = LUAU_INSN_C(raw); i.d = LUAU_INSN_D(raw); i.e = LUAU_INSN_E(raw); i.hasAux = hasAux(op); i.aux = i.hasAux ? p->code[pc + 1] : 0; i.jumpTarget = jumpTarget(raw, pc); i.semanticTag = tag(op);
        annotateInstruction(i, f.registers);
        if (p->lineinfo && pc < p->sizecode) i.line = luaG_getline(const_cast<Proto*>(p), pc);
        f.instructions.push_back(i); pc += i.length;
    }
    f.registerFirstUse.assign(f.registers, -1);
    f.registerLastUse.assign(f.registers, -1);
    f.registerDefinitionCount.assign(f.registers, 0);
    f.registerUseCount.assign(f.registers, 0);
    for (size_t index = 0; index < f.instructions.size(); ++index)
    {
        Instruction& instruction = f.instructions[index];
        if (instruction.semanticTag == "unknown") ++f.unknownInstructionCount;
        for (int reg : instruction.definitions)
            if (reg >= 0 && reg < f.registers) ++f.registerDefinitionCount[reg];
        for (int reg : instruction.uses)
        {
            if (reg < 0 || reg >= f.registers) continue;
            ++f.registerUseCount[reg];
            if (f.registerFirstUse[reg] < 0) f.registerFirstUse[reg] = int(index);
            f.registerLastUse[reg] = int(index);
        }
    }
    std::set<int> starts{0};
    for (const Instruction& i : f.instructions) if (i.jumpTarget >= 0) starts.insert(i.jumpTarget);
    for (const Instruction& i : f.instructions)
        if ((i.jumpTarget >= 0 || isReturn(LuauOpcode(i.opcode))) && i.offset + i.length < p->sizecode)
            starts.insert(i.offset + i.length);
    std::vector<int> sorted(starts.begin(), starts.end());
    std::sort(sorted.begin(), sorted.end());
    for (size_t n = 0; n < sorted.size(); ++n)
    {
        BasicBlock block; block.id = int(n); block.start = sorted[n]; block.end = (n + 1 < sorted.size() ? sorted[n + 1] : p->sizecode) - 1;
        for (size_t k = 0; k < f.instructions.size(); ++k) if (f.instructions[k].offset >= block.start && f.instructions[k].offset <= block.end) block.instructions.push_back(int(k));
        if (!block.instructions.empty())
        {
            const Instruction& last = f.instructions[block.instructions.back()];
            if (last.jumpTarget >= 0) for (size_t j = 0; j < sorted.size(); ++j) if (sorted[j] == last.jumpTarget) block.successors.push_back(int(j));
            bool conditional = isConditional(LuauOpcode(last.opcode));
            if (conditional && n + 1 < sorted.size()) block.successors.push_back(int(n + 1));
            else if (last.jumpTarget < 0 && !isReturn(LuauOpcode(last.opcode)) && n + 1 < sorted.size()) block.successors.push_back(int(n + 1));
            std::sort(block.successors.begin(), block.successors.end()); block.successors.erase(std::unique(block.successors.begin(), block.successors.end()), block.successors.end());
        }
        f.basicBlocks.push_back(std::move(block));
    }
    for (BasicBlock& block : f.basicBlocks)
        for (int successor : block.successors)
            if (successor >= 0 && successor < int(f.basicBlocks.size())) f.basicBlocks[successor].predecessors.push_back(block.id);
    for (BasicBlock& block : f.basicBlocks)
        for (int instructionIndex : block.instructions)
        {
            Instruction& instruction = f.instructions[instructionIndex];
            instruction.sourceBlock = block.id;
            if (instruction.jumpTarget >= 0)
                for (const BasicBlock& target : f.basicBlocks)
                    if (target.start == instruction.jumpTarget) { instruction.targetBlock = target.id; break; }
        }
    // Medal's SSA destruction pass computes register liveness with a backward
    // fixed point. Preserve that useful information in the neutral C++ IR.
    const int blockCount = int(f.basicBlocks.size());
    f.liveIn.assign(blockCount, {});
    f.liveOut.assign(blockCount, {});
    f.blockLiveRegisterCount.assign(blockCount, 0);
    std::vector<std::set<int>> blockUses(blockCount), blockDefs(blockCount);
    for (const BasicBlock& block : f.basicBlocks)
    {
        for (int instructionIndex : block.instructions)
        {
            const Instruction& instruction = f.instructions[instructionIndex];
            for (int reg : instruction.uses)
                if (reg >= 0 && reg < f.registers && !blockDefs[block.id].count(reg)) blockUses[block.id].insert(reg);
            for (int reg : instruction.definitions)
                if (reg >= 0 && reg < f.registers) blockDefs[block.id].insert(reg);
        }
    }
    bool livenessChanged = true;
    while (livenessChanged)
    {
        livenessChanged = false;
        for (int block = blockCount - 1; block >= 0; --block)
        {
            std::set<int> nextOut;
            for (int successor : f.basicBlocks[block].successors)
                if (successor >= 0 && successor < blockCount) nextOut.insert(f.liveIn[successor].begin(), f.liveIn[successor].end());
            std::set<int> nextIn = blockUses[block];
            for (int reg : nextOut) if (!blockDefs[block].count(reg)) nextIn.insert(reg);
            if (nextOut != std::set<int>(f.liveOut[block].begin(), f.liveOut[block].end()) ||
                nextIn != std::set<int>(f.liveIn[block].begin(), f.liveIn[block].end()))
            {
                f.liveOut[block] = std::vector<int>(nextOut.begin(), nextOut.end());
                f.liveIn[block] = std::vector<int>(nextIn.begin(), nextIn.end());
                livenessChanged = true;
            }
        }
    }
    for (int block = 0; block < blockCount; ++block)
    {
        std::set<int> live(f.liveOut[block].begin(), f.liveOut[block].end());
        int peak = int(live.size());
        for (auto it = f.basicBlocks[block].instructions.rbegin(); it != f.basicBlocks[block].instructions.rend(); ++it)
        {
            const Instruction& instruction = f.instructions[*it];
            for (int reg : instruction.definitions) if (reg >= 0 && reg < f.registers) live.erase(reg);
            for (int reg : instruction.uses) if (reg >= 0 && reg < f.registers) live.insert(reg);
            peak = std::max(peak, int(live.size()));
        }
        f.blockLiveRegisterCount[block] = peak;
    }
    // Medal's restructurer relies on dominators and natural-loop headers.  Keep
    // the same useful CFG facts in the neutral ByteVeil IR so future AST passes
    // do not need to rediscover them from serialized instructions.
    if (blockCount > 0)
    {
        std::vector<std::set<int>> predecessors(blockCount);
        for (const BasicBlock& block : f.basicBlocks)
            for (int successor : block.successors)
                if (successor >= 0 && successor < blockCount) predecessors[successor].insert(block.id);

        std::vector<char> reachable(blockCount, 0);
        std::function<void(int)> markReachable = [&](int block) {
            if (block < 0 || block >= blockCount || reachable[block]) return;
            reachable[block] = 1;
            for (int successor : f.basicBlocks[block].successors) markReachable(successor);
        };
        markReachable(0);
        for (int block = 0; block < blockCount; ++block) f.basicBlocks[block].reachable = reachable[block] != 0;

        std::vector<std::set<int>> dominators(blockCount);
        for (int block = 0; block < blockCount; ++block)
            if (reachable[block]) dominators[block] = {};
        dominators[0] = {0};
        std::set<int> allReachable;
        for (int block = 0; block < blockCount; ++block) if (reachable[block]) allReachable.insert(block);
        for (int block = 1; block < blockCount; ++block) if (reachable[block]) dominators[block] = allReachable;

        bool changed = true;
        while (changed)
        {
            changed = false;
            for (int block = 1; block < blockCount; ++block)
            {
                if (!reachable[block]) continue;
                std::set<int> next = allReachable;
                bool hasReachablePredecessor = false;
                for (int predecessor : predecessors[block])
                {
                    if (!reachable[predecessor]) continue;
                    hasReachablePredecessor = true;
                    std::set<int> intersection;
                    std::set_intersection(next.begin(), next.end(), dominators[predecessor].begin(), dominators[predecessor].end(), std::inserter(intersection, intersection.begin()));
                    next = std::move(intersection);
                }
                if (hasReachablePredecessor) next.insert(block);
                if (next != dominators[block]) { dominators[block] = std::move(next); changed = true; }
            }
        }

        f.immediateDominators.assign(blockCount, -1);
        f.immediateDominators[0] = 0;
        for (int block = 1; block < blockCount; ++block)
        {
            if (!reachable[block]) continue;
            int best = -1;
            size_t bestDepth = 0;
            for (int candidate : dominators[block])
            {
                if (candidate == block) continue;
                if (dominators[candidate].size() > bestDepth) { best = candidate; bestDepth = dominators[candidate].size(); }
            }
            f.immediateDominators[block] = best;
        }

        for (const BasicBlock& block : f.basicBlocks)
            for (int successor : block.successors)
                if (successor >= 0 && successor < blockCount && reachable[successor] && dominators[block.id].count(successor))
                {
                    f.backEdges.emplace_back(block.id, successor);
                    std::set<int> loop{successor, block.id};
                    std::vector<int> work{block.id};
                    while (!work.empty())
                    {
                        int current = work.back(); work.pop_back();
                        for (int predecessor : predecessors[current])
                            if (reachable[predecessor] && loop.insert(predecessor).second) work.push_back(predecessor);
                    }
                    f.naturalLoops.emplace_back(loop.begin(), loop.end());
                }

        // Materialize the loop facts instead of leaving consumers to infer
        // them from opcode mutations or serialized edges.
        for (size_t n = 0; n < f.backEdges.size(); ++n)
        {
            const auto edge = f.backEdges[n];
            auto found = std::find_if(f.loops.begin(), f.loops.end(), [&](const Loop& loop) { return loop.header == edge.second; });
            if (found == f.loops.end()) { f.loops.push_back(Loop{}); found = std::prev(f.loops.end()); found->header = edge.second; }
            found->backEdges.push_back(edge);
            found->latches.push_back(edge.first);
            if (n < f.naturalLoops.size()) found->blocks.insert(found->blocks.end(), f.naturalLoops[n].begin(), f.naturalLoops[n].end());
        }
        for (Loop& loop : f.loops)
        {
            std::sort(loop.blocks.begin(), loop.blocks.end());
            loop.blocks.erase(std::unique(loop.blocks.begin(), loop.blocks.end()), loop.blocks.end());
            std::sort(loop.latches.begin(), loop.latches.end());
            loop.latches.erase(std::unique(loop.latches.begin(), loop.latches.end()), loop.latches.end());
            const std::set<int> members(loop.blocks.begin(), loop.blocks.end());
            std::vector<int> outsidePredecessors;
            for (int predecessor : predecessors[loop.header])
                if (!members.count(predecessor)) outsidePredecessors.push_back(predecessor);
            loop.preheader = outsidePredecessors.size() == 1 ? outsidePredecessors.front() : -1;
            std::set<int> exits;
            for (int member : loop.blocks)
                for (int successor : f.basicBlocks[member].successors)
                    if (!members.count(successor)) exits.insert(successor);
            loop.exits.assign(exits.begin(), exits.end());
        }

        // Iterative post-dominators over the finite CFG. Exit blocks
        // post-dominate themselves; unreachable blocks remain empty.
        f.postDominators.assign(blockCount, {});
        for (int block = 0; block < blockCount; ++block)
            if (reachable[block]) f.postDominators[block] = f.basicBlocks[block].successors.empty() ? std::set<int>{block} : allReachable;
        bool postChanged = true;
        while (postChanged)
        {
            postChanged = false;
            for (int block = blockCount - 1; block >= 0; --block)
            {
                if (!reachable[block] || f.basicBlocks[block].successors.empty()) continue;
                std::set<int> next = allReachable;
                for (int successor : f.basicBlocks[block].successors)
                {
                    std::set<int> intersection;
                    std::set_intersection(next.begin(), next.end(), f.postDominators[successor].begin(), f.postDominators[successor].end(), std::inserter(intersection, intersection.begin()));
                    next = std::move(intersection);
                }
                next.insert(block);
                if (next != f.postDominators[block]) { f.postDominators[block] = std::move(next); postChanged = true; }
            }
        }
        f.immediatePostDominators.assign(blockCount, -1);
        for (int block = 0; block < blockCount; ++block)
        {
            if (!reachable[block]) continue;
            int best = -1;
            size_t bestSize = std::numeric_limits<size_t>::max();
            for (int candidate : f.postDominators[block])
                if (candidate != block && f.postDominators[candidate].size() < bestSize)
                { best = candidate; bestSize = f.postDominators[candidate].size(); }
            f.immediatePostDominators[block] = best;
        }

        // Tarjan SCCs identify irreducible cycles and nested loop components.
        std::vector<int> index(blockCount, -1), low(blockCount, -1), stack;
        std::vector<char> onStack(blockCount, 0); int nextIndex = 0;
        std::function<void(int)> strongConnect = [&](int v) {
            index[v] = low[v] = nextIndex++; stack.push_back(v); onStack[v] = 1;
            for (int w : f.basicBlocks[v].successors) if (reachable[w])
            {
                if (index[w] < 0) { strongConnect(w); low[v] = std::min(low[v], low[w]); }
                else if (onStack[w]) low[v] = std::min(low[v], index[w]);
            }
            if (low[v] == index[v])
            {
                std::vector<int> component;
                while (!stack.empty()) { int w = stack.back(); stack.pop_back(); onStack[w] = 0; component.push_back(w); if (w == v) break; }
                std::sort(component.begin(), component.end());
                f.stronglyConnectedComponents.push_back(std::move(component));
            }
        };
        for (int block = 0; block < blockCount; ++block) if (reachable[block] && index[block] < 0) strongConnect(block);
        Scope rootScope; rootScope.id = 0; rootScope.entryBlock = 0;
        std::vector<int> functionExits;
        for (const BasicBlock& block : f.basicBlocks) if (block.reachable && block.successors.empty()) functionExits.push_back(block.id);
        rootScope.exitBlock = functionExits.size() == 1 ? functionExits.front() : -1;
        for (int reg = 0; reg < f.registers; ++reg) rootScope.registers.push_back(reg);
        f.scopes.push_back(std::move(rootScope));
        for (size_t n = 0; n < f.loops.size(); ++n)
        {
            Scope scope; scope.id = int(n + 1); scope.parent = 0; scope.entryBlock = f.loops[n].header;
            scope.exitBlock = f.loops[n].exits.size() == 1 ? f.loops[n].exits.front() : -1;
            size_t parentSize = std::numeric_limits<size_t>::max();
            const std::set<int> members(f.loops[n].blocks.begin(), f.loops[n].blocks.end());
            for (size_t candidate = 0; candidate < f.loops.size(); ++candidate)
                if (candidate != n && f.loops[candidate].blocks.size() > members.size() && f.loops[candidate].blocks.size() < parentSize &&
                    std::all_of(members.begin(), members.end(), [&](int block) { return std::find(f.loops[candidate].blocks.begin(), f.loops[candidate].blocks.end(), block) != f.loops[candidate].blocks.end(); }))
                { scope.parent = int(candidate + 1); parentSize = f.loops[candidate].blocks.size(); }
            std::set<int> registers(f.liveIn[scope.entryBlock].begin(), f.liveIn[scope.entryBlock].end());
            for (int block : f.loops[n].blocks)
                for (int instructionIndex : f.basicBlocks[block].instructions)
                {
                    const Instruction& instruction = f.instructions[instructionIndex];
                    registers.insert(instruction.uses.begin(), instruction.uses.end());
                    registers.insert(instruction.definitions.begin(), instruction.definitions.end());
                }
            scope.registers.assign(registers.begin(), registers.end());
            f.scopes.push_back(std::move(scope));
        }
    }
    // Conservative register SSA: definitions are instruction-index versions;
    // joins receive deterministic phi versions when incoming definitions differ.
    f.instructionDefVersions.assign(f.instructions.size(), -1);
    if (blockCount > 0 && f.registers > 0)
    {
        for (size_t instructionIndex = 0; instructionIndex < f.instructions.size(); ++instructionIndex)
        {
            Instruction& instruction = f.instructions[instructionIndex];
            instruction.definitionVersions.clear();
            for (int reg : instruction.definitions)
                instruction.definitionVersions.push_back(1 + int(instructionIndex) * f.registers + reg);
            if (!instruction.definitionVersions.empty())
                f.instructionDefVersions[instructionIndex] = instruction.definitionVersions.front();
        }
        std::vector<std::set<int>> predecessors(blockCount);
        for (const BasicBlock& block : f.basicBlocks)
            for (int successor : block.successors)
                if (successor >= 0 && successor < blockCount) predecessors[successor].insert(block.id);
        std::vector<std::vector<int>> incoming(blockCount, std::vector<int>(f.registers, -1));
        std::vector<std::vector<int>> outgoing(blockCount, std::vector<int>(f.registers, -1));
        std::vector<std::vector<int>> phiVersions(blockCount, std::vector<int>(f.registers, -1));
        bool changed = true;
        while (changed)
        {
            changed = false;
            for (int block = 0; block < blockCount; ++block)
            {
                std::vector<int> next(f.registers, -1);
                if (block == 0) next.assign(f.registers, 0);
                else if (!predecessors[block].empty())
                {
                    for (int reg = 0; reg < f.registers; ++reg)
                    {
                        int value = -1; bool first = true; bool differs = false;
                        for (int predecessor : predecessors[block])
                        {
                            int candidate = outgoing[predecessor][reg];
                            if (first) { value = candidate; first = false; }
                            else if (candidate != value) differs = true;
                        }
                        if (differs)
                        {
                            int phiBase = 1 + (int(f.instructions.size()) + 1) * f.registers;
                            int phi = phiBase + block * f.registers + reg;
                            phiVersions[block][reg] = phi; next[reg] = phi;
                        }
                        else next[reg] = value;
                    }
                }
                if (next != incoming[block]) { incoming[block] = next; changed = true; }
                std::vector<int> out = next;
                for (int instructionIndex : f.basicBlocks[block].instructions)
                {
                    const Instruction& instruction = f.instructions[instructionIndex];
                    for (size_t definition = 0; definition < instruction.definitions.size(); ++definition)
                    {
                        int reg = instruction.definitions[definition];
                        if (reg >= 0 && reg < f.registers)
                            out[reg] = instruction.definitionVersions[definition];
                    }
                }
                if (out != outgoing[block]) { outgoing[block] = std::move(out); changed = true; }
            }
        }
        for (int block = 0; block < blockCount; ++block)
            for (int reg = 0; reg < f.registers; ++reg)
                if (phiVersions[block][reg] >= 0)
                {
                    PhiNode phi; phi.block = block; phi.reg = reg; phi.version = phiVersions[block][reg];
                    for (int predecessor : predecessors[block])
                    {
                        phi.incomingBlocks.push_back(predecessor);
                        phi.incomingVersions.push_back(outgoing[predecessor][reg]);
                    }
                    f.phiNodes.push_back(std::move(phi));
                }
    }
    for (int c = 0; c < p->sizep; ++c) { Function child; addFunction(p->p[c], child, nextId, id, c); f.children.push_back(std::move(child)); }
}

static void jsonFn(std::ostringstream& o, const Function& f)
{
    o << "{\"id\":" << f.id << ",\"parent_id\":" << f.parentId << ",\"prototype_index\":" << f.prototypeIndex << ",\"name_hint\":\"" << esc(f.nameHint) << "\",\"line_defined\":" << f.lineDefined << ",\"parameters\":" << f.parameters << ",\"registers\":" << f.registers << ",\"constants\":" << f.constants << ",\"upvalues\":" << f.upvalues << ",\"instructions\":[";
    for (size_t n = 0; n < f.instructions.size(); ++n)
    {
        if (n) o << ','; const auto& i = f.instructions[n];
        o << "{\"offset\":" << i.offset << ",\"opcode\":" << i.opcode << ",\"opcode_name\":\"" << opcodeName(i.opcode)
          << "\",\"length\":" << i.length << ",\"a\":" << i.a << ",\"b\":" << i.b << ",\"c\":" << i.c << ",\"d\":" << i.d << ",\"e\":" << i.e << ",\"aux\":" << i.aux
          << ",\"line\":" << i.line << ",\"jump_target\":" << i.jumpTarget << ",\"target_block\":" << i.targetBlock
          << ",\"destination_register\":" << i.destinationRegister << ",\"constant_index\":" << i.constantIndex
          << ",\"source_block\":" << i.sourceBlock << ",\"has_aux\":" << (i.hasAux ? "true" : "false")
          << ",\"is_auxiliary\":" << (i.isAuxiliary ? "true" : "false") << ",\"is_pure\":" << (i.isPure ? "true" : "false")
          << ",\"has_side_effects\":" << (i.hasSideEffects ? "true" : "false") << ",\"semantic_tag\":\"" << i.semanticTag << "\",\"uses\":[";
        for (size_t k = 0; k < i.uses.size(); ++k) { if (k) o << ','; o << i.uses[k]; }
        o << "],\"definitions\":[";
        for (size_t k = 0; k < i.definitions.size(); ++k) { if (k) o << ','; o << i.definitions[k]; }
        o << "],\"definition_versions\":[";
        for (size_t k = 0; k < i.definitionVersions.size(); ++k) { if (k) o << ','; o << i.definitionVersions[k]; }
        o << "]}";
    }
    o << "],\"basic_blocks\":[";
    for (size_t n = 0; n < f.basicBlocks.size(); ++n)
    {
        if (n) o << ','; const auto& b = f.basicBlocks[n];
        o << "{\"id\":" << b.id << ",\"start\":" << b.start << ",\"end\":" << b.end << ",\"instructions\":[";
        for (size_t k = 0; k < b.instructions.size(); ++k) { if (k) o << ','; o << b.instructions[k]; }
        o << "],\"successors\":["; for (size_t k = 0; k < b.successors.size(); ++k) { if (k) o << ','; o << b.successors[k]; }
        o << "],\"predecessors\":["; for (size_t k = 0; k < b.predecessors.size(); ++k) { if (k) o << ','; o << b.predecessors[k]; }
        o << "],\"live_in\":["; for (size_t k = 0; k < f.liveIn[b.id].size(); ++k) { if (k) o << ','; o << f.liveIn[b.id][k]; }
        o << "],\"live_out\":["; for (size_t k = 0; k < f.liveOut[b.id].size(); ++k) { if (k) o << ','; o << f.liveOut[b.id][k]; }
        o << "],\"live_register_count\":" << f.blockLiveRegisterCount[b.id] << ",\"reachable\":" << (b.reachable ? "true" : "false") << "}";
    }
    o << "],\"cfg_analysis\":{\"immediate_dominators\":[";
    for (size_t n = 0; n < f.immediateDominators.size(); ++n) { if (n) o << ','; o << f.immediateDominators[n]; }
    o << "],\"back_edges\":["; for (size_t n = 0; n < f.backEdges.size(); ++n) { if (n) o << ','; o << "[" << f.backEdges[n].first << "," << f.backEdges[n].second << "]"; }
    o << "],\"natural_loops\":["; for (size_t n = 0; n < f.naturalLoops.size(); ++n) { if (n) o << ','; o << '['; for (size_t k = 0; k < f.naturalLoops[n].size(); ++k) { if (k) o << ','; o << f.naturalLoops[n][k]; } o << ']'; }
    o << "],\"loop_regions\":["; for (size_t n = 0; n < f.loops.size(); ++n) { if (n) o << ','; const Loop& loop = f.loops[n];
    o << "{\"header\":" << loop.header << ",\"preheader\":" << loop.preheader << ",\"blocks\":[";
    for (size_t k = 0; k < loop.blocks.size(); ++k) { if (k) o << ','; o << loop.blocks[k]; }
    o << "],\"latches\":["; for (size_t k = 0; k < loop.latches.size(); ++k) { if (k) o << ','; o << loop.latches[k]; }
    o << "],\"exits\":["; for (size_t k = 0; k < loop.exits.size(); ++k) { if (k) o << ','; o << loop.exits[k]; } o << "]}"; }
    o << "],\"sccs\":["; for (size_t n = 0; n < f.stronglyConnectedComponents.size(); ++n) { if (n) o << ','; o << '['; for (size_t k = 0; k < f.stronglyConnectedComponents[n].size(); ++k) { if (k) o << ','; o << f.stronglyConnectedComponents[n][k]; } o << ']'; }
    o << "],\"immediate_post_dominators\":[";
    for (size_t n = 0; n < f.immediatePostDominators.size(); ++n) { if (n) o << ','; o << f.immediatePostDominators[n]; }
    o << "],\"liveness\":{\"block_live_register_count\":[";
    for (size_t n = 0; n < f.blockLiveRegisterCount.size(); ++n) { if (n) o << ','; o << f.blockLiveRegisterCount[n]; }
    o << "]}},\"ssa\":{\"instruction_def_versions\":[";
    for (size_t n = 0; n < f.instructionDefVersions.size(); ++n) { if (n) o << ','; o << f.instructionDefVersions[n]; }
    o << "],\"phi_nodes\":["; for (size_t n = 0; n < f.phiNodes.size(); ++n) { if (n) o << ','; const PhiNode& phi = f.phiNodes[n]; o << "{\"block\":" << phi.block << ",\"register\":" << phi.reg << ",\"version\":" << phi.version << ",\"incoming_blocks\":["; for (size_t k = 0; k < phi.incomingBlocks.size(); ++k) { if (k) o << ','; o << phi.incomingBlocks[k]; } o << "],\"incoming\":["; for (size_t k = 0; k < phi.incomingVersions.size(); ++k) { if (k) o << ','; o << phi.incomingVersions[k]; } o << "]}"; }
    o << "]},\"dataflow\":{\"register_first_use\":["; for (size_t n = 0; n < f.registerFirstUse.size(); ++n) { if (n) o << ','; o << f.registerFirstUse[n]; }
    o << "],\"register_last_use\":["; for (size_t n = 0; n < f.registerLastUse.size(); ++n) { if (n) o << ','; o << f.registerLastUse[n]; }
    o << "],\"definition_count\":["; for (size_t n = 0; n < f.registerDefinitionCount.size(); ++n) { if (n) o << ','; o << f.registerDefinitionCount[n]; }
    o << "],\"use_count\":["; for (size_t n = 0; n < f.registerUseCount.size(); ++n) { if (n) o << ','; o << f.registerUseCount[n]; }
    o << "],\"unknown_instructions\":" << f.unknownInstructionCount << "},\"scopes\":["; for (size_t n = 0; n < f.scopes.size(); ++n) { if (n) o << ','; const Scope& s = f.scopes[n]; o << "{\"id\":" << s.id << ",\"parent\":" << s.parent << ",\"entry\":" << s.entryBlock << ",\"exit\":" << s.exitBlock << ",\"registers\":["; for (size_t k = 0; k < s.registers.size(); ++k) { if (k) o << ','; o << s.registers[k]; } o << "]}"; }
    o << "],\"children\":["; for (size_t n = 0; n < f.children.size(); ++n) { if (n) o << ','; jsonFn(o, f.children[n]); } o << "]}";
}
}

std::string opcodeName(int opcode)
{
    static const char* names[] = {"NOP","BREAK","LOADNIL","LOADB","LOADN","LOADK","MOVE","GETGLOBAL","SETGLOBAL","GETUPVAL","SETUPVAL","CLOSEUPVALS","GETIMPORT","GETTABLE","SETTABLE","GETTABLEKS","SETTABLEKS","GETTABLEN","SETTABLEN","NEWCLOSURE","NAMECALL","CALL","RETURN","JUMP","JUMPBACK","JUMPIF","JUMPIFNOT","JUMPIFEQ","JUMPIFLE","JUMPIFLT","JUMPIFNOTEQ","JUMPIFNOTLE","JUMPIFNOTLT","ADD","SUB","MUL","DIV","MOD","POW","ADDK","SUBK","MULK","DIVK","MODK","POWK","AND","OR","ANDK","ORK","CONCAT","NOT","MINUS","LENGTH","NEWTABLE","DUPTABLE","SETLIST","FORNPREP","FORNLOOP","FORGLOOP","FORGPREP_INEXT","FORGLOOP_INEXT","FORGPREP_NEXT","FORGLOOP_NEXT","GETVARARGS","DUPCLOSURE","PREPVARARGS","LOADKX","JUMPX","FASTCALL","COVERAGE","CAPTURE","JUMPIFEQK","JUMPIFNOTEQK","FASTCALL1","FASTCALL2","FASTCALL2K","FORGPREP"};
    return opcode >= 0 && opcode < LOP__COUNT ? names[opcode] : "UNKNOWN";
}

bool validateProto(const Proto* root, std::string& error, int maxDepth, int maxInstructions)
{
    int total = 0; return validateOne(root, error, 0, total, maxDepth, maxInstructions, 0);
}

static bool validateFunctionAnalysis(const Function& f, std::string& error)
{
    const int blocks = int(f.basicBlocks.size());
    if (blocks == 0) { error = "IR function has no basic blocks"; return false; }
    if (int(f.immediateDominators.size()) != blocks) { error = "IR dominator vector does not match block count"; return false; }
    if (int(f.immediatePostDominators.size()) != blocks) { error = "IR post-dominator vector does not match block count"; return false; }
    if (int(f.liveIn.size()) != blocks || int(f.liveOut.size()) != blocks || int(f.blockLiveRegisterCount.size()) != blocks)
    { error = "IR liveness vectors do not match block count"; return false; }
    for (int block = 0; block < blocks; ++block)
    {
        for (int reg : f.liveIn[block])
            if (reg < 0 || reg >= f.registers) { error = "IR liveness contains an invalid live-in register"; return false; }
        for (int reg : f.liveOut[block])
            if (reg < 0 || reg >= f.registers) { error = "IR liveness contains an invalid live-out register"; return false; }
    }
    for (const BasicBlock& block : f.basicBlocks)
    {
        if (block.id < 0 || block.id >= blocks) { error = "IR contains an invalid basic-block id"; return false; }
        for (int target : block.successors)
        {
            if (target < 0 || target >= blocks) { error = "IR contains an out-of-range CFG successor"; return false; }
            if (std::find(f.basicBlocks[target].predecessors.begin(), f.basicBlocks[target].predecessors.end(), block.id) == f.basicBlocks[target].predecessors.end())
            { error = "IR CFG successor/predecessor mismatch"; return false; }
        }
        for (int predecessor : block.predecessors)
        {
            if (predecessor < 0 || predecessor >= blocks) { error = "IR contains an out-of-range CFG predecessor"; return false; }
            if (std::find(f.basicBlocks[predecessor].successors.begin(), f.basicBlocks[predecessor].successors.end(), block.id) == f.basicBlocks[predecessor].successors.end())
            { error = "IR CFG predecessor/successor mismatch"; return false; }
        }
        for (int instruction : block.instructions)
        {
            if (instruction < 0 || instruction >= int(f.instructions.size())) { error = "IR block references an invalid instruction"; return false; }
            if (f.instructions[instruction].sourceBlock != block.id) { error = "IR instruction has the wrong source block"; return false; }
        }
    }
    for (const Instruction& instruction : f.instructions)
    {
        if (instruction.definitions.size() != instruction.definitionVersions.size()) { error = "IR instruction definition/version mismatch"; return false; }
        for (int reg : instruction.uses)
            if (reg < 0 || reg >= f.registers) { error = "IR instruction contains an invalid register use"; return false; }
        for (int reg : instruction.definitions)
            if (reg < 0 || reg >= f.registers) { error = "IR instruction contains an invalid register definition"; return false; }
        if (!instruction.definitions.empty() && instruction.destinationRegister != instruction.definitions.front())
        { error = "IR primary destination does not match definitions"; return false; }
    }
    for (int idom : f.immediateDominators)
        if (idom < -1 || idom >= blocks) { error = "IR contains an invalid immediate dominator"; return false; }
    for (int ipdom : f.immediatePostDominators)
        if (ipdom < -1 || ipdom >= blocks) { error = "IR contains an invalid immediate post-dominator"; return false; }
    for (const auto& edge : f.backEdges)
        if (edge.first < 0 || edge.second < 0 || edge.first >= blocks || edge.second >= blocks) { error = "IR contains an invalid back-edge"; return false; }
    for (const Loop& loop : f.loops)
    {
        if (loop.header < 0 || loop.header >= blocks || loop.preheader >= blocks) { error = "IR contains an invalid loop region"; return false; }
        for (int block : loop.blocks) if (block < 0 || block >= blocks) { error = "IR loop contains an invalid member"; return false; }
        for (int latch : loop.latches) if (latch < 0 || latch >= blocks) { error = "IR loop contains an invalid latch"; return false; }
        for (int exit : loop.exits) if (exit < 0 || exit >= blocks) { error = "IR loop contains an invalid exit"; return false; }
    }
    for (const PhiNode& phi : f.phiNodes)
    {
        if (phi.block < 0 || phi.block >= blocks || phi.reg < 0 || phi.reg >= f.registers) { error = "IR contains an invalid phi node"; return false; }
        if (phi.incomingVersions.size() != f.basicBlocks[phi.block].predecessors.size()) { error = "IR phi node has the wrong number of incoming versions"; return false; }
        if (phi.incomingBlocks.size() != phi.incomingVersions.size()) { error = "IR phi node has the wrong number of incoming blocks"; return false; }
        for (int predecessor : phi.incomingBlocks)
            if (predecessor < 0 || predecessor >= blocks) { error = "IR phi node contains an invalid incoming block"; return false; }
    }
    for (const Function& child : f.children)
    {
        if (child.parentId != f.id) { error = "IR child has the wrong parent id"; return false; }
        if (!validateFunctionAnalysis(child, error)) return false;
    }
    return true;
}
bool validateAnalysis(const Module& module, std::string& error)
{
    if (!validateFunctionAnalysis(module.root, error)) return false;
    std::set<int> ids;
    std::function<bool(const Function&)> collect = [&](const Function& function) {
        if (!ids.insert(function.id).second) { error = "IR contains duplicate function ids"; return false; }
        for (const Function& child : function.children) if (!collect(child)) return false;
        return true;
    };
    return collect(module.root);
}
bool buildModule(const Proto* root, Module& module, std::string& error)
{
    if (!validateProto(root, error)) return false;
    module = Module{}; module.root = Function{}; int nextId = 0; addFunction(root, module.root, nextId, -1, 0);
    return validateAnalysis(module, error);
}

std::string toJson(const Module& m)
{
    std::ostringstream o; o << "{\"format\":\"" << esc(m.format) << "\",\"version\":" << m.version << ",\"source\":\"" << esc(m.source) << "\",\"root_function\":"; jsonFn(o, m.root); o << "}\n"; return o.str();
}

std::string disassemble(const Module& m)
{
    std::ostringstream o; std::function<void(const Function&)> go = [&](const Function& f) { o << "function " << f.id << " \"" << f.nameHint << "\" (parent=" << f.parentId << ")\n"; for (const auto& b : f.basicBlocks) { o << "  block_" << b.id << " [" << b.start << ".." << b.end << "]" << (b.reachable ? "" : " unreachable"); if (b.id < int(f.immediateDominators.size())) o << " idom=block_" << f.immediateDominators[b.id]; for (const PhiNode& phi : f.phiNodes) if (phi.block == b.id) o << " phi=r" << phi.reg << ":v" << phi.version; if (!b.successors.empty()) { o << " ->"; for (int s : b.successors) o << " block_" << s; } o << ":\n"; for (int k : b.instructions) { const auto& i = f.instructions[k]; o << "    @" << i.offset << " (0x" << std::hex << i.offset << std::dec << ") " << opcodeName(i.opcode) << " len=" << i.length << " A=" << i.a << " B=" << i.b << " C=" << i.c << " D=" << i.d << " E=" << i.e; if (i.jumpTarget >= 0) o << " -> " << i.jumpTarget; if (i.line) o << " line=" << i.line; if (i.hasAux) o << " AUX=" << i.aux; if (!i.definitions.empty()) { o << " defs="; for (size_t n = 0; n < i.definitions.size(); ++n) { if (n) o << ','; o << 'r' << i.definitions[n] << ":v" << i.definitionVersions[n]; } } o << "\n"; } } for (const auto& c : f.children) go(c); }; go(m.root); return o.str();
}

std::string cfgDot(const Module& m)
{
    std::set<int> loopHeaders; for (const auto& edge : m.root.backEdges) loopHeaders.insert(edge.second);
    std::ostringstream o; o << "digraph byteveil_cfg {\n"; for (const auto& b : m.root.basicBlocks) { o << "  b" << b.id << " [label=\"block_" << b.id << "\\n" << b.start << ".." << b.end; if (b.id < int(m.root.immediateDominators.size())) o << "\\nidom=" << m.root.immediateDominators[b.id]; if (loopHeaders.count(b.id)) o << "\\nloop-header"; o << "\"];\n"; for (int s : b.successors) o << "  b" << b.id << " -> b" << s << ";\n"; } o << "}\n"; return o.str();
}

std::string constantsText(const Proto* p) { std::ostringstream o; o << "function 0 constants: " << (p ? p->sizek : 0) << "\n"; return o.str(); }
std::string prototypesText(const Proto* p) { std::ostringstream o; o << "function 0 prototypes: " << (p ? p->sizep : 0) << "\n"; return o.str(); }
}

namespace Luau::Decompiler {
std::string inspectBytecode(lua_State* L, std::string& bytecode, const std::string& mode, std::string& error)
{
    if (luau_load(L, "=", bytecode.data(), bytecode.size(), 0) != 0) { error = lua_tostring(L, -1) ? lua_tostring(L, -1) : "bytecode loading failed"; return {}; }
    auto* c = (Closure*)lua_topointer(L, -1); if (!c || !c->l.p) { error = "loaded chunk has no Lua prototype"; return {}; }
    IR::Module m; if (!IR::buildModule(c->l.p, m, error)) return {};
    if (mode == "json" || mode == "ir") return IR::toJson(m);
    if (mode == "disassemble") return IR::disassemble(m);
    if (mode == "cfg") return IR::cfgDot(m);
    if (mode == "structured") return Restructure::render(m);
    if (mode == "constants") return IR::constantsText(c->l.p);
    if (mode == "prototypes") return IR::prototypesText(c->l.p);
    error = "unknown inspection mode: " + mode; return {};
}
}
