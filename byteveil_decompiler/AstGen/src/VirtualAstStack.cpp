//
// Created by xgladius on 8/6/22.
//
#include "../include/VirtualAstStack.h"

#include <utility>

namespace Luau::Decompiler::AstGen {
    VirtualAstStack::VirtualAstStack() : virtualStack(256, nullptr) {}

    // Luau functions can legally have more than 256 registers, and on bytecode
    // this lifter partially or incorrectly understands (an opcode its length
    // table doesn't fully cover, causing the pc walk to desync from real
    // instruction boundaries), decoded register indices can come out wildly
    // out of range. All access below used to be raw std::vector operator[]
    // with no bounds check, which is undefined behavior (observed as a hard
    // segfault) the moment idx is negative or >= size. Treating an
    // out-of-range index as "no value here" instead keeps the lifter honest —
    // callers already handle a missing register via orPlaceholder()/nullptr
    // checks — and it degrades a pathological function to a partially-lifted
    // one instead of crashing the whole run.
    static bool inRange(const std::vector<AstExpr*>& v, int idx) {
        return idx >= 0 && static_cast<size_t>(idx) < v.size();
    }

    AstExpr* VirtualAstStack::get_or(int idx, AstExpr* ore) {
        if (!inRange(virtualStack, idx))
            return ore;
        auto get = virtualStack[idx];
        if (get != nullptr) {
            virtualStack[idx] = nullptr;
            return get;
        }
        return ore;
    }

    void VirtualAstStack::remove(int idx) {
        if (!inRange(virtualStack, idx))
            return;
        virtualStack[idx] = nullptr;
    }

    void VirtualAstStack::set(int idx, AstExpr* ex) {
        if (!inRange(virtualStack, idx))
            return;
        virtualStack[idx] = std::move(ex);
    }

    AstExpr* VirtualAstStack::operator[](int idx) {
        if (!inRange(virtualStack, idx))
            return nullptr;
        return virtualStack[idx];
    }

    void VirtualAstStack::clear() {
        for (auto & i: virtualStack) {
            i = nullptr;
        }
    }

    int VirtualAstStack::getTop() {
        for (auto i = 0; i < virtualStack.size(); i++) {
            if (virtualStack[i] == nullptr)
                return i - 1;
        }
        return 0;
    }
}