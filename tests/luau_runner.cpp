#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>

#include <Luau/Compiler.h>
#include <Luau/BytecodeBuilder.h>
#include <lua.h>
#include <lualib.h>

namespace {
struct IdentityEncoder final : Luau::BytecodeEncoder {
    uint8_t encodeOp(uint8_t opcode) override { return opcode; }
};
}

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::cerr << "usage: byteveil_luau_runner FILE\n";
        return 2;
    }
    std::ifstream input(argv[1], std::ios::binary);
    if (!input) {
        std::cerr << "cannot read " << argv[1] << '\n';
        return 1;
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();

    IdentityEncoder encoder;
    std::string bytecode = Luau::compile(buffer.str(), {}, {}, &encoder);
    if (bytecode.empty()) {
        std::cerr << "source compilation failed\n";
        return 1;
    }

    std::unique_ptr<lua_State, decltype(&lua_close)> state(luaL_newstate(), lua_close);
    if (!state) {
        std::cerr << "cannot create Luau state\n";
        return 1;
    }
    luaL_openlibs(state.get());
    if (luau_load(state.get(), argv[1], bytecode.data(), bytecode.size(), 0) != 0) {
        std::cerr << (lua_tostring(state.get(), -1) ? lua_tostring(state.get(), -1) : "bytecode loading failed") << '\n';
        return 1;
    }
    if (lua_pcall(state.get(), 0, LUA_MULTRET, 0) != 0) {
        std::cerr << (lua_tostring(state.get(), -1) ? lua_tostring(state.get(), -1) : "runtime failure") << '\n';
        return 1;
    }
    return 0;
}
