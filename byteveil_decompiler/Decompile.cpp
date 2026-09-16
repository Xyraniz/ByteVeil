//
// Created by xgladius on 8/7/22.
//
#include "Decompile.h"

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
        return transpile(*block);
    }
}
