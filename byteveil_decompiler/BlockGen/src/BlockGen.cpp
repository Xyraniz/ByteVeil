//
// Created by xgladius on 8/6/22.
//
#include "../include/BlockGen.h"

#include <utility>

namespace Luau::Decompiler::BlockGen {
    template<> BlockGen<false>::BlockGen(Proto* p) : proto(p),
                                            blockInfo(Lifter::liftBodyInfo(p)),
                                            functionArgs(new std::vector<AstLocal*>{}) {}

    template<> BlockGen<true>::BlockGen(Proto *p,
            std::vector<AstExpr*> subFuncs) : proto(p),
                                                    blockInfo(Lifter::liftBodyInfo(p)),
                                                    functionArgs(new std::vector<AstLocal*>{}),
                                                    subFuncs(std::move(subFuncs)) {}

    template<> void BlockGen<false>::generateFunctionArgs() {
        if (proto->numparams) {
            functionArgs->reserve(proto->numparams);
            for (auto i = 0; i < proto->numparams; i++) {
                auto name = new std::string(std::string("arg") + std::to_string(i));
                auto local = new AstLocal{AstName((name->c_str())),
                                           Location(), nullptr, 1, 1, nullptr};
                functionArgs->push_back(local);
                virtualStack.set(i, new AstExprLocal { Location(), local, false });
            }
        }
    }

    template<> void BlockGen<true>::generateFunctionArgs() {}

    template<bool isMain> AstStatBlock* BlockGen<isMain>::handleAllInstructions() {
        bodyHandler.makeMain();
        for (auto pc = 0; pc < proto->sizecode;) {
            auto *insn = &proto->code[pc];
            handleInstruction(insn, pc);
            pc += Luau::Decompiler::BlockGen::Lifter::getOpLength(LuauOpcode(*insn));
        }
        auto ret = bodyHandler.get()->template as<AstStatBlock>();
        if (!ret) {
            // On control flow this BodyHandler's if/while/for stack machine
            // doesn't cleanly model (seen on a heavily branchy VM-dispatcher
            // body), bodyHandler.get() can end up returning something that
            // isn't actually a block, and as<AstStatBlock>() then returns
            // nullptr. That null used to flow straight into an
            // AstExprFunction's body field and crash the Luau printer the
            // moment it tried to print that function. An empty-but-valid
            // block with a visible marker is an honest degradation instead:
            // this function's body just couldn't be structurally recovered.
            static char placeholder[] = "--[[ byteveil: body not structurally recovered ]]";
            auto* stats = new std::vector<AstStat*>{
                new AstStatExpr{Location(), new AstExprConstantString{Location(),
                    AstArray<char>{placeholder, sizeof(placeholder) - 1}}}};
            ret = new AstStatBlock{Location(), AstArray<AstStat*>{stats->data(), stats->size()}};
        }
        return ret;
    }

    template<> AstStatBlock* BlockGen<true>::generate() {
        return handleAllInstructions();
    }

    template<> AstExpr* BlockGen<false>::generate() {
        // Non-main BlockGen instances previously left `subFuncs` empty, so any
        // NEWCLOSURE inside a nested function (a closure defined inside another
        // closure) indexed an empty vector -> UB / segfault. Build this proto's
        // own children first, the same way Decompile.cpp does for the top-level
        // proto, so every level of nesting gets its children lifted before its
        // own instructions are handled.
        subFuncs.reserve(proto->sizep);
        for (auto i = 0; i < proto->sizep; i++) {
            BlockGen<false> child(proto->p[i]);
            subFuncs.push_back(child.generate());
        }
        generateFunctionArgs();
        AstStatBlock* block = handleAllInstructions();
        auto name = AstName(proto->debugname ? proto->debugname->data : "");
        auto func = new AstExprFunction{AstExprFunction {Location(), AstArray<AstGenericType>{}, AstArray<AstGenericTypePack>{},
                                                         new AstLocal(name, Location(), nullptr, 1, 1, nullptr),
                                                         AstArray<AstLocal*>{functionArgs->data(), functionArgs->size()},
                                                         proto->is_vararg ? std::make_optional(Location()) : std::nullopt, block, 1, AstName(name), {}}};
        if (!proto->debugname) {
            return new AstExprGroup {Location(), func};
        }
        return func;
    }

    template<bool isMain> void BlockGen<isMain>::handleInstruction(unsigned int *insn, int pc) {
        auto materialize = [&](int reg) {
            auto local = Luau::Decompiler::AstGen::Handlers::genRegisterStat(
                locVars, reg, virtualStack[reg]);
            if (!local.existed)
                bodyHandler.addStat(local.stat);
            virtualStack.set(reg, local.expr);
        };
        if (blockInfo[pc] == Lifter::BlockType::END) {
            if (bodyHandler.getType() == BodyType::IF) {
                bodyHandler.addStat(bodyHandler.get()->template as<AstStatIf>());
            }
        } else if (blockInfo[pc] == Lifter::BlockType::WHILESTART) {
            bodyHandler.makeWhile();
        }
        switch (LUAU_INSN_OP(*insn)) {
            case LOP_LOADK: {
                unsigned int kIdx = LUAU_INSN_D(*insn);
                // Same "trust the index" issue as the string-constant sites fixed
                // above: proto->k[D] with no bounds check against proto->sizek.
                if (kIdx < static_cast<unsigned int>(proto->sizek))
                    virtualStack.set(LUAU_INSN_A(*insn), getConstantAst(&proto->k[kIdx]));
                materialize(LUAU_INSN_A(*insn));
                break;
            }
            case LOP_LOADNIL: {
                virtualStack.set(LUAU_INSN_A(*insn), new AstExprConstantNil{Location()});
                materialize(LUAU_INSN_A(*insn));
                break;
            }
            case LOP_LOADN: {
                virtualStack.set(LUAU_INSN_A(*insn), new AstExprConstantNumber{Location(), double(LUAU_INSN_D(*insn))});
                materialize(LUAU_INSN_A(*insn));
                break;
            }
            case LOP_LOADB: {
                virtualStack.set(LUAU_INSN_A(*insn), new AstExprConstantBool { Location(),
                                                                               LUAU_INSN_B(*insn) != 0 });
                materialize(LUAU_INSN_A(*insn));
                break;
            }
            case LOP_NOT: {
                virtualStack.set(LUAU_INSN_A(*insn), new AstExprUnary{Location(), AstExprUnary::Op::Not,
                                                                       Luau::Decompiler::orPlaceholder(virtualStack[LUAU_INSN_B(*insn)])});
                break;
            }
            case LOP_MINUS: {
                virtualStack.set(LUAU_INSN_A(*insn), new AstExprUnary{Location(), AstExprUnary::Op::Minus,
                                                                       Luau::Decompiler::orPlaceholder(virtualStack[LUAU_INSN_B(*insn)])});
                break;
            }
            case LOP_LENGTH: {
                virtualStack.set(LUAU_INSN_A(*insn), new AstExprUnary{Location(), AstExprUnary::Op::Len,
                                                                       Luau::Decompiler::orPlaceholder(virtualStack[LUAU_INSN_B(*insn)])});
                break;
            }
            case LOP_CONCAT: {
                // B..C inclusive, both 8-bit fields so already bounded to 0-255.
                unsigned int from = LUAU_INSN_B(*insn), to = LUAU_INSN_C(*insn);
                AstExpr* acc = Luau::Decompiler::orPlaceholder(virtualStack[from]);
                for (unsigned int i = from + 1; i <= to; i++) {
                    acc = new AstExprBinary{Location(), AstExprBinary::Op::Concat, acc,
                                            Luau::Decompiler::orPlaceholder(virtualStack[i])};
                }
                virtualStack.set(LUAU_INSN_A(*insn), acc);
                break;
            }
            case LOP_CALL: {
                auto callExpr = Luau::Decompiler::AstGen::Handlers::getCallAst(virtualStack, *insn);

                // C encodes result-count + 1. The previous <= C loop wrote
                // beyond the result range and polluted subsequent expressions.
                if (LUAU_INSN_C(*insn) > 1) {
                    for (auto i = 0u; i < LUAU_INSN_C(*insn) - 1; i++) {
                        virtualStack.set(LUAU_INSN_A(*insn) + i, callExpr);
                    }
                } else {
                    bodyHandler.addStat(new AstStatExpr {AstStatExpr{Location(), callExpr} });
                }
                break;
            }
            case LOP_SETGLOBAL: {
                auto stat = Luau::Decompiler::AstGen::Handlers::getSetGlobalAssignment(virtualStack, proto, *insn, *(insn + 1));
                bodyHandler.addStat(stat);
                break;
            }
            case LOP_GETGLOBAL: {
                virtualStack.set(LUAU_INSN_A(*insn), makeGlobal(Luau::Decompiler::getConstantName(proto, *(insn + 1))));
                materialize(LUAU_INSN_A(*insn));
                break;
            }
            case LOP_GETUPVAL: {
                // Debug names are optional in Luau bytecode; keep the read
                // visible with a stable synthetic name instead of dropping it.
                auto name = new std::string("upvalue_" + std::to_string(LUAU_INSN_B(*insn)));
                virtualStack.set(LUAU_INSN_A(*insn), makeGlobal(name->c_str()));
                materialize(LUAU_INSN_A(*insn));
                break;
            }
            case LOP_GETIMPORT: {
                // aux packs up to 3 path components as 10-bit constant-table
                // indices (see BytecodeBuilder::getImportId): top 2 bits are the
                // component count, then 10 bits each for id0/id1/id2. This is a
                // dotted global chain like "string.byte" or "Vector3.new", not a
                // single name, so build the AstExprIndexName chain directly from
                // the constant indices rather than through any register.
                uint32_t aux = *(insn + 1);
                unsigned int count = aux >> 30;
                unsigned int id0 = (aux >> 20) & 1023, id1 = (aux >> 10) & 1023, id2 = aux & 1023;
                AstExpr* chain = makeGlobal(Luau::Decompiler::getConstantName(proto, id0));
                if (count >= 2)
                    chain = new AstExprIndexName{Location(), chain, AstName(Luau::Decompiler::getConstantName(proto, id1)), Location(), Position(0, 0), '.'};
                if (count >= 3)
                    chain = new AstExprIndexName{Location(), chain, AstName(Luau::Decompiler::getConstantName(proto, id2)), Location(), Position(0, 0), '.'};
                virtualStack.set(LUAU_INSN_A(*insn), chain);
                break;
            }
            case LOP_GETTABLEKS: {
                virtualStack.set(LUAU_INSN_A(*insn), Luau::Decompiler::AstGen::Handlers::genAstExprName(virtualStack, proto, *insn, *(insn + 1), '.'));
                break;
            }
            case LOP_NAMECALL: {
                virtualStack.set(LUAU_INSN_A(*insn), Luau::Decompiler::AstGen::Handlers::genAstExprName(virtualStack, proto, *insn, *(insn + 1), ':'));
                break;
            }
            case LOP_NEWCLOSURE: {
                // Defensive bounds check: subFuncs is now populated recursively at
                // construction time (see the BlockGen<false>(Proto*) constructor), but
                // guard against any future desync between proto->sizep and subFuncs
                // instead of an out-of-bounds operator[] crashing the whole run.
                unsigned int childIdx = LUAU_INSN_D(*insn);
                if (childIdx < subFuncs.size()) {
                    virtualStack.set(LUAU_INSN_A(*insn), subFuncs[childIdx]);
                } else {
                    static char placeholder[] = "--[[ byteveil: unresolved nested closure ]]";
                    virtualStack.set(LUAU_INSN_A(*insn),
                        new AstExprConstantString{Location(), AstArray<char>{placeholder, sizeof(placeholder) - 1}});
                }
                materialize(LUAU_INSN_A(*insn));
                break;
            }
            case LOP_DUPCLOSURE: {
                // DUPCLOSURE references its target Proto indirectly: D indexes the
                // constant table, and that constant holds a template Closure whose
                // ->l.p is the actual Proto (used for closures with no upvalues, so
                // the compiler can share/intern them, unlike NEWCLOSURE which
                // indexes proto->p directly). Find that Proto's position in
                // proto->p to reuse the already-generated subFuncs entry.
                unsigned int kIdx = LUAU_INSN_D(*insn);
                AstExpr* found = nullptr;
                if (kIdx < static_cast<unsigned int>(proto->sizek)) {
                    TValue& kv = proto->k[kIdx];
                    if (kv.tt == LUA_TFUNCTION && kv.value.gc) {
                        Proto* target = kv.value.gc->cl.l.p;
                        for (int i = 0; i < proto->sizep && i < static_cast<int>(subFuncs.size()); i++) {
                            if (proto->p[i] == target) { found = subFuncs[i]; break; }
                        }
                    }
                }
                virtualStack.set(LUAU_INSN_A(*insn), Luau::Decompiler::orPlaceholder(found));
                break;
            }
            case LOP_NEWTABLE: {
                auto *items_arr = new std::vector<AstExprTable::Item>;
                virtualStack.set(LUAU_INSN_A(*insn),
                                 new AstExprTable{Location(), {items_arr->data(), items_arr->size()}});
                break;
            }
            case LOP_SETLIST: {
                virtualStack.set(LUAU_INSN_A(*insn), Luau::Decompiler::AstGen::Handlers::genTableAst(virtualStack, *insn, *(insn + 1)));
                break;
            }
            case LOP_SETTABLE: {
                bodyHandler.addStat(Luau::Decompiler::AstGen::Handlers::genSetTable(virtualStack, *insn));
                break;
            }
            case LOP_MOVE: {
                // functionArgs is only sized to proto->numparams, but a MOVE's source
                // register can reference any local register up to maxstacksize, so this
                // must be bounds-checked before indexing (was functionArgs->at(...),
                // which throws std::out_of_range / aborts the whole decompile on any
                // MOVE from a register beyond the parameter list).
                unsigned int srcReg = LUAU_INSN_B(*insn);
                if (srcReg < functionArgs->size() && (*functionArgs)[srcReg]) { // is an arg of the function
                    virtualStack.set(LUAU_INSN_A(*insn), Luau::Decompiler::orPlaceholder(virtualStack[srcReg]));
                    materialize(LUAU_INSN_A(*insn));
                    break;
                }
                auto locVar = Luau::Decompiler::AstGen::Handlers::genLocalStat(virtualStack, locVars, *insn);
                if (!locVar.existed)
                    bodyHandler.addStat(locVar.stat);
                virtualStack.set(LUAU_INSN_A(*insn), locVar.expr);
                break;
            }
            case LOP_JUMPIFEQ:
            case LOP_JUMPIFLE:
            case LOP_JUMPIFLT:
            case LOP_JUMPIFNOTEQ:
            case LOP_JUMPIFNOTLE:
            case LOP_JUMPIFNOTLT:
            {
                unsigned int aux = *(insn + 1);
                auto *left = Luau::Decompiler::orPlaceholder(virtualStack[LUAU_INSN_A(*insn)]);
                auto *right = Luau::Decompiler::orPlaceholder(virtualStack[aux]);
                AstExprBinary::Op op;
                switch(LUAU_INSN_OP(*insn)) {
                    case LOP_JUMPIFEQ:
                        op = AstExprBinary::CompareNe;
                        break;
                    case LOP_JUMPIFNOTEQ:
                        op = AstExprBinary::CompareEq;
                        break;
                    case LOP_JUMPIFLE:
                        op = AstExprBinary::CompareGe;
                        break;
                    case LOP_JUMPIFLT:
                        op = AstExprBinary::CompareGt;
                        break;
                    case LOP_JUMPIFNOTLE:
                        op = AstExprBinary::CompareLe;
                        break;
                    case LOP_JUMPIFNOTLT:
                        op = AstExprBinary::CompareLt;
                        break;
                    default:
                        break;
                }
                if (bodyHandler.getType() == BodyType::WHILE) {
                    bodyHandler.updateWhileCond(new AstExprBinary{Location(), op, left, right});
                    break;
                }
                bodyHandler.makeIf(new AstExprBinary{Location(), op, left, right});
                break;
            }
            case LOP_JUMPIFNOT: {
                if (bodyHandler.getType() == BodyType::WHILE) {
                    bodyHandler.updateWhileCond(Luau::Decompiler::orPlaceholder(virtualStack[LUAU_INSN_A(*insn)]));
                    break;
                } else {
                    bodyHandler.makeIf(Luau::Decompiler::orPlaceholder(virtualStack[LUAU_INSN_A(*insn)]));
                }
                break;
            }
            case LOP_FORNPREP: {
                auto name = new std::string(std::string("loop_index_") + std::to_string(LUAU_INSN_A(*insn) / 4));
                auto local = new AstLocal {AstName(name->c_str()), Location(), nullptr, 1, 1, nullptr};
                locVars.insert(std::make_pair(LUAU_INSN_A(*insn) + 2, local));
                bodyHandler.makeFor(local, Luau::Decompiler::orPlaceholder(virtualStack[LUAU_INSN_A(*insn) + 2]),
                        Luau::Decompiler::orPlaceholder(virtualStack[LUAU_INSN_A(*insn)]),
                        Luau::Decompiler::orPlaceholder(virtualStack[LUAU_INSN_A(*insn) + 1]));
                break;
            }
            case LOP_FORNLOOP: {
                if (bodyHandler.getType() == BodyType::FORPREP)
                    bodyHandler.addStat(bodyHandler.get()->template as<AstStatFor>());
                break;
            }
            case LOP_NOP: {
                if (bodyHandler.getType() == BodyType::FORPREP) {
                    if (AstStat* stat = bodyHandler.get()) bodyHandler.addStat(stat->template as<AstStatFor>());
                } else if (bodyHandler.getType() == BodyType::WHILE) {
                    if (AstStat* stat = bodyHandler.get()) bodyHandler.addStat(stat->template as<AstStatWhile>());
                }
                break;
            }
            case LOP_GETVARARGS: {
                virtualStack.set(LUAU_INSN_A(*insn), new AstExprVarargs{Location()});
                break;
            }
            case LOP_ADD:
                virtualStack.set(LUAU_INSN_A(*insn), new AstExprBinary {Location(), AstExprBinary::Op::Add,
                                                                        Luau::Decompiler::orPlaceholder(virtualStack[LUAU_INSN_B(*insn)]),
                                                                        Luau::Decompiler::orPlaceholder(virtualStack[LUAU_INSN_C(*insn)])});
                materialize(LUAU_INSN_A(*insn));
                break;
            case LOP_SUB:
                virtualStack.set(LUAU_INSN_A(*insn), new AstExprBinary {Location(), AstExprBinary::Op::Sub,
                                                                        Luau::Decompiler::orPlaceholder(virtualStack[LUAU_INSN_B(*insn)]),
                                                                        Luau::Decompiler::orPlaceholder(virtualStack[LUAU_INSN_C(*insn)])});
                materialize(LUAU_INSN_A(*insn));
                break;
            case LOP_MUL:
                virtualStack.set(LUAU_INSN_A(*insn), new AstExprBinary {Location(), AstExprBinary::Op::Mul,
                                                                        Luau::Decompiler::orPlaceholder(virtualStack[LUAU_INSN_B(*insn)]),
                                                                        Luau::Decompiler::orPlaceholder(virtualStack[LUAU_INSN_C(*insn)])});
                materialize(LUAU_INSN_A(*insn));
                break;
            case LOP_DIV:
                virtualStack.set(LUAU_INSN_A(*insn), new AstExprBinary {Location(), AstExprBinary::Op::Div,
                                                                        Luau::Decompiler::orPlaceholder(virtualStack[LUAU_INSN_B(*insn)]),
                                                                        Luau::Decompiler::orPlaceholder(virtualStack[LUAU_INSN_C(*insn)])});
                materialize(LUAU_INSN_A(*insn));
                break;
            case LOP_MOD:
                virtualStack.set(LUAU_INSN_A(*insn), new AstExprBinary {Location(), AstExprBinary::Op::Mod,
                                                                        Luau::Decompiler::orPlaceholder(virtualStack[LUAU_INSN_B(*insn)]),
                                                                        Luau::Decompiler::orPlaceholder(virtualStack[LUAU_INSN_C(*insn)])});
                materialize(LUAU_INSN_A(*insn));
                break;
            case LOP_POW:
                virtualStack.set(LUAU_INSN_A(*insn), new AstExprBinary {Location(), AstExprBinary::Op::Pow,
                                                                        Luau::Decompiler::orPlaceholder(virtualStack[LUAU_INSN_B(*insn)]),
                                                                        Luau::Decompiler::orPlaceholder(virtualStack[LUAU_INSN_C(*insn)])});
                materialize(LUAU_INSN_A(*insn));
                break;
            case LOP_RETURN: {
                // B == 1 encodes one returned value; dropping that case made
                // simple `return expression` functions appear empty.
                if (LUAU_INSN_B(*insn) == 0 || LUAU_INSN_B(*insn) > 1) {
                    auto* argVec = new std::vector<AstExpr *>{};
                    int argCount = LUAU_INSN_B(*insn) == 0 ? virtualStack.getTop() + 1 : LUAU_INSN_B(*insn) - 1;
                    unsigned int startReg = LUAU_INSN_A(*insn);
                    for (unsigned int i = startReg; i < startReg + argCount; i++) {
                        if (virtualStack[i])
                            argVec->push_back(virtualStack[i]);
                    }
                    AstArray<AstExpr *> args {argVec->data(), argVec->size()};
                    bodyHandler.addStat(new AstStatReturn {Location(), args});
                }
                break;
            }
            default:
                break;
        }
    }
}
