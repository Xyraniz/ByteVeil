//
// Created by xgladius on 8/6/22.
//
#pragma once
#include "Luau/Ast.h"
#include "../../Common.h"

namespace Luau::Decompiler {
    AstExprGlobal* makeGlobal(const char* name);

    AstExprGlobal* genFunctionAst(StkId func);

    AstExpr* getConstantAst(TValue* val);

    // An unhandled/unlifted opcode can leave a virtual register unset (nullptr).
    // Any Handlers function that must embed that register's value as a
    // non-optional AST child (a call target, a table key, an operand, ...)
    // should route it through this instead of dereferencing it directly, so a
    // gap in opcode coverage degrades to a visible placeholder comment in the
    // output rather than crashing the whole decompile.
    AstExpr* orPlaceholder(AstExpr* value);

    // GETGLOBAL/SETGLOBAL/GETTABLEKS/NAMECALL all read a constant-table index
    // (the aux word after the instruction) and assume it is a string constant.
    // If the pc stream ever desyncs from real instruction boundaries (e.g. an
    // opcode this lifter's getOpLength() table doesn't know the length of),
    // that index can point at a non-string constant, and blindly dereferencing
    // value.gc->ts.data reads a GCObject union as the wrong variant -> garbage
    // pointer -> segfault. This checks the constant's tag first.
    const char* getConstantName(Proto* proto, unsigned int idx);
}