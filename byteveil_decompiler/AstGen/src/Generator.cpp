//
// Created by xgladius on 8/7/22.
//
#include "../include/Generator.h"
#include "../include/Common.h"

#include <memory>

namespace Luau::Decompiler::AstGen {

    AstStat* AstStatForGenerator::generate()  {
        AstArray<AstStat*> bodyArray {body->data(), body->size()};
        return new AstStatFor {Location(), var,
                                from, to, step,
                                new AstStatBlock {Location(), bodyArray},
                                true, Location(), true };
    }

    AstStat* AstStatIfGenerator::generate() {
        AstArray<AstStat*> bodyArray {body->data(), body->size()};
        AstArray<AstStat*> ebodyArray {elseBody->data(), elseBody->size()};
        return new AstStatIf{ Location(), condition,
                              new AstStatBlock {Location(), bodyArray},
                              nullptr,
                              Location(), Location(), true };
    }

    AstStat* BodyGenerator::generate() {
        AstArray<AstStat*> bodyArray {body->data(), body->size()};
        return new AstStatBlock{ Location(), bodyArray };
    }

    AstStat* AstStatWhileGenerator::generate() {
        AstArray<AstStat*> bodyArray {body->data(), body->size()};
        // On control flow this lifter's block-structuring doesn't cleanly
        // recognize as a normal while-loop, updateCondition() can end up never
        // called, leaving `condition` null (previously also literally
        // uninitialized memory -- see the constructor fix in Generator.h).
        // Printing a null AstExpr* here crashes the whole run the moment this
        // loop is visited, so substitute a visible placeholder instead of an
        // unconditional true/false that would silently misrepresent the loop.
        AstExpr* cond = condition ? condition : Luau::Decompiler::orPlaceholder(nullptr);
        return new AstStatWhile { Location(), cond, new AstStatBlock {Location(), bodyArray},
                                  true, Location(), true };
    }

    void AstStatWhileGenerator::updateCondition(AstExpr *cond) {
        condition = cond;
    }
}