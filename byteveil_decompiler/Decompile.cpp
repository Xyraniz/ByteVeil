//
// Created by xgladius on 8/7/22.
//
#include "Decompile.h"
#include "IR.h"
#include "RegisterDecompile.h"
#include "Luau/Allocator.h"
#include "Luau/Lexer.h"
#include "Luau/Parser.h"
#include <Luau/Compiler.h>

namespace Luau::Decompiler {
    std::string decompile(lua_State* L, std::string& bytecode) {
        Proto* p = nullptr;
        std::string loadError;
        if (!loadBytecode(L, bytecode, p, loadError))
            return std::string("error: ") + loadError;

        auto validSource = [&](const std::string& source, std::string& validationError) {
            Luau::Allocator allocator;
            Luau::AstNameTable names(allocator);
            Luau::ParseResult parsed = Luau::Parser::parse(source.data(), source.size(), names, allocator);
            if (!parsed.errors.empty())
            {
                const Luau::ParseError& parseError = parsed.errors.front();
                const Luau::Position& position = parseError.getLocation().begin;
                validationError = "invalid Luau at " + std::to_string(position.line) + ":" + std::to_string(position.column) + ": " + parseError.getMessage();
                return false;
            }

            std::string syntaxBytecode = Luau::compile(source);
            if (syntaxBytecode.empty())
            {
                validationError = "Luau compiler rejected reconstructed source";
                return false;
            }
            int stackTop = lua_gettop(L);
            bool valid = luau_load(L, "byteveil-reconstruction", syntaxBytecode.data(), syntaxBytecode.size(), 0) == 0;
            if (!valid)
            {
                const char* message = lua_tostring(L, -1);
                validationError = message ? message : "reconstructed bytecode failed to load";
            }
            lua_settop(L, stackTop);
            return valid;
        };
        auto registerFallback = [&]() {
            std::string error;
            std::string source = RegisterDecompile::render(p, error);
            if (source.empty())
                return std::string("error: register-state reconstruction failed: ") + (error.empty() ? "no output" : error);
            std::string validationError;
            if (!validSource(source, validationError))
                return std::string("error: register-state reconstruction failed validation: ") + validationError;
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
        std::string output = Luau::toString(block);
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
        std::string validationError;
        if (!validSource(output, validationError))
            return registerFallback();
        return output;
    }
}
