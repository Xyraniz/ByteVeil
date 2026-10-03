#include "RobloxBytecode.h"

#include "Luau/Bytecode.h"
#include "Luau/BytecodeUtils.h"

#include <cstdint>
#include <limits>

namespace Luau::Decompiler::RobloxBytecode {
namespace {

constexpr size_t kMaxInputBytes = 256u * 1024u * 1024u;
constexpr uint32_t kMaxObjects = 2'000'000;

struct CodeRange {
    size_t offset = 0;
    uint32_t words = 0;
};

class Reader {
public:
    explicit Reader(std::string_view bytes) : bytes_(bytes) {}

    size_t offset() const { return pos_; }
    size_t remaining() const { return bytes_.size() - pos_; }

    bool u8(uint8_t& value)
    {
        if (remaining() < 1) return fail("truncated byte");
        value = static_cast<uint8_t>(bytes_[pos_++]);
        return true;
    }

    bool u32(uint32_t& value)
    {
        if (remaining() < 4) return fail("truncated 32-bit value");
        const auto* b = reinterpret_cast<const uint8_t*>(bytes_.data() + pos_);
        value = uint32_t(b[0]) | (uint32_t(b[1]) << 8) | (uint32_t(b[2]) << 16) | (uint32_t(b[3]) << 24);
        pos_ += 4;
        return true;
    }

    bool var(uint32_t& value)
    {
        value = 0;
        for (unsigned int i = 0; i < 5; ++i) {
            uint8_t byte = 0;
            if (!u8(byte)) return false;
            if (i == 4 && (byte & 0xf0)) return fail("ULEB128 integer overflow");
            value |= uint32_t(byte & 0x7f) << (i * 7);
            if ((byte & 0x80) == 0) return true;
        }
        return fail("ULEB128 integer is too long");
    }

    bool var64()
    {
        for (unsigned int i = 0; i < 10; ++i) {
            uint8_t byte = 0;
            if (!u8(byte)) return false;
            if (i == 9 && (byte & 0xfe)) return fail("64-bit ULEB128 integer overflow");
            if ((byte & 0x80) == 0) return true;
        }
        return fail("64-bit ULEB128 integer is too long");
    }

    bool skip(uint64_t count)
    {
        if (count > remaining()) return fail("truncated field");
        pos_ += static_cast<size_t>(count);
        return true;
    }

    bool fail(const char* message)
    {
        error_ = message;
        return false;
    }

    const std::string& error() const { return error_; }

private:
    std::string_view bytes_;
    size_t pos_ = 0;
    std::string error_;
};

bool count(Reader& reader, uint32_t& value, const char* name)
{
    if (!reader.var(value)) return false;
    if (value > kMaxObjects) return reader.fail(name);
    return true;
}

bool stringId(Reader& reader, uint32_t strings)
{
    uint32_t id = 0;
    if (!reader.var(id)) return false;
    if (id > strings) return reader.fail("string table index is out of range");
    return true;
}

bool parse(std::string_view input, uint8_t& version, std::vector<CodeRange>& code, std::string& error)
{
    if (input.size() >= 5 && input.substr(0, 5) == std::string_view("\x1bLuau", 5))
        input.remove_prefix(5);
    if (input.empty() || input.size() > kMaxInputBytes) {
        error = input.empty() ? "empty input" : "input exceeds 256 MiB safety limit";
        return false;
    }

    Reader reader(input);
    uint8_t typeVersion = 0;
    if (!reader.u8(version) || version < LBC_VERSION_MIN || version > LBC_VERSION_MAX) {
        error = version < LBC_VERSION_MIN || version > LBC_VERSION_MAX ? "unsupported Luau bytecode version" : reader.error();
        return false;
    }
    if (version >= 4) {
        if (!reader.u8(typeVersion)) { error = reader.error(); return false; }
        if (typeVersion < LBC_TYPE_VERSION_MIN || typeVersion > LBC_TYPE_VERSION_MAX) {
            error = "unsupported Luau type-info version";
            return false;
        }
    }

    uint32_t stringCount = 0;
    if (!count(reader, stringCount, "string table count exceeds safety limit")) { error = reader.error(); return false; }
    uint64_t totalStringBytes = 0;
    for (uint32_t i = 0; i < stringCount; ++i) {
        uint32_t length = 0;
        if (!reader.var(length)) { error = reader.error(); return false; }
        totalStringBytes += length;
        if (totalStringBytes > kMaxInputBytes || !reader.skip(length)) { error = reader.error().empty() ? "string data exceeds safety limit" : reader.error(); return false; }
    }

    if (typeVersion == 3) {
        uint8_t index = 0;
        uint32_t entries = 0;
        do {
            if (!reader.u8(index)) { error = reader.error(); return false; }
            if (index == 0) break;
            if (++entries > 255 || !stringId(reader, stringCount)) {
                error = reader.error().empty() ? "userdata type map exceeds safety limit" : reader.error();
                return false;
            }
        } while (true);
    }

    uint32_t protoCount = 0;
    if (!count(reader, protoCount, "prototype count exceeds safety limit") || protoCount == 0) {
        error = reader.error().empty() ? "prototype table is empty" : reader.error();
        return false;
    }

    uint64_t totalCodeWords = 0;
    uint64_t totalConstants = 0;
    code.clear();
    code.reserve(protoCount);
    for (uint32_t protoIndex = 0; protoIndex < protoCount; ++protoIndex) {
        size_t protoEnd = input.size();
        if (version >= 12) {
            uint32_t protoSize = 0;
            if (!reader.var(protoSize) || protoSize == 0 || protoSize > reader.remaining()) {
                error = reader.error().empty() ? "prototype size is out of range" : reader.error();
                return false;
            }
            protoEnd = reader.offset() + protoSize;
        }

        uint8_t maxStack = 0, params = 0, upvalues = 0, vararg = 0, flags = 0;
        if (!reader.u8(maxStack) || !reader.u8(params) || !reader.u8(upvalues) || !reader.u8(vararg)) {
            error = reader.error(); return false;
        }
        (void)maxStack;
        if (params > maxStack || upvalues > 200 || vararg > 1) { error = "invalid prototype register metadata"; return false; }

        if (version >= 4) {
            if (!reader.u8(flags)) { error = reader.error(); return false; }
            uint32_t typeInfoSize = 0;
            if (!reader.var(typeInfoSize) || !reader.skip(typeInfoSize)) { error = reader.error(); return false; }
        }

        uint32_t codeWords = 0;
        if (!count(reader, codeWords, "instruction word count exceeds safety limit")) { error = reader.error(); return false; }
        totalCodeWords += codeWords;
        if (totalCodeWords > kMaxObjects) { error = "aggregate instruction word count exceeds safety limit"; return false; }
        CodeRange range{reader.offset(), codeWords};
        if (!reader.skip(uint64_t(codeWords) * 4)) { error = reader.error(); return false; }
        code.push_back(range);

        uint32_t constantCount = 0;
        if (!count(reader, constantCount, "constant count exceeds safety limit")) { error = reader.error(); return false; }
        totalConstants += constantCount;
        if (totalConstants > kMaxObjects) { error = "aggregate constant count exceeds safety limit"; return false; }
        for (uint32_t i = 0; i < constantCount; ++i) {
            uint8_t kind = 0;
            if (!reader.u8(kind)) { error = reader.error(); return false; }
            uint32_t a = 0, b = 0, c = 0;
            switch (kind) {
            case LBC_CONSTANT_NIL: break;
            case LBC_CONSTANT_BOOLEAN: if (!reader.skip(1)) { error = reader.error(); return false; } break;
            case LBC_CONSTANT_NUMBER: if (!reader.skip(8)) { error = reader.error(); return false; } break;
            case LBC_CONSTANT_STRING: if (!stringId(reader, stringCount)) { error = reader.error(); return false; } break;
            case LBC_CONSTANT_IMPORT: if (!reader.skip(4)) { error = reader.error(); return false; } break;
            case LBC_CONSTANT_TABLE:
                if (!count(reader, a, "table constant key count exceeds safety limit")) { error = reader.error(); return false; }
                for (uint32_t j = 0; j < a; ++j) {
                    if (!reader.var(b) || b >= constantCount) { error = reader.error().empty() ? "table constant key index is out of range" : reader.error(); return false; }
                }
                break;
            case LBC_CONSTANT_CLOSURE:
                if (!reader.var(a) || a >= protoCount) { error = reader.error().empty() ? "closure prototype index is out of range" : reader.error(); return false; }
                break;
            case LBC_CONSTANT_VECTOR:
                if (version < 5 || !reader.skip(16)) { error = reader.error().empty() ? "vector constant is invalid for bytecode version" : reader.error(); return false; }
                break;
            case LBC_CONSTANT_TABLE_WITH_CONSTANTS:
                if (version < 7 || !count(reader, a, "table-with-constants key count exceeds safety limit")) { error = reader.error().empty() ? "table-with-constants requires bytecode version 7" : reader.error(); return false; }
                for (uint32_t j = 0; j < a; ++j) {
                    int32_t valueIndex = -1;
                    if (!reader.var(b) || b >= constantCount || !reader.u32(c)) { error = reader.error().empty() ? "table-with-constants data is invalid" : reader.error(); return false; }
                    valueIndex = static_cast<int32_t>(c);
                    if (valueIndex >= 0 && static_cast<uint32_t>(valueIndex) >= constantCount) { error = "table constant value index is out of range"; return false; }
                }
                break;
            case LBC_CONSTANT_INTEGER:
                if (version < 8 || !reader.skip(1) || !reader.var64()) { error = reader.error().empty() ? "integer constant is invalid for bytecode version" : reader.error(); return false; }
                break;
            case LBC_CONSTANT_CLASS_SHAPE:
                if (version < 10 || !reader.var(a) || a >= constantCount || !reader.var(b) || !reader.var(c)) { error = reader.error().empty() ? "class shape is invalid for bytecode version" : reader.error(); return false; }
                if (uint64_t(b) + c > kMaxObjects) { error = "class shape member count exceeds safety limit"; return false; }
                for (uint64_t j = 0; j < uint64_t(b) + c; ++j) {
                    uint32_t member = 0;
                    if (!reader.var(member) || member >= constantCount) { error = reader.error().empty() ? "class shape member index is out of range" : reader.error(); return false; }
                }
                break;
            case LBC_CONSTANT_VECTORD:
                if (version < 13 || !reader.skip(32)) { error = reader.error().empty() ? "double vector constant is invalid for bytecode version" : reader.error(); return false; }
                break;
            default:
                error = "unknown Luau constant tag " + std::to_string(kind);
                return false;
            }
        }

        uint32_t childCount = 0;
        if (!count(reader, childCount, "child prototype count exceeds safety limit")) { error = reader.error(); return false; }
        for (uint32_t i = 0; i < childCount; ++i) {
            uint32_t child = 0;
            if (!reader.var(child) || child >= protoCount) { error = reader.error().empty() ? "child prototype index is out of range" : reader.error(); return false; }
        }

        uint32_t ignored = 0;
        if (!reader.var(ignored) || !stringId(reader, stringCount)) { error = reader.error(); return false; }
        uint8_t lineInfo = 0;
        if (!reader.u8(lineInfo) || lineInfo > 1) { error = reader.error().empty() ? "invalid line-info flag" : reader.error(); return false; }
        if (lineInfo) {
            uint8_t gap = 0;
            if (!reader.u8(gap) || gap >= 32) { error = reader.error().empty() ? "invalid line-info interval" : reader.error(); return false; }
            uint64_t intervals = codeWords == 0 ? 0 : ((uint64_t(codeWords) - 1) >> gap) + 1;
            if (!reader.skip(uint64_t(codeWords) + intervals * 4)) { error = reader.error(); return false; }
        }

        uint8_t debugInfo = 0;
        if (!reader.u8(debugInfo) || debugInfo > 1) { error = reader.error().empty() ? "invalid debug-info flag" : reader.error(); return false; }
        if (debugInfo) {
            uint32_t localCount = 0;
            if (!count(reader, localCount, "local debug count exceeds safety limit")) { error = reader.error(); return false; }
            for (uint32_t i = 0; i < localCount; ++i) {
                uint32_t start = 0, end = 0;
                uint8_t reg = 0;
                if (!stringId(reader, stringCount) || !reader.var(start) || !reader.var(end) || !reader.u8(reg)) { error = reader.error(); return false; }
                if (reg >= maxStack) { error = "debug local register is out of range"; return false; }
            }
            uint32_t upvalueNameCount = 0;
            if (!count(reader, upvalueNameCount, "upvalue debug count exceeds safety limit")) { error = reader.error(); return false; }
            for (uint32_t i = 0; i < upvalueNameCount; ++i)
                if (!stringId(reader, stringCount)) { error = reader.error(); return false; }
        }

        if (version >= 11) {
            uint32_t feedbackCount = 0;
            if (!count(reader, feedbackCount, "feedback-vector count exceeds safety limit")) { error = reader.error(); return false; }
            for (uint32_t i = 0; i < feedbackCount; ++i) {
                uint8_t slotKind = 0;
                if (!reader.u8(slotKind) || slotKind != LFT_CALLTARGET || !reader.var(ignored)) { error = reader.error().empty() ? "unsupported feedback-vector slot" : reader.error(); return false; }
            }
        }

        if (version >= 12) {
            if ((flags & LPF_INLINABLE) != 0 && !reader.var64()) { error = reader.error(); return false; }
            if (reader.offset() > protoEnd) { error = "prototype fields exceed declared size"; return false; }
            // Future experimental versions may append fields inside a sized proto.
            if (!reader.skip(protoEnd - reader.offset())) { error = reader.error(); return false; }
        }
    }

    uint32_t mainProto = 0;
    if (!reader.var(mainProto) || mainProto >= protoCount) { error = reader.error().empty() ? "main prototype index is out of range" : reader.error(); return false; }
    const size_t trailing = reader.remaining();
    if (trailing != 0 && trailing != 24) { error = "unexpected trailing data after main prototype (expected 0 or 24 bytes)"; return false; }
    return true;
}

bool applyOpcodeTransform(std::string& bytes, const std::vector<CodeRange>& ranges, unsigned int multiplier, std::string& error)
{
    for (const CodeRange& range : ranges) {
        uint32_t pc = 0;
        while (pc < range.words) {
            const size_t opcodeOffset = range.offset + size_t(pc) * 4;
            const uint8_t encoded = static_cast<uint8_t>(bytes[opcodeOffset]);
            const uint8_t opcode = static_cast<uint8_t>((static_cast<unsigned int>(encoded) * multiplier) & 0xff);
            if (opcode >= LOP__COUNT) {
                error = "unknown opcode " + std::to_string(opcode) + " at code word " + std::to_string(pc);
                return false;
            }
            const uint32_t length = static_cast<uint32_t>(Luau::getOpLength(static_cast<LuauOpcode>(opcode)));
            if (length == 0 || length > range.words - pc) {
                error = "instruction AUX word is truncated at code word " + std::to_string(pc);
                return false;
            }
            bytes[opcodeOffset] = static_cast<char>(opcode);
            pc += length;
        }
    }
    return true;
}

}

bool isBytecode(std::string_view input)
{
    uint8_t version = 0;
    std::vector<CodeRange> ranges;
    std::string error;
    return parse(input, version, ranges, error);
}

bool opcodeVariants(std::string_view input, std::vector<std::string>& variants, std::string& error)
{
    uint8_t version = 0;
    std::vector<CodeRange> ranges;
    if (!parse(input, version, ranges, error)) return false;

    if (input.size() >= 5 && input.substr(0, 5) == std::string_view("\x1bLuau", 5))
        input.remove_prefix(5);
    variants.clear();
    for (unsigned int multiplier : {1u, 203u}) {
        std::string candidate(input);
        std::string transformError;
        if (!applyOpcodeTransform(candidate, ranges, multiplier, transformError)) {
            if (multiplier == 1u) continue;
            error = "Roblox opcode decoding failed: " + transformError;
            continue;
        }
        if (variants.empty() || variants.front() != candidate)
            variants.push_back(std::move(candidate));
    }
    if (variants.empty()) {
        if (error.empty()) error = "no supported opcode encoding was found";
        return false;
    }
    return true;
}

}
