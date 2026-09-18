#include "RegisterDecompile.h"

#include "IR.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace Luau::Decompiler::RegisterDecompile {
namespace {

struct FunctionInfo {
    const Proto* proto = nullptr;
    int id = -1;
    std::vector<int> childIds;
};

class Renderer {
public:
    explicit Renderer(const Proto* root) { collect(root); }

    std::string render(std::string& error)
    {
        if (functions.empty() || !functions.front().proto) {
            error = "register-state renderer received no root prototype";
            return {};
        }

        std::ostringstream out;
        out << "-- ByteVeil Luau register-state reconstruction\n";
        out << "-- Explicit control flow is retained when it cannot be reduced safely.\n";
        out << "-- Unsupported runtime-specific behavior remains visible in comments.\n";
        out << "local byteveil_functions = {}\n\n";
        for (const FunctionInfo& function : functions)
            renderFunction(out, function);
        out << "return byteveil_functions[0]({}, ...)\n";
        return out.str();
    }

private:
    std::vector<FunctionInfo> functions;
    std::unordered_map<const Proto*, int> protoIds;

    int collect(const Proto* proto)
    {
        const int id = int(functions.size());
        functions.push_back(FunctionInfo{proto, id, {}});
        protoIds[proto] = id;
        std::vector<int> children;
        if (proto)
            for (int index = 0; index < proto->sizep; ++index)
                children.push_back(collect(proto->p[index]));
        functions[id].childIds = std::move(children);
        return id;
    }

    static void line(std::ostringstream& out, int depth, const std::string& text)
    {
        out << std::string(size_t(depth * 4), ' ') << text << '\n';
    }

    static std::string luaString(const char* data, size_t size)
    {
        std::ostringstream out;
        out << '"';
        for (size_t index = 0; index < size; ++index)
        {
            const unsigned char c = static_cast<unsigned char>(data[index]);
            switch (c)
            {
            case '\\': out << "\\\\"; break;
            case '"': out << "\\\""; break;
            case '\a': out << "\\a"; break;
            case '\b': out << "\\b"; break;
            case '\f': out << "\\f"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            case '\v': out << "\\v"; break;
            default:
                if (c >= 32 && c <= 126) out << char(c);
                else out << '\\' << std::setw(3) << std::setfill('0') << std::dec << unsigned(c);
                break;
            }
        }
        out << '"';
        return out.str();
    }

    static std::string number(double value)
    {
        if (std::isnan(value)) return "(0 / 0)";
        if (std::isinf(value)) return value < 0 ? "(-1 / 0)" : "(1 / 0)";
        std::ostringstream out;
        out << std::setprecision(17) << value;
        return out.str();
    }

    std::string constant(const Proto* proto, int index) const
    {
        if (!proto || index < 0 || index >= proto->sizek) return "nil --[[ invalid constant index ]]";
        const TValue& value = proto->k[index];
        switch (value.tt)
        {
        case LUA_TNIL: return "nil";
        case LUA_TBOOLEAN: return value.value.b ? "true" : "false";
        case LUA_TNUMBER: return number(value.value.n);
        case LUA_TSTRING:
            return value.value.gc ? luaString(value.value.gc->ts.data, value.value.gc->ts.len) : "nil --[[ null string ]]";
        case LUA_TFUNCTION: return "nil --[[ closure constant ]]";
        case LUA_TTABLE: return "{} --[[ duplicated table template ]]";
        default: return "nil --[[ unsupported constant type ]]";
        }
    }

    std::string importExpression(const Proto* proto, uint32_t aux) const
    {
        const unsigned int count = aux >> 30;
        const unsigned int ids[] = {(aux >> 20) & 1023, (aux >> 10) & 1023, aux & 1023};
        if (count < 1 || count > 3) return "nil --[[ invalid import path ]]";
        std::string result = "getfenv()[" + constant(proto, int(ids[0])) + "]";
        for (unsigned int index = 1; index < count; ++index)
            result += "[" + constant(proto, int(ids[index])) + "]";
        return result;
    }

    static std::string get(int reg) { return "get(" + std::to_string(reg) + ")"; }
    static std::string set(int reg, const std::string& value) { return "set(" + std::to_string(reg) + ", " + value + ")"; }

    int closureId(const Proto* proto, int constantIndex) const
    {
        if (!proto || constantIndex < 0 || constantIndex >= proto->sizek) return -1;
        const TValue& value = proto->k[constantIndex];
        if (value.tt != LUA_TFUNCTION || !value.value.gc || value.value.gc->cl.isC || !value.value.gc->cl.l.p) return -1;
        auto found = protoIds.find(value.value.gc->cl.l.p);
        return found == protoIds.end() ? -1 : found->second;
    }

    static int instructionLength(LuauOpcode op)
    {
        switch (op)
        {
        case LOP_GETGLOBAL: case LOP_SETGLOBAL: case LOP_GETIMPORT: case LOP_GETTABLEKS:
        case LOP_SETTABLEKS: case LOP_NAMECALL: case LOP_JUMPIFEQ: case LOP_JUMPIFLE:
        case LOP_JUMPIFLT: case LOP_JUMPIFNOTEQ: case LOP_JUMPIFNOTLE: case LOP_JUMPIFNOTLT:
        case LOP_NEWTABLE: case LOP_SETLIST: case LOP_FORGLOOP: case LOP_LOADKX:
        case LOP_JUMPIFEQK: case LOP_JUMPIFNOTEQK: case LOP_FASTCALL2: case LOP_FASTCALL2K:
            return 2;
        default:
            return 1;
        }
    }

    static bool jumpOpcode(LuauOpcode op)
    {
        switch (op)
        {
        case LOP_JUMP: case LOP_JUMPBACK: case LOP_JUMPX: case LOP_JUMPIF: case LOP_JUMPIFNOT:
        case LOP_JUMPIFEQ: case LOP_JUMPIFLE: case LOP_JUMPIFLT: case LOP_JUMPIFNOTEQ:
        case LOP_JUMPIFNOTLE: case LOP_JUMPIFNOTLT: case LOP_JUMPIFEQK: case LOP_JUMPIFNOTEQK:
        case LOP_FORNPREP: case LOP_FORNLOOP: case LOP_FORGPREP: case LOP_FORGLOOP:
        case LOP_FORGPREP_INEXT: case LOP_FORGLOOP_INEXT: case LOP_FORGPREP_NEXT: case LOP_FORGLOOP_NEXT:
            return true;
        default:
            return false;
        }
    }

    static int jumpTarget(uint32_t raw, int pc)
    {
        const LuauOpcode op = LuauOpcode(LUAU_INSN_OP(raw));
        if (op == LOP_JUMPX) return pc + LUAU_INSN_E(raw) + 1;
        if (jumpOpcode(op)) return pc + LUAU_INSN_D(raw) + 1;
        if ((op == LOP_LOADB || op == LOP_FASTCALL || op == LOP_FASTCALL1 || op == LOP_FASTCALL2 || op == LOP_FASTCALL2K) && LUAU_INSN_C(raw))
            return pc + LUAU_INSN_C(raw) + 1;
        return -1;
    }

    static std::vector<std::pair<int, int>> blocks(const Proto* proto)
    {
        std::vector<std::pair<int, int>> result;
        if (!proto || proto->sizecode <= 0) return result;
        std::vector<int> starts{0};
        for (int pc = 0; pc < proto->sizecode;)
        {
            const uint32_t raw = proto->code[pc];
            const LuauOpcode op = LuauOpcode(LUAU_INSN_OP(raw));
            const int length = instructionLength(op);
            const int target = jumpTarget(raw, pc);
            if (target >= 0 && target < proto->sizecode) starts.push_back(target);
            if ((target >= 0 || op == LOP_RETURN) && pc + length < proto->sizecode) starts.push_back(pc + length);
            pc += length;
        }
        std::sort(starts.begin(), starts.end());
        starts.erase(std::unique(starts.begin(), starts.end()), starts.end());
        for (size_t index = 0; index < starts.size(); ++index)
            result.emplace_back(starts[index], index + 1 < starts.size() ? starts[index + 1] : proto->sizecode);
        return result;
    }

    static std::string binary(LuauOpcode op)
    {
        switch (op)
        {
        case LOP_ADD: case LOP_ADDK: return "+";
        case LOP_SUB: case LOP_SUBK: return "-";
        case LOP_MUL: case LOP_MULK: return "*";
        case LOP_DIV: case LOP_DIVK: return "/";
        case LOP_MOD: case LOP_MODK: return "%";
        case LOP_POW: case LOP_POWK: return "^";
        case LOP_AND: case LOP_ANDK: return "and";
        case LOP_OR: case LOP_ORK: return "or";
        default: return "?";
        }
    }

    void emitCall(std::ostringstream& out, int depth, int pc, int a, int b, int c) const
    {
        line(out, depth, "local call_args_" + std::to_string(pc) + " = range(" + std::to_string(a + 1) + ", " + (b == 0 ? "top" : std::to_string(a + b - 1)) + ")");
        line(out, depth, "local call_results_" + std::to_string(pc) + " = table.pack(" + get(a) + "(table.unpack(call_args_" + std::to_string(pc) + ", 1, call_args_" + std::to_string(pc) + ".n)))");
        if (c == 0)
        {
            line(out, depth, "for result_index = 1, call_results_" + std::to_string(pc) + ".n do");
            line(out, depth + 1, "set(" + std::to_string(a) + " + result_index - 1, call_results_" + std::to_string(pc) + "[result_index])");
            line(out, depth, "end");
            line(out, depth, "top = " + std::to_string(a) + " + call_results_" + std::to_string(pc) + ".n - 1");
        }
        else
            for (int result = 0; result < c - 1; ++result)
                line(out, depth, set(a + result, "call_results_" + std::to_string(pc) + "[" + std::to_string(result + 1) + "]"));
    }

    void renderFunction(std::ostringstream& out, const FunctionInfo& function)
    {
        const Proto* proto = function.proto;
        out << "byteveil_functions[" << function.id << "] = function(upvalues, ...)\n";
        line(out, 1, "local registers, cells = {}, {}");
        line(out, 1, "local top = -1");
        line(out, 1, "local function get(index)");
        line(out, 2, "local cell = cells[index]");
        line(out, 2, "if cell then return cell.value end");
        line(out, 2, "return registers[index]");
        line(out, 1, "end");
        line(out, 1, "local function set(index, value)");
        line(out, 2, "local cell = cells[index]");
        line(out, 2, "if cell then cell.value = value else registers[index] = value end");
        line(out, 2, "if index > top then top = index end");
        line(out, 1, "end");
        line(out, 1, "local function upvalue(index)");
        line(out, 2, "local cell = upvalues[index]");
        line(out, 2, "if not cell then cell = { value = nil }; upvalues[index] = cell end");
        line(out, 2, "return cell");
        line(out, 1, "end");
        line(out, 1, "local function range(first, last)");
        line(out, 2, "local values = { n = 0 }");
        line(out, 2, "for index = first, last do values.n = values.n + 1; values[values.n] = get(index) end");
        line(out, 2, "return values");
        line(out, 1, "end");
        if (proto)
            for (int parameter = 0; parameter < proto->numparams; ++parameter)
                line(out, 1, set(parameter, "select(" + std::to_string(parameter + 1) + ", ...)"));

        const std::vector<std::pair<int, int>> functionBlocks = blocks(proto);
        line(out, 1, "local pc = " + std::to_string(functionBlocks.empty() ? 0 : functionBlocks.front().first));
        line(out, 1, "while true do");
        if (functionBlocks.empty())
        {
            line(out, 2, "return");
        }
        else
        {
            for (size_t blockIndex = 0; blockIndex < functionBlocks.size(); ++blockIndex)
            {
                const int start = functionBlocks[blockIndex].first;
                const int end = functionBlocks[blockIndex].second;
                const int fallthrough = blockIndex + 1 < functionBlocks.size() ? functionBlocks[blockIndex + 1].first : -1;
                line(out, 2, std::string(blockIndex == 0 ? "if" : "elseif") + " pc == " + std::to_string(start) + " then");
                bool terminated = false;
                std::string captureName;
                int captureIndex = 0;
                for (int pc = start; proto && pc < end;)
                {
                    const uint32_t raw = proto->code[pc];
                    const LuauOpcode op = LuauOpcode(LUAU_INSN_OP(raw));
                    const int length = instructionLength(op);
                    const int a = LUAU_INSN_A(raw), b = LUAU_INSN_B(raw), c = LUAU_INSN_C(raw), d = LUAU_INSN_D(raw);
                    const uint32_t aux = length == 2 ? proto->code[pc + 1] : 0;
                    const int target = jumpTarget(raw, pc);
                    const std::string next = fallthrough >= 0 ? std::to_string(fallthrough) : "-1";

                    switch (op)
                    {
                    case LOP_NOP: case LOP_COVERAGE: case LOP_PREPVARARGS: break;
                    case LOP_BREAK: line(out, 3, "-- BREAK/debug instruction retained as a no-op"); break;
                    case LOP_LOADNIL: line(out, 3, set(a, "nil")); break;
                    case LOP_LOADB:
                        line(out, 3, set(a, b ? "true" : "false"));
                        if (c) { line(out, 3, "pc = " + std::to_string(target)); terminated = true; }
                        break;
                    case LOP_LOADN: line(out, 3, set(a, std::to_string(d))); break;
                    case LOP_LOADK: line(out, 3, set(a, constant(proto, d))); break;
                    case LOP_LOADKX: line(out, 3, set(a, constant(proto, int(aux)))); break;
                    case LOP_MOVE: line(out, 3, set(a, get(b))); break;
                    case LOP_GETGLOBAL: line(out, 3, set(a, "getfenv()[" + constant(proto, int(aux)) + "]")); break;
                    case LOP_SETGLOBAL: line(out, 3, "getfenv()[" + constant(proto, int(aux)) + "] = " + get(a)); break;
                    case LOP_GETUPVAL: line(out, 3, set(a, "upvalue(" + std::to_string(b) + ").value")); break;
                    case LOP_SETUPVAL: line(out, 3, "upvalue(" + std::to_string(b) + ").value = " + get(a)); break;
                    case LOP_CLOSEUPVALS: line(out, 3, "-- upvalue cells already have heap lifetime for registers >= " + std::to_string(a)); break;
                    case LOP_GETIMPORT: line(out, 3, set(a, importExpression(proto, aux))); break;
                    case LOP_GETTABLE: line(out, 3, set(a, get(b) + "[" + get(c) + "]")); break;
                    case LOP_SETTABLE: line(out, 3, get(b) + "[" + get(c) + "] = " + get(a)); break;
                    case LOP_GETTABLEKS: line(out, 3, set(a, get(b) + "[" + constant(proto, int(aux)) + "]")); break;
                    case LOP_SETTABLEKS: line(out, 3, get(b) + "[" + constant(proto, int(aux)) + "] = " + get(a)); break;
                    case LOP_GETTABLEN: line(out, 3, set(a, get(b) + "[" + std::to_string(c + 1) + "]")); break;
                    case LOP_SETTABLEN: line(out, 3, get(b) + "[" + std::to_string(c + 1) + "] = " + get(a)); break;
                    case LOP_NEWCLOSURE: {
                        const int child = d >= 0 && d < int(function.childIds.size()) ? function.childIds[d] : -1;
                        captureName = "capture_" + std::to_string(pc);
                        captureIndex = 0;
                        line(out, 3, "local " + captureName + " = {}");
                        if (child >= 0) line(out, 3, set(a, "function(...) return byteveil_functions[" + std::to_string(child) + "](" + captureName + ", ...) end"));
                        else line(out, 3, set(a, "nil --[[ unresolved child prototype ]]"));
                        break;
                    }
                    case LOP_DUPCLOSURE: {
                        const int child = closureId(proto, d);
                        captureName = "capture_" + std::to_string(pc);
                        captureIndex = 0;
                        line(out, 3, "local " + captureName + " = {}");
                        if (child >= 0) line(out, 3, set(a, "function(...) return byteveil_functions[" + std::to_string(child) + "](" + captureName + ", ...) end"));
                        else line(out, 3, set(a, "nil --[[ unresolved duplicated closure ]]"));
                        break;
                    }
                    case LOP_CAPTURE:
                        if (captureName.empty()) line(out, 3, "-- orphan CAPTURE " + std::to_string(a) + ", " + std::to_string(b));
                        else if (a == LCT_VAL) line(out, 3, captureName + "[" + std::to_string(captureIndex++) + "] = { value = " + get(b) + " }");
                        else if (a == LCT_REF)
                        {
                            line(out, 3, "if not cells[" + std::to_string(b) + "] then cells[" + std::to_string(b) + "] = { value = registers[" + std::to_string(b) + "] }; registers[" + std::to_string(b) + "] = nil end");
                            line(out, 3, captureName + "[" + std::to_string(captureIndex++) + "] = cells[" + std::to_string(b) + "]");
                        }
                        else if (a == LCT_UPVAL) line(out, 3, captureName + "[" + std::to_string(captureIndex++) + "] = upvalue(" + std::to_string(b) + ")");
                        else line(out, 3, "-- unknown capture type " + std::to_string(a));
                        break;
                    case LOP_NAMECALL:
                        line(out, 3, set(a, get(b) + "[" + constant(proto, int(aux)) + "]"));
                        line(out, 3, set(a + 1, get(b)));
                        break;
                    case LOP_CALL: emitCall(out, 3, pc, a, b, c); break;
                    case LOP_RETURN: {
                        if (b == 1) line(out, 3, "return");
                        else
                        {
                            line(out, 3, "local return_values = range(" + std::to_string(a) + ", " + (b == 0 ? "top" : std::to_string(a + b - 2)) + ")");
                            line(out, 3, "return table.unpack(return_values, 1, return_values.n)");
                        }
                        terminated = true;
                        break;
                    }
                    case LOP_JUMP: case LOP_JUMPBACK: case LOP_JUMPX:
                    case LOP_FORGPREP: case LOP_FORGPREP_INEXT: case LOP_FORGPREP_NEXT:
                        if (op == LOP_FORGPREP) line(out, 3, "-- generic iterator metamethod preparation is represented by the loop call below");
                        line(out, 3, "pc = " + std::to_string(target)); terminated = true; break;
                    case LOP_JUMPIF: case LOP_JUMPIFNOT:
                        line(out, 3, "if " + std::string(op == LOP_JUMPIFNOT ? "not " : "") + get(a) + " then pc = " + std::to_string(target) + " else pc = " + next + " end");
                        terminated = true; break;
                    case LOP_JUMPIFEQ: case LOP_JUMPIFLE: case LOP_JUMPIFLT:
                    case LOP_JUMPIFNOTEQ: case LOP_JUMPIFNOTLE: case LOP_JUMPIFNOTLT: {
                        std::string comparison;
                        if (op == LOP_JUMPIFEQ) comparison = get(a) + " == " + get(int(aux));
                        else if (op == LOP_JUMPIFLE) comparison = get(a) + " <= " + get(int(aux));
                        else if (op == LOP_JUMPIFLT) comparison = get(a) + " < " + get(int(aux));
                        else if (op == LOP_JUMPIFNOTEQ) comparison = get(a) + " ~= " + get(int(aux));
                        else if (op == LOP_JUMPIFNOTLE) comparison = "not (" + get(a) + " <= " + get(int(aux)) + ")";
                        else comparison = "not (" + get(a) + " < " + get(int(aux)) + ")";
                        line(out, 3, "if " + comparison + " then pc = " + std::to_string(target) + " else pc = " + next + " end");
                        terminated = true; break;
                    }
                    case LOP_JUMPIFEQK: case LOP_JUMPIFNOTEQK:
                        line(out, 3, "if " + get(a) + (op == LOP_JUMPIFEQK ? " == " : " ~= ") + constant(proto, int(aux)) + " then pc = " + std::to_string(target) + " else pc = " + next + " end");
                        terminated = true; break;
                    case LOP_ADD: case LOP_SUB: case LOP_MUL: case LOP_DIV: case LOP_MOD: case LOP_POW:
                    case LOP_AND: case LOP_OR:
                        line(out, 3, set(a, "(" + get(b) + " " + binary(op) + " " + get(c) + ")")); break;
                    case LOP_ADDK: case LOP_SUBK: case LOP_MULK: case LOP_DIVK: case LOP_MODK: case LOP_POWK:
                    case LOP_ANDK: case LOP_ORK:
                        line(out, 3, set(a, "(" + get(b) + " " + binary(op) + " " + constant(proto, c) + ")")); break;
                    case LOP_CONCAT: {
                        std::string expression;
                        for (int reg = b; reg <= c; ++reg) expression += (expression.empty() ? "" : " .. ") + get(reg);
                        line(out, 3, set(a, "(" + expression + ")")); break;
                    }
                    case LOP_NOT: line(out, 3, set(a, "not " + get(b))); break;
                    case LOP_MINUS: line(out, 3, set(a, "-" + get(b))); break;
                    case LOP_LENGTH: line(out, 3, set(a, "#" + get(b))); break;
                    case LOP_NEWTABLE: line(out, 3, set(a, "{}")); break;
                    case LOP_DUPTABLE: line(out, 3, set(a, constant(proto, d))); break;
                    case LOP_SETLIST: {
                        const std::string last = c == 0 ? "top" : std::to_string(b + c - 2);
                        line(out, 3, "for source = " + std::to_string(b) + ", " + last + " do " + get(a) + "[" + std::to_string(aux) + " + source - " + std::to_string(b) + "] = get(source) end");
                        break;
                    }
                    case LOP_FORNPREP: {
                        const std::string condition = "((" + get(a + 1) + " > 0 and " + get(a + 2) + " <= " + get(a) + ") or (not (" + get(a + 1) + " > 0) and " + get(a) + " <= " + get(a + 2) + "))";
                        line(out, 3, "if " + condition + " then pc = " + next + " else pc = " + std::to_string(target) + " end");
                        terminated = true; break;
                    }
                    case LOP_FORNLOOP: {
                        line(out, 3, set(a + 2, get(a + 2) + " + " + get(a + 1)));
                        const std::string condition = "((" + get(a + 1) + " > 0 and " + get(a + 2) + " <= " + get(a) + ") or (not (" + get(a + 1) + " > 0) and " + get(a) + " <= " + get(a + 2) + "))";
                        line(out, 3, "if " + condition + " then pc = " + std::to_string(target) + " else pc = " + next + " end");
                        terminated = true; break;
                    }
                    case LOP_FORGLOOP: case LOP_FORGLOOP_INEXT: case LOP_FORGLOOP_NEXT: {
                        const int variables = op == LOP_FORGLOOP ? int(aux) : 2;
                        line(out, 3, "local iterator_results_" + std::to_string(pc) + " = table.pack(" + get(a) + "(" + get(a + 1) + ", " + get(a + 2) + "))");
                        for (int variable = 0; variable < variables; ++variable)
                            line(out, 3, set(a + 3 + variable, "iterator_results_" + std::to_string(pc) + "[" + std::to_string(variable + 1) + "]"));
                        line(out, 3, set(a + 2, "iterator_results_" + std::to_string(pc) + "[1]"));
                        line(out, 3, "if iterator_results_" + std::to_string(pc) + "[1] ~= nil then pc = " + std::to_string(target) + " else pc = " + next + " end");
                        terminated = true; break;
                    }
                    case LOP_GETVARARGS:
                        if (b == 0)
                        {
                            line(out, 3, "local vararg_count = math.max(0, select(\"#\", ...) - " + std::to_string(proto->numparams) + ")");
                            line(out, 3, "for vararg_index = 1, vararg_count do set(" + std::to_string(a) + " + vararg_index - 1, select(" + std::to_string(proto->numparams) + " + vararg_index, ...)) end");
                            line(out, 3, "top = " + std::to_string(a) + " + vararg_count - 1");
                        }
                        else
                            for (int variable = 0; variable < b - 1; ++variable)
                                line(out, 3, set(a + variable, "select(" + std::to_string(proto->numparams + variable + 1) + ", ...)"));
                        break;
                    case LOP_FASTCALL: case LOP_FASTCALL1: case LOP_FASTCALL2: case LOP_FASTCALL2K:
                        line(out, 3, "-- FASTCALL optimization omitted; execute its ordinary fallback sequence");
                        if (target >= 0) { line(out, 3, "pc = " + next); terminated = true; }
                        break;
                    default:
                        line(out, 3, "-- unsupported opcode @" + std::to_string(pc) + ": " + IR::opcodeName(int(op)));
                        break;
                    }
                    pc += length;
                }
                if (!terminated)
                {
                    if (fallthrough >= 0) line(out, 3, "pc = " + std::to_string(fallthrough));
                    else line(out, 3, "return");
                }
            }
            line(out, 2, "else");
            line(out, 3, "error(\"ByteVeil reconstruction reached invalid pc \" .. tostring(pc))");
            line(out, 2, "end");
        }
        line(out, 1, "end");
        out << "end\n\n";
    }
};

bool treeNeedsRegisterRenderer(const Proto* proto)
{
    if (!proto) return false;
    // The inherited AST path can print nested functions while attaching a
    // capture to the wrong closure or call target. Keep the entire prototype
    // tree in one register model whenever closures are present.
    if (proto->sizep > 0)
        return true;
    for (int pc = 0; pc < proto->sizecode;)
    {
        const LuauOpcode op = LuauOpcode(LUAU_INSN_OP(proto->code[pc]));
        switch (op)
        {
        case LOP_JUMP: case LOP_JUMPBACK: case LOP_JUMPX: case LOP_JUMPIF: case LOP_JUMPIFNOT:
        case LOP_JUMPIFEQ: case LOP_JUMPIFLE: case LOP_JUMPIFLT: case LOP_JUMPIFNOTEQ:
        case LOP_JUMPIFNOTLE: case LOP_JUMPIFNOTLT: case LOP_JUMPIFEQK: case LOP_JUMPIFNOTEQK:
        case LOP_FORNPREP: case LOP_FORNLOOP: case LOP_FORGPREP: case LOP_FORGLOOP:
        case LOP_FORGPREP_INEXT: case LOP_FORGLOOP_INEXT: case LOP_FORGPREP_NEXT: case LOP_FORGLOOP_NEXT:
        case LOP_LOADB: case LOP_FASTCALL: case LOP_FASTCALL1: case LOP_FASTCALL2: case LOP_FASTCALL2K:
        case LOP_SETUPVAL: case LOP_GETTABLE: case LOP_SETTABLEKS: case LOP_SETTABLEN:
        case LOP_NEWCLOSURE: case LOP_DUPCLOSURE: case LOP_CAPTURE:
            return true;
        default:
            break;
        }
        switch (op)
        {
        case LOP_GETGLOBAL: case LOP_SETGLOBAL: case LOP_GETIMPORT: case LOP_GETTABLEKS:
        case LOP_SETTABLEKS: case LOP_NAMECALL: case LOP_JUMPIFEQ: case LOP_JUMPIFLE:
        case LOP_JUMPIFLT: case LOP_JUMPIFNOTEQ: case LOP_JUMPIFNOTLE: case LOP_JUMPIFNOTLT:
        case LOP_NEWTABLE: case LOP_SETLIST: case LOP_FORGLOOP: case LOP_LOADKX:
        case LOP_JUMPIFEQK: case LOP_JUMPIFNOTEQK: case LOP_FASTCALL2: case LOP_FASTCALL2K:
            pc += 2;
            break;
        default:
            pc += 1;
            break;
        }
    }
    for (int child = 0; child < proto->sizep; ++child)
        if (treeNeedsRegisterRenderer(proto->p[child])) return true;
    return false;
}

} // namespace

std::string render(const Proto* root, std::string& error)
{
    Renderer renderer(root);
    return renderer.render(error);
}

bool shouldPrefer(const Proto* root)
{
    return treeNeedsRegisterRenderer(root);
}

} // namespace Luau::Decompiler::RegisterDecompile
