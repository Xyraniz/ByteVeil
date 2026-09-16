//
// Created by xgladius on 8/7/22.
//
#include "../include/Handlers.h"

namespace Luau::Decompiler::AstGen::Handlers {
    AstExprCall* getCallAst(VirtualAstStack& virtualStack, unsigned int insn)  {
        auto function = Luau::Decompiler::orPlaceholder(virtualStack[LUAU_INSN_A(insn)]);
        bool isSelf = false;
        if (function->is<AstExprIndexName>()) {
            isSelf = function->as<AstExprIndexName>()->op == ':';
        }

        auto* argVec = new std::vector<AstExpr *>{};
        int argCount = LUAU_INSN_B(insn) == 0 ? virtualStack.getTop() : LUAU_INSN_B(insn) - 1;
        // virtualStack.getTop() can legitimately return -1 (register 0 unset).
        // The old code fed that straight into `LUAU_INSN_A(insn) + argCount`
        // as an unsigned comparison bound; -1 there wraps to ~4 billion and the
        // loop below would run effectively forever. Clamp to a small sane
        // range instead -- Luau registers fit in 8 bits, so a real call never
        // needs more than ~255 argument slots.
        if (argCount < 0)
            argCount = 0;
        if (argCount > 255)
            argCount = 255;
        unsigned int startReg = isSelf ? LUAU_INSN_A(insn) + 2 : LUAU_INSN_A(insn) + 1;
        unsigned int endReg = LUAU_INSN_A(insn) + static_cast<unsigned int>(argCount);
        for (unsigned int i = startReg; i <= endReg; i++) {
            auto reg = virtualStack[i];
            if (reg) {
                argVec->push_back(reg);
                if (LUAU_INSN_B(insn) != 0)
                    virtualStack.remove(i);
            }
        }

        AstArray<AstExpr *> args {argVec->data(), argVec->size()};
        virtualStack.remove(LUAU_INSN_A(insn));
        return new AstExprCall { Location(), function, args, isSelf, Location() };
    }

    AstExprIndexName* genAstExprName(VirtualAstStack& virtualStack, Proto* proto, unsigned int insn, unsigned int aux, char symbol) {
        auto name = AstName(Luau::Decompiler::getConstantName(proto, aux));
        return new AstExprIndexName{Location(), Luau::Decompiler::orPlaceholder(virtualStack[LUAU_INSN_B(insn)]),
                                                            name, Location(), Position(0, 0), symbol};
    }

    AstStatAssign* getSetGlobalAssignment(VirtualAstStack& virtualStack, Proto* proto, unsigned int insn, unsigned int aux) {
        auto* vars_arr = new std::vector<AstExpr *>;
        auto* values_arr = new std::vector<AstExpr *>;

        vars_arr->push_back(Luau::Decompiler::makeGlobal(Luau::Decompiler::getConstantName(proto, aux)));
        values_arr->push_back(Luau::Decompiler::orPlaceholder(virtualStack[LUAU_INSN_A(insn)]));

        AstArray<AstExpr*> vars {vars_arr->data(), vars_arr->size()};
        AstArray<AstExpr*> vals {values_arr->data(), values_arr->size()};

        return new AstStatAssign {Location(), vars, vals};
    }

    locVar genLocalStat(VirtualAstStack& virtualStack, std::unordered_map<int, AstLocal*>& locVars, unsigned int insn) {
        auto *vars_arr = new std::vector<AstLocal *>;
        auto name = new std::string(std::string("loc") + std::to_string(LUAU_INSN_B(insn)));
        AstLocal *loc;
        bool existed = false;
        if (locVars.find(LUAU_INSN_B(insn)) != locVars.end()) {
            loc = locVars.at(LUAU_INSN_B(insn));
            existed = true;
        } else {
            loc = new AstLocal{AstName(name->c_str()), Location(), nullptr, 1, 1, nullptr};
            locVars.insert(std::make_pair(LUAU_INSN_B(insn), loc));
        }
        vars_arr->push_back(loc);
        auto *values_arr = new std::vector<AstExpr *>;
        values_arr->push_back(Luau::Decompiler::orPlaceholder(virtualStack[LUAU_INSN_B(insn)]));
        auto stat = new AstStatLocal{Location(), {vars_arr->data(), vars_arr->size()},
                                     {values_arr->data(), values_arr->size()},
                                     std::make_optional<Location>(Location())};
        return {existed, stat, new AstExprLocal{Location(), loc, false}};
    }

    AstExprTable* genTableAst(VirtualAstStack& virtualStack, unsigned int insn, int index) {
        auto *items_arr = new std::vector<AstExprTable::Item>;
        int argCount = LUAU_INSN_C(insn) == 0 ? virtualStack.getTop() : LUAU_INSN_C(insn) - 1;
        // Same getTop() == -1 wraparound risk as getCallAst above: clamp before
        // it feeds an unsigned loop bound.
        if (argCount < 0)
            argCount = 0;
        if (argCount > 255)
            argCount = 255;
        unsigned int endReg = LUAU_INSN_A(insn) + static_cast<unsigned int>(argCount);
        for (unsigned int i = LUAU_INSN_B(insn); i <= endReg; i++) {
            items_arr->push_back(AstExprTable::Item{AstExprTable::Item::Kind::List, nullptr, Luau::Decompiler::orPlaceholder(virtualStack[i])});
        }
        return new AstExprTable{Location(), {items_arr->data(), items_arr->size()}};
    }

    AstStatAssign* genSetTable(VirtualAstStack& virtualStack, unsigned int insn) {
        auto* vars_arr = new std::vector<AstExpr *>;
        auto* values_arr = new std::vector<AstExpr *>;

        vars_arr->push_back(new AstExprIndexExpr{ Location(),
                                                  Luau::Decompiler::orPlaceholder(virtualStack[LUAU_INSN_B(insn)]),
                                                  Luau::Decompiler::orPlaceholder(virtualStack[LUAU_INSN_C(insn)])});
        values_arr->push_back(Luau::Decompiler::orPlaceholder(virtualStack[LUAU_INSN_A(insn)]));

        AstArray<AstExpr*> vars {vars_arr->data(), vars_arr->size()};
        AstArray<AstExpr*> vals {values_arr->data(), values_arr->size()};

        return new AstStatAssign {Location(), vars, vals};
    }
}