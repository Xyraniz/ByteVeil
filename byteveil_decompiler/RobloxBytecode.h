#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace Luau::Decompiler::RobloxBytecode {

// Validate Luau's serialized bytecode envelope without allocating VM objects.
// Roblox dumps in the supplied corpus use version 13/type-info 3 and omit the
// optional five-byte "\x1bLuau" file prefix.
bool isBytecode(std::string_view input);

// Return identity-opcode and Roblox opcode-decoded variants, each after a
// bounds-checked walk of the serialized prototype tree. AUX words are copied
// verbatim; only actual instruction opcode bytes are normalized.
bool opcodeVariants(std::string_view input, std::vector<std::string>& variants, std::string& error);

}
