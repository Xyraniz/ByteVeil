#pragma once

#include <string>
#include "Common.h"

namespace Luau::Decompiler::RegisterDecompile {

// Render a conservative Luau reconstruction that models registers and control
// flow explicitly. This is used when the legacy expression/AST lifter cannot
// safely reduce the bytecode to ordinary structured source.
std::string render(const Proto* root, std::string& error);

// Return true when a prototype tree contains control flow that the legacy
// block-state lifter is known to lose or mis-shape.
bool shouldPrefer(const Proto* root);

}
