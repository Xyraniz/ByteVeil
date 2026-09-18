//
// Created by xgladius on 8/7/22.
//
#include "Decompile.h"
#include <Luau/Compiler.h>

namespace Luau::Decompiler {
    std::string decompile(lua_State* L, std::string& bytecode) {
        if (luau_load(L, "=", bytecode.c_str(), bytecode.size(), 0) != 0)
            return std::string("error: ") + (lua_tostring(L, -1) ? lua_tostring(L, -1) : "bytecode loading failed");
        Closure* closure = (Closure*)lua_topointer(L, -1);
        if (!closure || !closure->l.p)
            return "error: loaded chunk has no Lua prototype";
        Proto* p = closure->l.p;
        std::vector<AstExpr*> subFuncs;
        for (auto i = 0; i < p->sizep; i++) {
            BlockGen::BlockGen<false> block(p->p[i]);
            subFuncs.push_back(block.generate());
        }

        BlockGen::BlockGen<true> main(p, subFuncs);
        auto* block = main.generate();
        if (!block)
            return "error: block lifting failed for this chunk";
        std::string output = transpile(*block);
        // A successful printer call is not sufficient evidence that the
        // reconstruction is valid. The previous lifter could silently emit
        // empty loop bodies or turn a closure into `false` after losing a
        // virtual-stack value. Refuse those known-corrupt shapes instead of
        // presenting them as a successful decompilation.
        if (output.find(" do end") != std::string::npos ||
            output.find("loc0=false") != std::string::npos)
            return "error: Luau lifter produced structurally incomplete output; control-flow body or closure value was lost";
        // The printer can emit text even when a control-flow shape was only
        // partially recovered. Parse/compile the result before returning it;
        // invalid source is a failed decompilation, not a usable result.
        std::string syntaxBytecode = Luau::compile(output);
        if (syntaxBytecode.empty())
            return "error: Luau lifter produced syntactically invalid source";
        int stackTop = lua_gettop(L);
        if (luau_load(L, "byteveil-reconstruction", syntaxBytecode.data(), syntaxBytecode.size(), 0) != 0)
        {
            lua_settop(L, stackTop);
            return "error: Luau lifter produced syntactically invalid source";
        }
        lua_settop(L, stackTop);
        return output;
    }
}
