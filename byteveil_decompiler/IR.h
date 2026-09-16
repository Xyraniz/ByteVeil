#pragma once
#include <string>
#include <vector>
#include "Common.h"

namespace Luau::Decompiler::IR {
struct Instruction {
    int offset = 0;
    int opcode = -1;
    int length = 1;
    int a = 0, b = 0, c = 0, d = 0, e = 0;
    int line = 0;
    int jumpTarget = -1;
    bool hasAux = false;
    std::string semanticTag;
};

struct BasicBlock {
    int id = 0;
    int start = 0;
    int end = 0;
    std::vector<int> instructions;
    std::vector<int> successors;
};

struct Function {
    int id = 0;
    int parentId = -1;
    int prototypeIndex = 0;
    int parameters = 0;
    int registers = 0;
    int constants = 0;
    int upvalues = 0;
    int lineDefined = 0;
    std::string nameHint;
    std::vector<Instruction> instructions;
    std::vector<BasicBlock> basicBlocks;
    std::vector<Function> children;
};

struct Module {
    std::string format = "Luau bytecode";
    int version = 0;
    std::string source;
    Function root;
};

bool validateProto(const Proto* root, std::string& error, int maxDepth = 128, int maxInstructions = 1000000);
bool buildModule(const Proto* root, Module& module, std::string& error);
std::string opcodeName(int opcode);
std::string toJson(const Module& module);
std::string disassemble(const Module& module);
std::string cfgDot(const Module& module);
std::string constantsText(const Proto* root);
std::string prototypesText(const Proto* root);
}

namespace Luau::Decompiler {
std::string inspectBytecode(lua_State* L, std::string& bytecode, const std::string& mode, std::string& error);
}

using Luau::Decompiler::IR::Module;
using Luau::Decompiler::IR::Instruction;
using Luau::Decompiler::IR::BasicBlock;
using Luau::Decompiler::IR::Function;
using Luau::Decompiler::IR::validateProto;
using Luau::Decompiler::IR::buildModule;
using Luau::Decompiler::IR::opcodeName;
using Luau::Decompiler::IR::toJson;
using Luau::Decompiler::IR::disassemble;
using Luau::Decompiler::IR::cfgDot;
using Luau::Decompiler::IR::constantsText;
using Luau::Decompiler::IR::prototypesText;
using Luau::Decompiler::inspectBytecode;
