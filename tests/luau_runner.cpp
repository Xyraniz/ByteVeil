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

bool readFile(const char* path, std::string& contents)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    std::ostringstream buffer;
    buffer << input.rdbuf();
    contents = buffer.str();
    return true;
}
}

int main(int argc, char** argv)
{
    if (argc != 2 && argc != 5) {
        std::cerr << "usage: byteveil_luau_runner FILE\n"
                     "       byteveil_luau_runner --compile LEVEL SOURCE OUTPUT\n";
        return 2;
    }

    const bool compileOnly = argc == 5 && std::string(argv[1]) == "--compile";
    const char* sourcePath = compileOnly ? argv[3] : argv[1];
    std::string source;
    if (!readFile(sourcePath, source)) {
        std::cerr << "cannot read " << sourcePath << '\n';
        return 1;
    }

    IdentityEncoder encoder;
    Luau::CompileOptions options;
    if (compileOnly)
    {
        if (argv[2][0] < '0' || argv[2][0] > '2' || argv[2][1] != '\0') {
            std::cerr << "optimization level must be 0, 1, or 2\n";
            return 2;
        }
        options.optimizationLevel = argv[2][0] - '0';
        options.debugLevel = 2;
    }
    std::string bytecode = Luau::compile(source, options, {}, &encoder);
    if (bytecode.empty()) {
        std::cerr << "source compilation failed\n";
        return 1;
    }

    if (compileOnly)
    {
        std::ofstream output(argv[4], std::ios::binary);
        if (!output) {
            std::cerr << "cannot write " << argv[4] << '\n';
            return 1;
        }
        output.write(bytecode.data(), std::streamsize(bytecode.size()));
        if (!output) {
            std::cerr << "cannot finish writing " << argv[4] << '\n';
            return 1;
        }
        return 0;
    }

    std::unique_ptr<lua_State, decltype(&lua_close)> state(luaL_newstate(), lua_close);
    if (!state) {
        std::cerr << "cannot create Luau state\n";
        return 1;
    }
    luaL_openlibs(state.get());
    if (luau_load(state.get(), sourcePath, bytecode.data(), bytecode.size(), 0) != 0) {
        std::cerr << (lua_tostring(state.get(), -1) ? lua_tostring(state.get(), -1) : "bytecode loading failed") << '\n';
        return 1;
    }
    if (lua_pcall(state.get(), 0, LUA_MULTRET, 0) != 0) {
        std::cerr << (lua_tostring(state.get(), -1) ? lua_tostring(state.get(), -1) : "runtime failure") << '\n';
        return 1;
    }
    return 0;
}
