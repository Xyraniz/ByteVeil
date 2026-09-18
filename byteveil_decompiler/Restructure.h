#pragma once
#include <string>
#include "IR.h"

namespace Luau::Decompiler::Restructure {
// Produce a deterministic, source-like control-flow plan.  This is deliberately
// separate from the legacy AST lifter: unsafe regions remain explicit gotos
// instead of being silently misrepresented as Lua.
std::string render(const IR::Module& module);
}
