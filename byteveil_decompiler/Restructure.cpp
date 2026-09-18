#include "Restructure.h"
#include <algorithm>
#include <functional>
#include <set>
#include <sstream>

namespace Luau::Decompiler::Restructure {
namespace {
std::string indent(int n) { return std::string(size_t(n * 2), ' '); }
std::string kind(const IR::Function& f, int id, const std::set<int>& headers)
{
    const IR::BasicBlock& b = f.basicBlocks[id];
    if (id == 0) return "entry";
    if (headers.count(id)) return "loop-header";
    if (b.successors.empty()) return "exit";
    if (b.successors.size() > 1) return "conditional";
    if (b.predecessors.size() > 1) return "join";
    return "linear";
}
int commonPostdom(const IR::Function& f, int left, int right)
{
    if (left < 0 || right < 0 || left >= int(f.postDominators.size()) || right >= int(f.postDominators.size())) return -1;
    std::vector<int> candidates;
    for (int block : f.postDominators[left])
        if (f.postDominators[right].count(block)) candidates.push_back(block);
    std::sort(candidates.begin(), candidates.end(), [&](int a, int b) {
        return f.postDominators[a].size() < f.postDominators[b].size();
    });
    return candidates.empty() ? -1 : candidates.front();
}
void renderFunction(std::ostringstream& out, const IR::Function& f, int level)
{
    std::set<int> headers;
    for (const auto& edge : f.backEdges) headers.insert(edge.second);
    out << indent(level) << "function " << f.id << " -- structured CFG plan\n";
    out << indent(level) << "  -- blocks=" << f.basicBlocks.size() << ", loops=" << f.loops.size()
        << ", sccs=" << f.stronglyConnectedComponents.size() << ", phis=" << f.phiNodes.size() << "\n";
    for (const IR::BasicBlock& block : f.basicBlocks)
    {
        out << indent(level) << "  block_" << block.id << " -- " << kind(f, block.id, headers);
        if (block.id < int(f.immediateDominators.size())) out << ", idom=block_" << f.immediateDominators[block.id];
        out << "\n";
        if (block.successors.size() == 2)
        {
            const int join = commonPostdom(f, block.successors[0], block.successors[1]);
            out << indent(level) << "    if <condition from block_" << block.id << "> then\n";
            out << indent(level) << "      -- true -> block_" << block.successors[0] << "\n";
            out << indent(level) << "    else\n";
            out << indent(level) << "      -- false -> block_" << block.successors[1] << "\n";
            out << indent(level) << "    end";
            if (join >= 0) out << " -- join=block_" << join;
            out << "\n";
        }
        else if (headers.count(block.id))
        {
            out << indent(level) << "    while true do -- natural loop header\n";
            for (const IR::Loop& loop : f.loops)
                if (loop.header == block.id) out << indent(level) << "      -- loop blocks: " << loop.blocks.size() << "\n";
            out << indent(level) << "    end\n";
        }
        else if (block.successors.empty()) out << indent(level) << "    return\n";
        else if (block.successors.size() == 1) out << indent(level) << "    goto block_" << block.successors[0] << "\n";
        else out << indent(level) << "    -- irreducible edge set; preserved as explicit CFG edges\n";
        for (int instructionIndex : block.instructions)
        {
            if (instructionIndex < 0 || instructionIndex >= int(f.instructions.size())) continue;
            const IR::Instruction& instruction = f.instructions[instructionIndex];
            out << indent(level) << "    -- @" << instruction.offset << " " << IR::opcodeName(instruction.opcode);
            if (instruction.destinationRegister >= 0) out << " r" << instruction.destinationRegister << " <-";
            if (instruction.hasSideEffects) out << " [side-effect]";
            else if (instruction.isPure) out << " [pure]";
            out << "\n";
        }
    }
    for (const IR::Function& child : f.children) renderFunction(out, child, level);
}
}
std::string render(const IR::Module& module)
{
    std::ostringstream out;
    out << "-- ByteVeil structured control-flow reconstruction\n";
    out << "-- This output preserves unsafe regions explicitly; it is not executed.\n";
    renderFunction(out, module.root, 0);
    return out.str();
}
}
