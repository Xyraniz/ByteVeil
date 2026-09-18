//
// Created by xgladius on 8/7/22.
//
#include "Decompile.h"
#include "RegisterDecompile.h"
#include <Luau/Compiler.h>

namespace Luau::Decompiler {
    std::string decompile(lua_State* L, std::string& bytecode) {
        if (luau_load(L, "=", bytecode.c_str(), bytecode.size(), 0) != 0)
            return std::string("error: ") + (lua_tostring(L, -1) ? lua_tostring(L, -1) : "bytecode loading failed");
        Closure* closure = (Closure*)lua_topointer(L, -1);
        if (!closure || !closure->l.p)
            return "error: loaded chunk has no Lua prototype";
        Proto* p = closure->l.p;

        auto validSource = [&](const std::string& source) {
            std::string syntaxBytecode = Luau::compile(source);
            if (syntaxBytecode.empty())
                return false;
            int stackTop = lua_gettop(L);
            bool valid = luau_load(L, "byteveil-reconstruction", syntaxBytecode.data(), syntaxBytecode.size(), 0) == 0;
            lua_settop(L, stackTop);
            return valid;
        };
        auto registerFallback = [&]() {
            std::string error;
            std::string source = RegisterDecompile::render(p, error);
            if (source.empty())
                return std::string("error: register-state reconstruction failed: ") + (error.empty() ? "no output" : error);
            if (!validSource(source))
                return std::string("error: register-state reconstruction produced syntactically invalid source");
            return source;
        };

        if (RegisterDecompile::shouldPrefer(p))
            return registerFallback();

        std::vector<AstExpr*> subFuncs;
        for (auto i = 0; i < p->sizep; i++) {
            BlockGen::BlockGen<false> block(p->p[i]);
            subFuncs.push_back(block.generate());
        }

        BlockGen::BlockGen<true> main(p, subFuncs);
        auto* block = main.generate();
        if (!block)
            return registerFallback();
        std::string output = transpile(*block);
        // A successful printer call is not sufficient evidence that the
        // reconstruction is valid. The previous lifter could silently emit
        // empty loop bodies or turn a closure into `false` after losing a
        // virtual-stack value. Refuse those known-corrupt shapes instead of
        // presenting them as a successful decompilation.
        if (output.find(" do end") != std::string::npos ||
            output.find("loc0=false") != std::string::npos)
            return registerFallback();
        // The printer can emit text even when a control-flow shape was only
        // partially recovered. Parse/compile the result before returning it;
        // invalid source is a failed decompilation, not a usable result.
        if (!validSource(output))
            return registerFallback();
        return output;
    }
}
