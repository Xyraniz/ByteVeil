<div align="center">
<h1>ByteVeil</h1>
  <p><strong>A static analyzer and partial decompiler for Lua 5.1 and Luau bytecode.</strong></p>
  <p>
    <a href="https://github.com/Xyraniz/ByteVeil"><img src="https://img.shields.io/github/languages/top/Xyraniz/ByteVeil?style=flat-square" alt="Top language" /></a>
    <a href="https://github.com/Xyraniz/ByteVeil/issues"><img src="https://img.shields.io/github/issues/Xyraniz/ByteVeil?style=flat-square" alt="Issues" /></a>
  </p>
</div>

ByteVeil is a standalone C++ command-line tool for inspecting Lua 5.1 chunks and Luau source or bytecode. It detects the input format, loads bytecode into the vendored Luau libraries when needed, and exposes structured information without executing the analyzed program.

The project has several separate paths rather than one universal decompiler. Lua 5.1 chunks use a dedicated reader and lifting route. Luau source is compiled to bytecode before inspection by default, while Luau bytecode can be inspected directly or passed through the higher-level lifter. Protector detection and literal payload inspection stay on their own static routes.

## What ByteVeil does

ByteVeil can:

- Identify Lua 5.1 and Luau bytecode by signature.

- Compile Luau source to bytecode for analysis.

- Emit JSON or IR-style descriptions of Luau functions, prototypes, constants, instructions, source lines, jump targets, basic blocks, and nested functions.

- Print deterministic instruction disassembly with opcode operands, block successors, line information, auxiliary-word markers, and jump targets.

- Produce a Graphviz DOT control-flow graph for the root function.

- Print constant-table and prototype summaries.

- Lift supported Lua 5.1 instructions into Lua source, keeping unsupported instructions as comments with program-counter and operand information.

- Lift supported Luau bytecode through a block and AST pipeline based on Luau's internal AST types.

- Preserve structured state transitions for control-flow shapes that are not safely reducible to an ordinary `if` or `while` construct.

- Report static indicators associated with MoonSec V3, Luraph, dynamic loaders, Roblox or executor APIs, dispatcher-like virtual-machine patterns, and large escaped blobs.

- Extract only literal strings passed to `loadstring` through the `unpack` route, returning JSON with `executed: false`.

## Comparison and design choices

ByteVeil was audited against [shrimp-nz/medal](https://github.com/shrimp-nz/medal), whose strongest contribution is a Rust AST/SSA pipeline with dominator-based control-flow restructuring, local renaming, and a formatter that handles precedence and statement disambiguation. ByteVeil keeps those ideas as design targets, but does not copy Medal's code: it already provides a broader safety-oriented CLI, static protector and loader analysis, deterministic JSON/IR, Graphviz output, automatic Lua 5.1/Luau detection, and a vendored Luau execution-free inspection path.

The current implementation closes the most actionable gap from that comparison on the Lua 5.1 route. Debug-local and upvalue names are reused when their lifetime is known, conditional jumps and loop transitions are preserved in `structured` output, `LOADBOOL`, `JMP`, `SETLIST`, `TAILCALL`, early `RETURN`, and malformed/non-variadic `VARARG` cases are represented without producing invalid Lua, and large nested-prototype chunks are emitted through a function table rather than exceeding Lua's 200-local limit. This is deliberately conservative: unresolved semantics remain visible in comments or placeholders instead of being invented.

The second pass adds the CFG foundation needed for a future Medal-style restructurer on Luau bytecode. Every function now exposes reachable-block immediate dominators, back-edges, and natural-loop membership in JSON under `cfg_analysis`; disassembly prints each block's `idom`, and Graphviz marks loop headers. These facts are computed without executing bytecode and remain available even when the higher-level AST lifter cannot safely reduce a graph to idiomatic Lua.

The third pass adds conservative register SSA metadata. Register-writing instructions receive deterministic definition versions, and joins whose incoming versions differ receive explicit phi nodes. JSON consumers can read `ssa.instruction_def_versions` and `ssa.phi_nodes`; the disassembler prints phi summaries next to their blocks. The IR now also records instruction uses, destinations, constant references, source and target blocks, purity/effects, block predecessors, SCCs, loop records, and conservative root/loop scopes. This is an analysis layer for future restructuring, not a claim that dynamic aliasing or multiple-return semantics have been fully solved.

The output of the lifters is an analysis aid. It is not a promise that every input can be reconstructed into equivalent, idiomatic source code.

The Luau lifter now validates its printed result before returning success. If control-flow recovery loses a loop body or a closure value, the CLI returns an explicit error instead of reporting malformed source as a successful decompilation. This is intentionally a visible analysis boundary while the Luau AST restructurer is being completed.

The Lua 5.1 `lua` route now includes a conservative CFG restructor for common reducible shapes. It recognizes numeric `for`, `while`, `repeat/until`, conditional branches, nested `if/else` blocks, and nested function bodies. A backward register-definition pass propagates simple `LOADK`, `MOVE`, and `LOADNIL` values into loop bounds and expressions, while debug-local ranges are used to keep stable source names where the bytecode retains them. Unsupported or ambiguous graphs still fall back to visible register-oriented statements rather than being silently guessed.

## Safety model

ByteVeil is designed for static inspection. The normal CLI does not execute the input script, a loader, a virtual machine contained in the input, or network code. The protector route only searches for textual evidence and returns a detector result. The unpack route extracts literal payloads but does not run them.

The repository includes a separate equivalence harness for Lua 5.1 lifting. That harness runs the original and candidate programs with an external Lua 5.1 interpreter and compares exit code, standard output, and standard error. This is a validation tool for a candidate lifting result, not part of the normal analysis path.

Static indicators are heuristic evidence. A marker can be absent, misleading, or embedded in an unrelated string. The tool should not be described as a protector identifier with proof of family membership, and it does not automatically defeat a virtualized or dynamically generated loader.

## Build requirements

ByteVeil uses CMake, Ninja, and C++17. The repository vendors the Luau source tree needed for the compiler, VM, AST, and analysis libraries, so the main build does not require downloading Luau at build time.

On Ubuntu, install the basic toolchain with:

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake ninja-build
```

Configure and build:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

The resulting executable is:

```
build/byteveil
```

The CMake project uses the vendored Luau components `Luau.Compiler`, `Luau.Analysis`, `Luau.VM`, and `Luau.Ast`.

## Command-line usage

The executable reports its own version and help text:

```bash
./build/byteveil --version
./build/byteveil --help
```

The general form is:

```
ByteVeil <input> [options]
```

Important options are:

```
-o, --output FILE       Write output to FILE
--format lua|luau|json|ir|structured|protectors|unpack
--disassemble           Print deterministic low-level disassembly
--dump-constants        Print constant table summary
--dump-prototypes       Print prototype summary
--cfg FILE              Write the root CFG as Graphviz DOT
--analyze               Print static indicators; never executes input
--bytecode              Treat input as precompiled bytecode
--source                Treat input as Luau source
--no-color              Disable colored diagnostics
--timeout SECONDS       Abort decompilation after a wall-clock limit
-q, --quiet             Suppress informational messages
```

The default input mode is source-oriented. Bytecode can be forced with `--bytecode`, and source compilation can be selected explicitly with `--source`. Inputs with Lua 5.1 or Luau bytecode signatures are recognized automatically.

## Inspect Luau source or bytecode

Compile and inspect a Luau source file as JSON:

```bash
./build/byteveil script.luau --format json
```

Inspect an existing Luau bytecode file:

```bash
./build/byteveil --bytecode sample.luac --format json
```

Print a deterministic disassembly:

```bash
./build/byteveil --bytecode sample.luac --disassemble
```

Write the root control-flow graph as Graphviz DOT:

```bash
./build/byteveil --bytecode sample.luac --cfg graph.dot
```

The JSON/IR representation contains nested functions, parameters, register and constant counts, instructions, basic blocks, successors and predecessors, line information, jump targets, auxiliary-word metadata, register uses/definitions, purity/effects, loop/SCC facts, scopes, and SSA information. The internal IR validates prototype depth, total instruction count, instruction boundaries, operand ranges, and constant/prototype references before building the representation.

## Lift to source-like output

Request the Luau lifter explicitly:

```bash
./build/byteveil --bytecode sample.luac --format lua -o lifted.lua
```

The Lua 5.1 path uses the same output format for supported instructions:

```bash
./build/byteveil --bytecode tests/fixtures/lua51-sample.luac --format lua -o lifted.lua
```

The Lua 5.1 route covers common register, constant, global, upvalue, table, arithmetic, concatenation, unary, closure, call, vararg, and return instructions. Unsupported instructions remain visible in the output as comments instead of silently disappearing.

The Luau lifter builds blocks from bytecode and feeds them into an AST-generation and transpilation pipeline. It includes guards for malformed or unusual register accesses and a wall-clock timeout for decompilation. A timeout or unsupported bytecode shape should be treated as an analysis boundary, not as evidence that the original program is invalid.

## Static protector indicators

Run the detector without executing the input:

```bash
./build/byteveil --format protectors script.lua
```

The detector currently checks for visible markers and patterns associated with:

- MoonSec V3.

- Luraph.

- Dynamic-loader primitives such as `loadstring`, `string.dump`, `getfenv`, `setfenv`, and `load`.

- Roblox or executor API names such as `HttpGet`, `request`, `writefile`, and `getgenv`.

- Dispatcher-like loops and opcode-oriented virtual-machine patterns.

- Large escaped, binary-looking strings.

The result is JSON with a family, confidence, and evidence for each detected indicator. Confidence is heuristic and based on visible evidence in the input.

## Literal payload inspection

The `unpack` route searches for literal strings passed to `loadstring`:

```bash
./build/byteveil --format unpack script.lua
```

The result identifies the visible family, lists extracted literal payloads, sets `executed` to `false`, and reports `no-literal-payload` when there is no literal payload to extract. Dynamically computed payloads are not reconstructed by this route.

## Analysis mode

The `--analyze` option prints a compact static summary:

```bash
./build/byteveil --analyze script.lua
```

The summary reports the detected format, byte count, quoted-string count, visible MoonSec markers, dynamic-loader markers, network or executor names, long encoded-string indicators, and a low or medium heuristic confidence.

## Validation

The repository includes shell-based checks for the CLI and Lua 5.1 inspection path:

```bash
./tests/test_cli.sh ./build/byteveil
./tests/test_lua51.sh ./build/byteveil
./tests/test_luau_regressions.sh ./build/byteveil
```

The equivalence harness compares an original Lua 5.1 program with a candidate lifted program:

```bash
./tests/equivalence_lua51.sh original.lua candidate.lua
```

The project builds successfully as `ByteVeil 0.4.8`; the CLI, Luau integrity, Luau regression, Lua 5.1 reader, extended, and reconstruction suites pass in the maintained checkout. The equivalence harness remains separate and requires an external `lua5.1` executable.

The checked fixtures cover JSON output, disassembly, CFG generation, Lua 5.1 lifting, Luau loop metadata, simple return/call regressions, and truncated-bytecode rejection. The project's existing documentation also records validation against 18 Lua 5.1 chunks derived from a MoonSec V3 corpus. That result describes the recorded test run; it is not a guarantee that every protector sample can be analyzed or lifted.

## Project layout

| Path | Purpose |
| --- | --- |
| `byteveil_cli.cpp` | CLI parsing, input detection, source compilation, static analysis, and output routing. |
| `byteveil_decompiler/Lua51.*` | Lua 5.1 chunk detection and inspection. |
| `byteveil_decompiler/IR.*` | Luau function, instruction, basic-block, JSON, disassembly, and CFG representation. |
| `byteveil_decompiler/Protectors.*` | Static protector and loader-marker heuristics. |
| `byteveil_decompiler/Unpack.*` | Literal `loadstring` payload inspection. |
| `byteveil_decompiler/BlockGen/` | Luau bytecode block generation and lifting helpers. |
| `byteveil_decompiler/AstGen/` | AST construction from lifted Luau operations. |
| `luau/` | Vendored Luau compiler, VM, AST, analysis code, and related notices. |
| `tests/` | CLI, Lua 5.1, equivalence scripts, and fixtures. |

## Known limitations

The lifters cover a defined subset of Lua 5.1 and Luau behavior. Complex aliasing, fully dynamic `SETLIST` arity, calculated metamethods, irreducible loops, difficult scope shapes, and some closure, upvalue, vararg, and multiple-return cases can exceed the current lifting model. The structured Lua 5.1 output preserves branch targets but is intentionally a state machine rather than Medal-style fully idiomatic source; the SCC analysis identifies cycles but does not automatically turn every cycle into idiomatic Lua control flow. Luau IR scopes and effects are conservative analysis metadata, not complete source-level lexical lifetime recovery. Luau output remains guarded by structural-integrity checks: when the body, loop, or closure cannot be recovered safely, the CLI fails instead of returning code 0 with a plausible-looking false reconstruction.

The automatic unpack route is intentionally narrow. It extracts literal payloads only. Loaders that calculate their payload dynamically, virtualize execution, or depend on a runtime-specific environment require separate family-specific analysis and validation.

## Attribution and licenses

ByteVeil includes the Luau source tree required for its compiler, VM, AST, and analysis libraries. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md), [luau/LICENSE.txt](luau/LICENSE.txt), and [luau/lua_LICENSE.txt](luau/lua_LICENSE.txt) for the applicable notices.

The repository also documents design references and inspiration, including Oracle Decompiler, `luauDec`, and `unluau`. ByteVeil is not an official fork of those projects and is not affiliated with them.

## Development and verification

ByteVeil now exposes its CFG and SSA metadata through the inspection formats and validates that metadata before returning a module. The recommended local verification command is:

```bash
tests/run_all.sh
```

The runner configures and builds a debug-capable binary, then invokes CTest with `--output-on-failure`. Individual scripts can still be run with `build/byteveil` as their first argument. CLI options are rejected when a format is unknown or when `--timeout` is not a decimal value in the safe range `0..86400`; this prevents accidental silent fallback to a different inspection mode.


## Luau reconstruction pipeline

The Luau inspection path is deliberately split into validated phases. Bytecode is decoded into an IR, basic blocks are connected into a CFG, dominators/post-dominators and natural loops are computed, and register lifetimes, definitions, uses and phi nodes are exposed as dataflow metadata. The `--format structured` mode consumes those facts to render conditional diamonds, joins, loop headers and irreducible edges as an explicit control-flow plan. It preserves unsafe edges instead of guessing source that merely looks plausible.

The source decompiler retains the legacy AST lifter for compatible output, but now applies two safety gates: known incomplete shapes are rejected and the generated Luau is compiled and loaded before it is returned. This follows the analysis recommendation to keep ByteVeil's validation layer while moving toward a Medal-style restructuring phase.

