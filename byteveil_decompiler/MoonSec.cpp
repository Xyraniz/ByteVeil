#include "MoonSec.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string_view>
#include <utility>
#include <vector>

namespace ByteVeil::MoonSec {
namespace {

constexpr std::size_t kMaxPayloadBytes = 64u * 1024u * 1024u;
constexpr std::size_t kMaxPrototypeDepth = 128;
constexpr std::size_t kMaxFunctions = 100000;
constexpr std::size_t kMaxInstructions = 4000000;
constexpr std::size_t kMaxConstants = 4000000;

enum class Step : unsigned char { Constants, Instructions, Functions, Parameters };
enum class ConstantKind : unsigned char { Unknown, Boolean, Number, String };

struct Literal {
    std::string value;
    std::size_t offset = 0;
};

struct Metrics {
    std::size_t functions = 0;
    std::size_t instructions = 0;
    std::size_t constants = 0;
    std::size_t strings = 0;
    std::size_t nonNilConstants = 0;
};

bool hasMoonSecMarker(const std::string& source)
{
    return source.find("MoonSec V3") != std::string::npos ||
           source.find("MoonSecV3") != std::string::npos ||
           source.find("moonsec v3") != std::string::npos;
}

bool longBracketOpen(const std::string& source, std::size_t pos, std::size_t& equals, std::size_t& afterOpen)
{
    if (pos >= source.size() || source[pos] != '[')
        return false;
    std::size_t cursor = pos + 1;
    while (cursor < source.size() && source[cursor] == '=')
        ++cursor;
    if (cursor >= source.size() || source[cursor] != '[')
        return false;
    equals = cursor - (pos + 1);
    afterOpen = cursor + 1;
    return true;
}

std::size_t skipLongBracket(const std::string& source, std::size_t pos)
{
    std::size_t equals = 0;
    std::size_t afterOpen = 0;
    if (!longBracketOpen(source, pos, equals, afterOpen))
        return pos;

    const std::string close = "]" + std::string(equals, '=') + "]";
    const std::size_t end = source.find(close, afterOpen);
    return end == std::string::npos ? source.size() : end + close.size();
}

bool readQuotedLiteral(const std::string& source, std::size_t& cursor, Literal& literal)
{
    const char quote = source[cursor];
    literal.offset = cursor;
    ++cursor;
    std::string value;
    value.reserve(64);

    while (cursor < source.size()) {
        unsigned char current = static_cast<unsigned char>(source[cursor++]);
        if (current == static_cast<unsigned char>(quote)) {
            literal.value = std::move(value);
            return true;
        }
        if (current != '\\') {
            value.push_back(static_cast<char>(current));
            continue;
        }
        if (cursor >= source.size())
            return false;

        const char escaped = source[cursor++];
        switch (escaped) {
        case 'a': value.push_back('\a'); break;
        case 'b': value.push_back('\b'); break;
        case 'f': value.push_back('\f'); break;
        case 'n': value.push_back('\n'); break;
        case 'r': value.push_back('\r'); break;
        case 't': value.push_back('\t'); break;
        case 'v': value.push_back('\v'); break;
        case '\n': value.push_back('\n'); break;
        case '\r':
            if (cursor < source.size() && source[cursor] == '\n')
                ++cursor;
            value.push_back('\n');
            break;
        case 'z':
            while (cursor < source.size() && (source[cursor] == ' ' || source[cursor] == '\t' || source[cursor] == '\r' || source[cursor] == '\n' || source[cursor] == '\f' || source[cursor] == '\v'))
                ++cursor;
            break;
        case 'x': {
            auto hex = [](char value) -> int {
                if (value >= '0' && value <= '9') return value - '0';
                if (value >= 'a' && value <= 'f') return value - 'a' + 10;
                if (value >= 'A' && value <= 'F') return value - 'A' + 10;
                return -1;
            };
            if (cursor + 1 > source.size())
                return false;
            const int left = hex(source[cursor++]);
            const int right = cursor < source.size() ? hex(source[cursor++]) : -1;
            if (left < 0 || right < 0)
                return false;
            value.push_back(static_cast<char>((left << 4) | right));
            break;
        }
        default:
            if (escaped >= '0' && escaped <= '9') {
                unsigned int decimal = static_cast<unsigned int>(escaped - '0');
                for (int digits = 1; digits < 3 && cursor < source.size() && source[cursor] >= '0' && source[cursor] <= '9'; ++digits)
                    decimal = decimal * 10u + static_cast<unsigned int>(source[cursor++] - '0');
                if (decimal > 255)
                    return false;
                value.push_back(static_cast<char>(decimal));
            } else {
                value.push_back(escaped);
            }
            break;
        }
    }
    return false;
}

std::vector<Literal> stringsInSource(const std::string& source)
{
    std::vector<Literal> result;
    for (std::size_t cursor = 0; cursor < source.size();) {
        if (source[cursor] == '-' && cursor + 1 < source.size() && source[cursor + 1] == '-') {
            cursor += 2;
            const std::size_t longEnd = skipLongBracket(source, cursor);
            if (longEnd != cursor) {
                cursor = longEnd;
            } else {
                const std::size_t lineEnd = source.find_first_of("\r\n", cursor);
                cursor = lineEnd == std::string::npos ? source.size() : lineEnd + 1;
            }
            continue;
        }
        if (source[cursor] == '[') {
            const std::size_t longEnd = skipLongBracket(source, cursor);
            if (longEnd != cursor) {
                cursor = longEnd;
                continue;
            }
        }
        if (source[cursor] == '\'' || source[cursor] == '"') {
            Literal literal;
            if (readQuotedLiteral(source, cursor, literal))
                result.push_back(std::move(literal));
            else
                break;
            continue;
        }
        ++cursor;
    }
    return result;
}

bool alphabetCandidate(const std::string& text)
{
    if (text.size() < 18 || (text.size() - 16) % 2 != 0 || text.size() > kMaxPayloadBytes * 2 + 16)
        return false;
    std::array<bool, 256> seen{};
    for (std::size_t index = 0; index < 16; ++index) {
        const unsigned char byte = static_cast<unsigned char>(text[index]);
        if (seen[byte] || byte == '\r' || byte == '\n')
            return false;
        seen[byte] = true;
    }
    return true;
}

std::string decodeAlphabetPayload(const std::string& encoded, unsigned int key)
{
    std::array<int, 256> alphabet{};
    alphabet.fill(-1);
    for (std::size_t index = 0; index < 16; ++index)
        alphabet[static_cast<unsigned char>(encoded[index])] = static_cast<int>(index);

    std::string decoded;
    decoded.reserve((encoded.size() - 16) / 2);
    unsigned int rollingKey = key;
    for (std::size_t index = 16; index < encoded.size(); index += 2) {
        const int left = alphabet[static_cast<unsigned char>(encoded[index])];
        const int right = index + 1 < encoded.size() ? alphabet[static_cast<unsigned char>(encoded[index + 1])] : -1;
        const unsigned int hi = left < 0 ? 0u : static_cast<unsigned int>(left);
        const unsigned int lo = right < 0 ? 0u : static_cast<unsigned int>(right);
        decoded.push_back(static_cast<char>((hi * 16u + lo + rollingKey) & 0xffu));
        rollingKey = (rollingKey + key) & 0xffu;
    }
    return decoded;
}

class SerializedReader {
public:
    SerializedReader(std::string_view data, const std::array<Step, 4>& order, const std::array<ConstantKind, 4>& tags)
        : data(data), order(order), tags(tags) {}

    bool parse(Metrics& output)
    {
        metrics = {};
        if (!parseFunction(0))
            return false;
        if (position != data.size() || metrics.functions == 0 || metrics.instructions == 0)
            return false;
        output = metrics;
        return true;
    }

private:
    bool need(std::size_t count) const { return count <= data.size() - position; }

    bool readByte(uint8_t& value)
    {
        if (!need(1)) return false;
        value = static_cast<uint8_t>(data[position++]);
        return true;
    }

    bool readU16(uint16_t& value)
    {
        if (!need(2)) return false;
        value = static_cast<uint16_t>(static_cast<uint8_t>(data[position])) |
                static_cast<uint16_t>(static_cast<uint8_t>(data[position + 1]) << 8);
        position += 2;
        return true;
    }

    bool readU32(uint32_t& value)
    {
        if (!need(4)) return false;
        value = static_cast<uint32_t>(static_cast<uint8_t>(data[position])) |
                (static_cast<uint32_t>(static_cast<uint8_t>(data[position + 1])) << 8) |
                (static_cast<uint32_t>(static_cast<uint8_t>(data[position + 2])) << 16) |
                (static_cast<uint32_t>(static_cast<uint8_t>(data[position + 3])) << 24);
        position += 4;
        return true;
    }

    bool skip(std::size_t count)
    {
        if (!need(count)) return false;
        position += count;
        return true;
    }

    bool parseConstants()
    {
        uint32_t count = 0;
        if (!readU32(count) || count > kMaxConstants - metrics.constants || count > data.size() - position)
            return false;
        metrics.constants += count;
        for (uint32_t index = 0; index < count; ++index) {
            uint8_t tag = 0;
            if (!readByte(tag)) return false;
            // MoonSec's serializer treats an unrecognized type flag as nil
            // and consumes no value bytes.  Do not fold arbitrary flags with
            // `& 3`: that would turn a malformed flag such as 4 into a valid
            // Boolean tag and could desynchronize validation.
            const ConstantKind kind = tag < tags.size() ? tags[tag] : ConstantKind::Unknown;
            switch (kind) {
            case ConstantKind::Unknown:
                break;
            case ConstantKind::Boolean:
                if (!skip(1)) return false;
                ++metrics.nonNilConstants;
                break;
            case ConstantKind::Number:
                if (!skip(8)) return false;
                ++metrics.nonNilConstants;
                break;
            case ConstantKind::String: {
                uint32_t length = 0;
                if (!readU32(length) || length > data.size() - position || length > kMaxPayloadBytes)
                    return false;
                if (!skip(length)) return false;
                ++metrics.nonNilConstants;
                ++metrics.strings;
                break;
            }
            }
        }
        return true;
    }

    bool parseInstructions()
    {
        uint32_t count = 0;
        if (!readU32(count) || count > kMaxInstructions - metrics.instructions || count > data.size() - position)
            return false;
        for (uint32_t index = 0; index < count; ++index) {
            uint8_t descriptor = 0;
            if (!readByte(descriptor)) return false;
            if ((descriptor & 1u) != 0)
                continue;
            uint16_t ignored = 0;
            if (!readU16(ignored) || !readU16(ignored)) return false; // opcode and A
            switch ((descriptor >> 1u) & 3u) {
            case 0:
                if (!readU16(ignored) || !readU16(ignored)) return false;
                break;
            case 1:
            case 2: {
                uint32_t ignored32 = 0;
                if (!readU32(ignored32)) return false;
                break;
            }
            case 3: {
                uint32_t ignored32 = 0;
                if (!readU32(ignored32) || !readU16(ignored)) return false;
                break;
            }
            }
            ++metrics.instructions;
        }
        return true;
    }

    bool parseFunctions(std::size_t depth)
    {
        uint32_t count = 0;
        if (!readU32(count) || count > kMaxFunctions - metrics.functions || count > data.size() / 5u)
            return false;
        for (uint32_t index = 0; index < count; ++index) {
            if (!parseFunction(depth + 1)) return false;
        }
        return true;
    }

    bool parseFunction(std::size_t depth)
    {
        if (depth > kMaxPrototypeDepth || metrics.functions >= kMaxFunctions)
            return false;
        ++metrics.functions;
        for (Step step : order) {
            switch (step) {
            case Step::Constants:
                if (!parseConstants()) return false;
                break;
            case Step::Instructions:
                if (!parseInstructions()) return false;
                break;
            case Step::Functions:
                if (!parseFunctions(depth)) return false;
                break;
            case Step::Parameters: {
                uint8_t ignored = 0;
                if (!readByte(ignored)) return false;
                break;
            }
            }
        }
        return true;
    }

    std::string_view data;
    const std::array<Step, 4>& order;
    const std::array<ConstantKind, 4>& tags;
    std::size_t position = 0;
    Metrics metrics;
};

std::string layoutName(const std::array<Step, 4>& layout)
{
    auto name = [](Step step) {
        switch (step) {
        case Step::Constants: return "constants";
        case Step::Instructions: return "instructions";
        case Step::Functions: return "functions";
        case Step::Parameters: return "parameters";
        }
        return "unknown";
    };
    std::ostringstream result;
    for (std::size_t index = 0; index < layout.size(); ++index) {
        if (index) result << ',';
        result << name(layout[index]);
    }
    return result.str();
}

std::string tagName(const std::array<ConstantKind, 4>& tags)
{
    auto name = [](ConstantKind kind) {
        switch (kind) {
        case ConstantKind::Unknown: return "nil";
        case ConstantKind::Boolean: return "boolean";
        case ConstantKind::Number: return "number";
        case ConstantKind::String: return "string";
        }
        return "unknown";
    };
    std::ostringstream result;
    for (std::size_t index = 0; index < tags.size(); ++index) {
        if (index) result << ',';
        result << index << ':' << name(tags[index]);
    }
    return result.str();
}

std::string checksum(const std::string& bytes)
{
    uint64_t value = 14695981039346656037ull;
    for (unsigned char byte : bytes) {
        value ^= byte;
        value *= 1099511628211ull;
    }
    std::ostringstream result;
    result << "fnv1a64:" << std::hex << std::setfill('0') << std::setw(16) << value;
    return result.str();
}

std::string jsonEscape(const std::string& text)
{
    std::ostringstream result;
    for (unsigned char byte : text) {
        if (byte == '"' || byte == '\\') result << '\\' << static_cast<char>(byte);
        else if (byte == '\n') result << "\\n";
        else if (byte == '\r') result << "\\r";
        else if (byte == '\t') result << "\\t";
        else if (byte < 0x20) result << "\\u00" << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned int>(byte) << std::dec;
        else result << static_cast<char>(byte);
    }
    return result.str();
}

bool probe(const std::string& bytes, Payload& payload)
{
    std::array<Step, 4> layout = {Step::Constants, Step::Instructions, Step::Functions, Step::Parameters};
    int bestScore = std::numeric_limits<int>::min();
    Metrics bestMetrics;
    std::array<Step, 4> bestLayout{};
    std::array<ConstantKind, 4> bestTags{};

    do {
        for (unsigned int booleanTag = 0; booleanTag < 4; ++booleanTag) {
            for (unsigned int numberTag = 0; numberTag < 4; ++numberTag) {
                if (numberTag == booleanTag) continue;
                for (unsigned int stringTag = 0; stringTag < 4; ++stringTag) {
                    if (stringTag == booleanTag || stringTag == numberTag) continue;
                    std::array<ConstantKind, 4> tags{};
                    tags.fill(ConstantKind::Unknown);
                    tags[booleanTag] = ConstantKind::Boolean;
                    tags[numberTag] = ConstantKind::Number;
                    tags[stringTag] = ConstantKind::String;
                    Metrics metrics;
                    SerializedReader reader(bytes, layout, tags);
                    if (!reader.parse(metrics)) continue;

                    const int score = static_cast<int>(metrics.instructions > 1000000 ? 1000000 : metrics.instructions) +
                                      static_cast<int>(metrics.functions > 100000 ? 100000 : metrics.functions) * 3 +
                                      static_cast<int>(metrics.strings > 100000 ? 100000 : metrics.strings) * 4 +
                                      static_cast<int>(metrics.nonNilConstants > 100000 ? 100000 : metrics.nonNilConstants);
                    if (score > bestScore) {
                        bestScore = score;
                        bestMetrics = metrics;
                        bestLayout = layout;
                        bestTags = tags;
                    }
                }
            }
        }
    } while (std::next_permutation(layout.begin(), layout.end(), [](Step left, Step right) {
        return static_cast<unsigned char>(left) < static_cast<unsigned char>(right);
    }));

    if (bestScore == std::numeric_limits<int>::min())
        return false;
    payload.prototypeLayout = layoutName(bestLayout);
    payload.constantTags = tagName(bestTags);
    payload.functions = bestMetrics.functions;
    payload.instructions = bestMetrics.instructions;
    payload.constants = bestMetrics.constants;
    return true;
}

} // namespace

bool extract(const std::string& source, Payload& payload, std::string& error)
{
    payload = {};
    error.clear();
    if (!hasMoonSecMarker(source)) {
        error = "MoonSec V3 marker is not visible";
        return false;
    }

    const std::vector<Literal> literals = stringsInSource(source);
    int bestScore = std::numeric_limits<int>::min();
    Payload best;
    for (const Literal& literal : literals) {
        if (!alphabetCandidate(literal.value))
            continue;
        for (unsigned int key = 0; key <= 255; ++key) {
            Payload candidate;
            candidate.bytes = decodeAlphabetPayload(literal.value, key);
            if (!probe(candidate.bytes, candidate))
                continue;
            candidate.sourceOffset = literal.offset;
            candidate.key = key;
            candidate.checksum = checksum(candidate.bytes);
            const int score = static_cast<int>(candidate.instructions > 1000000 ? 1000000 : candidate.instructions) +
                              static_cast<int>(candidate.functions > 100000 ? 100000 : candidate.functions) * 3 +
                              static_cast<int>(candidate.constants > 100000 ? 100000 : candidate.constants);
            if (score > bestScore || (score == bestScore && candidate.sourceOffset < best.sourceOffset)) {
                bestScore = score;
                best = std::move(candidate);
            }
        }
    }

    if (bestScore == std::numeric_limits<int>::min()) {
        error = "no alphabet-encoded MoonSec V3 serialized prototype passed structural validation";
        return false;
    }
    payload = std::move(best);
    return true;
}

std::string inspect(const std::string& source)
{
    Payload payload;
    std::string error;
    const bool ok = extract(source, payload, error);
    std::ostringstream result;
    result << "{\"format\":\"moonsec-v3-analysis\",\"executed\":false,\"recognized\":"
           << (hasMoonSecMarker(source) ? "true" : "false") << ",\"status\":\""
           << (ok ? "serialized-bytecode-extracted" : "serialized-bytecode-not-recovered") << "\"";
    if (ok) {
        result << ",\"payload\":{\"kind\":\"moonsec-v3-serialized-bytecode\",\"bytes\":" << payload.bytes.size()
               << ",\"source_offset\":" << payload.sourceOffset
               << ",\"decoder_key\":" << payload.key
               << ",\"prototype_layout\":\"" << jsonEscape(payload.prototypeLayout)
               << "\",\"constant_tags\":\"" << jsonEscape(payload.constantTags)
               << "\",\"functions\":" << payload.functions
               << ",\"instructions\":" << payload.instructions
               << ",\"constants\":" << payload.constants
               << ",\"checksum\":\"" << payload.checksum << "\"}";
    } else {
        result << ",\"reason\":\"" << jsonEscape(error) << "\"";
    }
    result << ",\"note\":\"The adapter lexes and validates data only; it never invokes Lua, loads a recovered chunk, or executes the protected payload. Opcode virtualization is reported as a separate stage.\"}\n";
    return result.str();
}

} // namespace ByteVeil::MoonSec
