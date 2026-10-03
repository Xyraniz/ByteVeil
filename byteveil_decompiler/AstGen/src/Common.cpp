//
// Created by xgladius on 8/6/22.
//
#include "../include/Common.h"

namespace Luau::Decompiler {
    AstExprGlobal* makeGlobal(const char* name) {
        return new AstExprGlobal {Location(), AstName(name)};
    }

    AstExprGlobal* genFunctionAst(StkId func) {
        auto cl = func->value.gc->cl;
        const char* name = cl.isC ? (cl.c.debugname ? getstr(cl.c.debugname) : "")
                                  : (cl.l.p->debugname ? getstr(cl.l.p->debugname) : "");
        return makeGlobal(name);
    }

    AstExpr* getConstantAst(TValue* val) {
        if (val == nullptr)
            return nullptr;
        switch (val->tt) {
            case LUA_TNIL:
                return new AstExprConstantNil{Location()};
            case LUA_TBOOLEAN:
                return new AstExprConstantBool { Location(), val->value.b != 0 };
            case LUA_TNUMBER:
                return new AstExprConstantNumber { Location(), val->value.n };
            case LUA_TSTRING: {
                AstArray<char> str{val->value.gc->ts.data, val->value.gc->ts.len};
                return new AstExprConstantString{ Location(), str, AstExprConstantString::QuoteStyle::QuotedSimple };
            }
            case LUA_TFUNCTION:
                return genFunctionAst(val);
        }
        return nullptr;
    }

    AstExpr* orPlaceholder(AstExpr* value) {
        if (value)
            return value;
        static char placeholder[] = "--[[ byteveil: unresolved register ]]";
        return new AstExprConstantString{Location(), AstArray<char>{placeholder, sizeof(placeholder) - 1},
            AstExprConstantString::QuoteStyle::QuotedSimple};
    }

    const char* getConstantName(Proto* proto, unsigned int idx) {
        if (!proto || idx >= static_cast<unsigned int>(proto->sizek))
            return "?byteveil_bad_const_index?";
        TValue* val = &proto->k[idx];
        if (val->tt != LUA_TSTRING || !val->value.gc)
            return "?byteveil_non_string_const?";
        return val->value.gc->ts.data;
    }
}
