#include "IR.h"
#include <algorithm>
#include <functional>
#include <iomanip>
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
        return true;
    default: return false;
    }
}

static bool isConditional(LuauOpcode op)
{
    switch (op)
    {
    case LOP_LOADB: case LOP_JUMPIF: case LOP_JUMPIFNOT: case LOP_JUMPIFEQ:
    case LOP_JUMPIFLE: case LOP_JUMPIFLT: case LOP_JUMPIFNOTEQ: case LOP_JUMPIFNOTLE:
    case LOP_JUMPIFNOTLT: case LOP_JUMPIFEQK: case LOP_JUMPIFNOTEQK: case LOP_FORNPREP:
    case LOP_FORNLOOP: case LOP_FORGLOOP: case LOP_FORGPREP_INEXT: case LOP_FORGLOOP_INEXT:
    case LOP_FORGPREP_NEXT: case LOP_FORGLOOP_NEXT: case LOP_FORGPREP:
        return true;
    default: return false;
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
    case LOP_LOADNIL: case LOP_LOADB: case LOP_LOADN: case LOP_LOADK: case LOP_MOVE:
        return "load";
    case LOP_ADD: case LOP_SUB: case LOP_MUL: case LOP_DIV: case LOP_MOD: case LOP_POW:
    case LOP_ADDK: case LOP_SUBK: case LOP_MULK: case LOP_DIVK: case LOP_MODK: case LOP_POWK:
        return "binary";
    case LOP_CALL: case LOP_NAMECALL: case LOP_FASTCALL: case LOP_FASTCALL1: case LOP_FASTCALL2: case LOP_FASTCALL2K:
        return "call";
    case LOP_NEWCLOSURE: case LOP_DUPCLOSURE: case LOP_CAPTURE:
        return "closure";
    case LOP_RETURN: return "return";
    default: return "instruction";
    }
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

    std::set<int> boundaries{0};
    for (int pc = 0; pc < p->sizecode;)
    {
        ++total;
        if (total > maxInstructions) { error = "instruction limit exceeded"; return false; }
        uint32_t raw = p->code[pc];
        int op = int(LUAU_INSN_OP(raw));
        if (op < 0 || op >= LOP__COUNT) { error = "function " + std::to_string(functionId) + " offset " + std::to_string(pc) + ": unknown opcode " + std::to_string(op); return false; }
        int len = opLength(LuauOpcode(op));
        if (pc + len > p->sizecode) { error = "function " + std::to_string(functionId) + " offset " + std::to_string(pc) + ": missing AUX instruction"; return false; }
        if (LUAU_INSN_A(raw) >= p->maxstacksize && (op != LOP_BREAK && op != LOP_NOP && op != LOP_COVERAGE))
        { error = "function " + std::to_string(functionId) + " offset " + std::to_string(pc) + ": register A out of range"; return false; }
        if ((op == LOP_LOADK || op == LOP_ADDK || op == LOP_SUBK || op == LOP_MULK || op == LOP_DIVK || op == LOP_MODK || op == LOP_POWK || op == LOP_DUPCLOSURE) && LUAU_INSN_D(raw) >= p->sizek)
        { error = "function " + std::to_string(functionId) + " offset " + std::to_string(pc) + ": constant index out of range"; return false; }
        if ((op == LOP_NEWCLOSURE) && LUAU_INSN_D(raw) >= p->sizep)
        { error = "function " + std::to_string(functionId) + " offset " + std::to_string(pc) + ": prototype index out of range"; return false; }
        int target = jumpTarget(raw, pc);
        if (target >= 0) { if (target < 0 || target >= p->sizecode) { error = "function " + std::to_string(functionId) + " offset " + std::to_string(pc) + ": jump target out of range"; return false; } boundaries.insert(target); }
        if (op == LOP_CAPTURE && LUAU_INSN_B(raw) >= p->maxstacksize && LUAU_INSN_A(raw) != LCT_UPVAL)
        { error = "function " + std::to_string(functionId) + " offset " + std::to_string(pc) + ": capture register out of range"; return false; }
        pc += len;
    }
    for (int i = 0; i < p->sizep; ++i)
        if (!validateOne(p->p[i], error, depth + 1, total, maxDepth, maxInstructions, functionId + i + 1)) return false;
    return true;
}

static void addFunction(const Proto* p, Function& f, int id, int parentId, int protoIndex)
{
    f.id = id; f.parentId = parentId; f.prototypeIndex = protoIndex;
    f.parameters = p->numparams; f.registers = p->maxstacksize; f.constants = p->sizek; f.upvalues = p->nups; f.lineDefined = p->linedefined;
    f.nameHint = p->debugname ? p->debugname->data : "function_" + std::to_string(id);
    for (int pc = 0; pc < p->sizecode;)
    {
        uint32_t raw = p->code[pc]; LuauOpcode op = LuauOpcode(LUAU_INSN_OP(raw));
        Instruction i; i.offset = pc; i.opcode = int(op); i.length = opLength(op); i.a = LUAU_INSN_A(raw); i.b = LUAU_INSN_B(raw); i.c = LUAU_INSN_C(raw); i.d = LUAU_INSN_D(raw); i.e = LUAU_INSN_E(raw); i.hasAux = hasAux(op); i.jumpTarget = jumpTarget(raw, pc); i.semanticTag = tag(op);
        if (p->lineinfo && pc < p->sizecode) i.line = luaG_getline(const_cast<Proto*>(p), pc);
        f.instructions.push_back(i); pc += i.length;
    }
    std::set<int> starts{0};
    for (const Instruction& i : f.instructions) if (i.jumpTarget >= 0) starts.insert(i.jumpTarget);
    for (const Instruction& i : f.instructions) if (i.jumpTarget >= 0 && i.offset + i.length < p->sizecode) starts.insert(i.offset + i.length);
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
            std::sort(block.successors.begin(), block.successors.end()); block.successors.erase(std::unique(block.successors.begin(), block.successors.end()), block.successors.end());
        }
        f.basicBlocks.push_back(std::move(block));
    }
    // Medal's restructurer relies on dominators and natural-loop headers.  Keep
    // the same useful CFG facts in the neutral ByteVeil IR so future AST passes
    // do not need to rediscover them from serialized instructions.
    const int blockCount = int(f.basicBlocks.size());
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
    }
    int childId = id + 1;
    for (int c = 0; c < p->sizep; ++c) { Function child; addFunction(p->p[c], child, childId, id, c); childId += 1; f.children.push_back(std::move(child)); }
}

static void jsonFn(std::ostringstream& o, const Function& f)
{
    o << "{\"id\":" << f.id << ",\"parent_id\":" << f.parentId << ",\"prototype_index\":" << f.prototypeIndex << ",\"name_hint\":\"" << esc(f.nameHint) << "\",\"line_defined\":" << f.lineDefined << ",\"parameters\":" << f.parameters << ",\"registers\":" << f.registers << ",\"constants\":" << f.constants << ",\"upvalues\":" << f.upvalues << ",\"instructions\":[";
    for (size_t n = 0; n < f.instructions.size(); ++n) { if (n) o << ','; const auto& i = f.instructions[n]; o << "{\"offset\":" << i.offset << ",\"opcode\":" << i.opcode << ",\"opcode_name\":\"" << opcodeName(i.opcode) << "\",\"length\":" << i.length << ",\"a\":" << i.a << ",\"b\":" << i.b << ",\"c\":" << i.c << ",\"d\":" << i.d << ",\"e\":" << i.e << ",\"line\":" << i.line << ",\"jump_target\":" << i.jumpTarget << ",\"has_aux\":" << (i.hasAux ? "true" : "false") << ",\"semantic_tag\":\"" << i.semanticTag << "\"}"; }
    o << "],\"basic_blocks\":[";
    for (size_t n = 0; n < f.basicBlocks.size(); ++n) { if (n) o << ','; const auto& b = f.basicBlocks[n]; o << "{\"id\":" << b.id << ",\"start\":" << b.start << ",\"end\":" << b.end << ",\"instructions\":["; for (size_t k = 0; k < b.instructions.size(); ++k) { if (k) o << ','; o << b.instructions[k]; } o << "],\"successors\":["; for (size_t k = 0; k < b.successors.size(); ++k) { if (k) o << ','; o << b.successors[k]; } o << "]}"; }
    o << "],\"cfg_analysis\":{";
    o << "\"immediate_dominators\":[";
    for (size_t n = 0; n < f.immediateDominators.size(); ++n) { if (n) o << ','; o << f.immediateDominators[n]; }
    o << "],\"back_edges\":[";
    for (size_t n = 0; n < f.backEdges.size(); ++n) { if (n) o << ','; o << "[" << f.backEdges[n].first << "," << f.backEdges[n].second << "]"; }
    o << "],\"natural_loops\":[";
    for (size_t n = 0; n < f.naturalLoops.size(); ++n) { if (n) o << ','; o << '['; for (size_t k = 0; k < f.naturalLoops[n].size(); ++k) { if (k) o << ','; o << f.naturalLoops[n][k]; } o << ']'; }
    o << "]},\"children\":["; for (size_t n = 0; n < f.children.size(); ++n) { if (n) o << ','; jsonFn(o, f.children[n]); } o << "]}";
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

bool buildModule(const Proto* root, Module& module, std::string& error)
{
    if (!validateProto(root, error)) return false;
    module = Module{}; module.root = Function{}; addFunction(root, module.root, 0, -1, 0); return true;
}

std::string toJson(const Module& m)
{
    std::ostringstream o; o << "{\"format\":\"" << esc(m.format) << "\",\"version\":" << m.version << ",\"source\":\"" << esc(m.source) << "\",\"root_function\":"; jsonFn(o, m.root); o << "}\n"; return o.str();
}

std::string disassemble(const Module& m)
{
    std::ostringstream o; std::function<void(const Function&)> go = [&](const Function& f) { o << "function " << f.id << " \"" << f.nameHint << "\" (parent=" << f.parentId << ")\n"; for (const auto& b : f.basicBlocks) { o << "  block_" << b.id << " [" << b.start << ".." << b.end << "]"; if (b.id < int(f.immediateDominators.size())) o << " idom=block_" << f.immediateDominators[b.id]; if (!b.successors.empty()) { o << " ->"; for (int s : b.successors) o << " block_" << s; } o << ":\n"; for (int k : b.instructions) { const auto& i = f.instructions[k]; o << "    @" << i.offset << " (0x" << std::hex << i.offset << std::dec << ") " << opcodeName(i.opcode) << " len=" << i.length << " A=" << i.a << " B=" << i.b << " C=" << i.c << " D=" << i.d << " E=" << i.e; if (i.jumpTarget >= 0) o << " -> " << i.jumpTarget; if (i.line) o << " line=" << i.line; if (i.hasAux) o << " AUX"; o << "\n"; } } for (const auto& c : f.children) go(c); }; go(m.root); return o.str();
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
    if (mode == "constants") return IR::constantsText(c->l.p);
    if (mode == "prototypes") return IR::prototypesText(c->l.p);
    error = "unknown inspection mode: " + mode; return {};
}
}
