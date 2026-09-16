#pragma once
#include <string>

namespace ByteVeil::Lua51 {

bool isChunk(const std::string& data);
std::string inspect(const std::string& data, const std::string& mode, std::string& error);

}
