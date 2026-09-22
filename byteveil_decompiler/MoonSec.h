#pragma once

#include <cstddef>
#include <string>

namespace ByteVeil::MoonSec {

// Metadata for the encrypted, serialized Lua 5.1 prototype tree used by the
// MoonSec V3 loader.  `bytes` intentionally remain in the private adapter:
// callers can request a deterministic report without accidentally printing a
// multi-megabyte binary blob to a terminal.
struct Payload {
    std::string bytes;
    std::size_t sourceOffset = 0;
    unsigned int key = 0;
    std::string prototypeLayout;
    std::string constantTags;
    std::size_t functions = 0;
    std::size_t instructions = 0;
    std::size_t constants = 0;
    std::string checksum;
};

// Recognizes only the visible MoonSec V3 family marker, decodes candidate
// alphabet/nibble strings, and accepts a candidate only when its complete
// serialized prototype tree is structurally valid.  It does not invoke Lua,
// load bytecode, or execute a recovered payload.
bool extract(const std::string& source, Payload& payload, std::string& error);

// JSON report used by the CLI's explicit MoonSec inspection route.
std::string inspect(const std::string& source);

} // namespace ByteVeil::MoonSec
