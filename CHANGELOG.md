# Changelog

## Unreleased - Portable build and watchdog hardening

### Corrected

- CMake 4.x can configure the project even though the vendored Luau checkout still declares a CMake 3.0 policy baseline.
- The root build no longer builds and links all of `Luau.Analysis` merely to obtain `Transpiler.cpp`. Only the transpiler is compiled, avoiding unrelated legacy-analysis compiler failures and reducing the build graph.
- The address-space guard now uses a Windows Job Object on Windows and `RLIMIT_AS` on POSIX systems.
- The decompiler timeout no longer uses `siglongjmp` across live C++ objects. A portable watchdog exits the CLI with code 124 after printing the timeout diagnostic.
- CTest passes the real target path to shell tests, including the Windows executable suffix, and JSON tests find a working `python3` or `python` interpreter rather than trusting a broken application alias.
- Lua 5.1 tests that require an external interpreter now skip explicitly when `lua5.1`/`luac5.1` are unavailable instead of reporting an unrelated product regression.

### Validation

- CMake 4.4.1 configure and Ninja/MinGW build: **PASS**.
- Six-test CTest suite on Windows: **PASS** (Lua 5.1 external-interpreter tests skipped when the interpreter is absent).

## Unreleased - Conservative register SSA and phi analysis

### Added

- Luau IR now tracks deterministic definition versions for register-writing instructions.
- CFG joins with distinct incoming register definitions produce explicit `phi_nodes`, including block, register, generated version, and incoming versions.
- SSA metadata is emitted in JSON under `ssa` and summarized in disassembly as `phi=rN:vM`.
- The implementation is intentionally conservative: it models register definitions and joins without claiming that every Luau multiple-return, aliasing, or metamethod case has been resolved.

### Validation

- Deterministic JSON was verified byte-for-byte on branch and loop samples.
- A sample containing an `if` join and numeric loop produced eight phi nodes with stable versions.
- Existing CLI, Lua 5.1, and extended regression suites remain passing.

## Unreleased - Lua 5.1 semantic lifting and structured CFG hardening

This iteration follows a source audit against [shrimp-nz/medal](https://github.com/shrimp-nz/medal), [metaworm/luac-parser-rs](https://github.com/metaworm/luac-parser-rs), and other actively maintained Lua decompiler projects. Medal remains stronger in full AST/SSA restructuring and idiomatic formatting; ByteVeil retains a wider safety-oriented inspection surface, including Luau support, static protector analysis, deterministic JSON/IR, CFG export, and non-executing loader inspection. The implementation below adopts the highest-value behavior without copying incompatible code or licenses.

### Added

- Lua 5.1 lifting now consults debug-local intervals and names, selecting the innermost active local for register references and using declared upvalue names when available.
- `LOADBOOL` skip behavior and `JMP` transitions are explicitly retained in lifted comments and structured output.
- `SETLIST` with a fixed element count is expanded into indexed assignments; open-tail `SETLIST` remains annotated with its producer instruction rather than being guessed.
- Early `RETURN` and `TAILCALL` output is wrapped in a `do ... end` block so unreachable bytecode that follows a return does not make the generated Lua syntactically invalid.
- Non-variadic prototypes that contain `VARARG` are represented as a visible diagnostic assignment instead of emitting illegal `...`; open returns only emit `...` for prototypes marked variadic.
- Structured Lua 5.1 output now preserves conditional branch decisions, numeric-loop back edges, generic-loop exits, and `LOADBOOL` skip edges. Nested prototypes are emitted through a function table, avoiding Lua's 200-local limit on large chunks.
- `tests/test_lua51_extended.sh` covers compilation, local/branch lifting, structured output, JSON analysis, and syntax validation. The real `Xyraniz/Obfuscator-Samples` `output.lua` fixture was also processed: 643 functions and 40,598 instructions were decoded, and both Lua output modes parsed successfully with Lua 5.1.
- Luau IR now computes reachable-block dominators with an iterative data-flow pass, immediate dominators, CFG back-edges, and natural-loop membership. The results are available under `cfg_analysis` in JSON, `idom=block_N` in disassembly, and annotated loop headers in Graphviz output.
- The CFG metadata is calculated per nested function and remains deterministic, giving a future AST restructurer the same structural foundation used by Medal without pretending that every graph is reducible.
- Luau numeric and generic loop opcodes are now treated as conditional terminators when constructing block successors. This preserves both the loop back-edge and the exit edge; previously `FORNPREP`/`FORNLOOP` could leave the loop body unreachable in the derived CFG.

### Corrected

- The Lua 5.1 version assertion in `tests/test_lua51.sh` now matches the CLI's actual `0.4.8` version instead of the stale `0.4.7` expectation.
- Generated Lua no longer fails parsing solely because the bytecode contains early returns, invalid vararg metadata, or too many nested prototype state variables.

### Validation

- Release CMake build: **PASS**.
- `tests/test_cli.sh`: **PASS**.
- `tests/test_lua51.sh`: **PASS**.
- `tests/test_lua51_extended.sh`: **PASS**.
- `output.lua` from `Xyraniz/Obfuscator-Samples`: JSON parse, 643-prototype traversal, Lua lift syntax, and structured lift syntax: **PASS**.

### Explicit limits

- This does not claim Medal-level idiomatic recovery of every Lua construct. The structured output is a faithful state machine, not a universal `if`/`while` restructurer.
- Dynamic `SETLIST` arity, metamethod-dependent behavior, irreducible control flow, and protector-specific virtualization still require specialized analysis. ByteVeil continues to mark those boundaries instead of executing or inventing semantics.

## 0.4.8 - Hardened Luau lifter and wider opcode coverage

This release was driven by targeted tests against a real MoonSec V3 sample, `Xyraniz/Obfuscator-Samples` at `Moonsec/v3/323928.lua`, using `--format lua`. Before these modifications, the sample could reproducibly crash or hang the process.

The `BlockGen` and `AstGen` code is almost a direct copy of `xgladius/luauDec`. The implementation was compared line by line with the upstream repository to separate inherited bugs from ByteVeil modifications. The issues below were present in the vendored lifter and were fixed locally.

### Fixed crashes and hangs

- `functionArgs->at(B)` in `LOP_MOVE` could throw `std::out_of_range`. The vector only had `proto->numparams` entries, while `MOVE` can refer to any local register. The access now checks its bounds.

- `BlockGen<false>` did not generate its own `subFuncs` for non-root functions. A closure defined inside another closure, which is common in obfuscator VMs, could therefore index an empty vector at `LOP_NEWCLOSURE` and segfault. Each nesting level now generates its child functions recursively, as `Decompile.cpp` already did for the root prototype.

- `VirtualAstStack` indexed its 256-register vector with unchecked `operator[]` access. When bytecode flow became desynchronized from actual instruction boundaries, decoded register indexes could be far outside the vector, including an observed `idx=7074`. Out-of-range access now returns no value instead of invoking undefined behavior or crashing.

- `getCallAst` and `genTableAst` could underflow an integer. `virtualStack.getTop()` can legitimately return `-1`; using that value in an unsigned loop bound could wrap to roughly four billion and leave the process in an almost infinite loop. The argument count is now clamped to a sensible range before it becomes a loop bound.

- `AstStatWhileGenerator::condition` was not initialized in the constructor. If `updateCondition()` was never called because the control flow was outside the lifter's model, the final `AstStatWhile` could contain a null or uninitialized `AstExpr*`, causing Luau's printer to crash. The field now starts as `nullptr`, and `generate()` inserts a visible placeholder when no condition was produced.

- `handleAllInstructions()` assumed that `bodyHandler.get()->as<AstStatBlock>()` would always succeed. For control flow that the `BodyHandler` state machine cannot model cleanly, `as<>()` can return `nullptr`, which was then inserted as a function body and crashed the printer. The code now falls back to an empty block with a visible comment.

- Several paths in `Handlers.cpp` and `BlockGen.cpp`, including calls, arithmetic and unary operators, `JUMPIF*`, `FORNPREP`, table operations, and `MOVE`, assumed that a virtual register always contained a value. An unsupported opcode could leave the register as `nullptr`, and a later `->is<>()`, `->as<>()`, or direct AST insertion could crash. A new `orPlaceholder()` helper is now used at those sites. An unresolved register is rendered as `--[[ byteveil: unresolved register ]]` instead of crashing the process.

- `GETGLOBAL`, `SETGLOBAL`, `GETTABLEKS`, `NAMECALL`, and `LOADK` assumed that the constant index in `aux` or `D` was within `proto->sizek` and referred to a string. The new `getConstantName()` helper checks both the bounds and the type. Invalid indexes and non-string constants now produce readable placeholders, `?byteveil_bad_const_index?` and `?byteveil_non_string_const?`, instead of reading outside the valid range.

### Added

- Expanded high-level Luau lifter coverage. The lifter previously handled roughly 35 of the approximately 80 defined opcodes. This release adds `LOADNIL`, `LOADN`, `LOADB`, which was disabled in the inherited code, `NOT`, `MINUS`, `LENGTH`, `CONCAT`, `DUPCLOSURE` for closures without upvalues, and `GETIMPORT`.

- `DUPCLOSURE` locates the target `Proto*` inside `proto->p` when the closure has no upvalues.

- `GETIMPORT` decodes global access chains such as `string.byte` by reading up to three 10-bit packed constant indexes from the `aux` word, following `BytecodeBuilder::getImportId`.

- `--timeout SECONDS`, with a default of 20 seconds and `0` meaning no limit. Decompilations that exceed the limit now abort cleanly instead of hanging indefinitely. This is an additional safeguard, not a proof that every pathological input is harmless.

- A 2 GiB address-space limit is applied before the Luau decompiler runs. An uncontrolled allocation now fails predictably as `std::bad_alloc`, which is handled by the existing `try`/`catch`, instead of ending the process with an allocator-related `SIGBUS` or `SIGSEGV`.

### Verification

- CMake Release and Debug builds: **PASS**.

- Existing CLI and Lua 5.1 suites, `tests/test_cli.sh` and `tests/test_lua51.sh`: **PASS**, with no regressions at release time.

- Line-by-line comparison with the current `xgladius/luauDec` commit to separate inherited bugs from ByteVeil modifications.

- The real `Moonsec/v3/323928.lua` sample from `Xyraniz/Obfuscator-Samples` previously crashed or hung reproducibly under `--format lua`, as confirmed with `gdb`. It now exits cleanly and produces honest partial output, with explicit placeholders where the lifter cannot resolve a value.

- Regression checks with ordinary Lua snippets covering closures, upvalues, concatenation, tables, numeric `for` loops, calls to `print` and `ipairs`, metatable-based object methods, `pairs`, and `pcall`. The unpatched build crashed with an exception even on a simple closure snippet. The patched build did not crash in the cases tested.

### Explicit limits

- The high-level Luau lifter remains **experimental**. It still does not cover every Luau opcode, including generic `GETTABLE` and `SETTABLE`, `FASTCALL*`, generic `FORGPREP` and `FORGLOOP` used by `pairs` and `ipairs`, and every `GETUPVAL` or `SETUPVAL` case. These fixes prioritize clean failure over a perfectly faithful reconstruction. Unsupported regions are marked with visible placeholders instead of being fabricated.

- The MoonSec V3 sample is not "deobfuscated" in the sense of recovering the original source before protection. The output is a partial representation of the compiled VM interpreter bytecode. That matches the project's scope: static detection and lifting, never execution.

## 0.4.7 - Advanced analysis and structural metadata

### Added

- Conservative alias analysis based on table reads and writes.

- Scope-overlap detection from local-variable intervals.

- Counting of dynamic metamethod sites using table opcodes and the `__index`, `__newindex`, and `__call` constants.

- Tarjan SCC analysis to find cycles and loops that should not be forced into a simple `if` or `while` structure.

- Three adaptations inspired by unluac: local names and intervals, PC-to-`lineinfo` mapping, and branch normalization for cycle analysis.

- Expanded JSON with `locals`, `upvalues`, `lines`, per-instruction line information, and an `analysis` block.

### Verification

- CMake Release build and version smoke test: **PASS**.

- Debug metadata and advanced analysis on a fixture containing a table, closure, and loop: **PASS**.

- Existing Lua 5.1 corpus: **18/18** chunks processed to JSON.

### Explicit limits

- These analyses report risks and cycles. They do not claim that semantic equivalence has been resolved.

- Calculated metamethods, complex aliasing, and scopes that escape their original region still require behavioral validation with a fixture.

## 0.4.6 - Closures, upvalues, multiple returns, and literal extraction

### Added

- The Lua 5.1 lifter represents upvalue reads, upvalue writes, nested closures, vararg parameters, `VARARG`, open-arity calls, and basic multiple returns.

- Table reads and writes retain their indexed form so that metamethod behavior is not hidden behind an unsafe simplification.

- New `--format unpack` mode, which identifies the visible family and extracts only literal `loadstring` payloads without executing code.

- Static extraction adapters for MoonSec V3, Luraph, and generic loaders. Dynamically computed payloads are reported explicitly as unavailable.

### Verification

- CMake Release build: **PASS**.

- CLI and Lua 5.1 suites: **PASS**.

- Fixture with a closure, upvalue, and vararg: parsed and lifted successfully.

- JSON preserves nested prototypes and `SETLIST B=0` metadata.

- Literal `loadstring` extraction: validated. Dynamic loaders remain diagnostic-only when their payload cannot be extracted.

### Explicit limits

- Full equivalence still requires validating each fixture with the harness. The lifter does not guarantee complete semantics for complex aliasing, metamethods, irreducible loops, or difficult scope shapes.

- `unpack` does not execute or resolve virtualized VMs, calculated blobs, or remote payloads. A detector is not presented as a universal unpacker.

## 0.4.5 - Lifting, structured CFGs, SETLIST, and detectors

### Added

- Functional Lua 5.1 lifting for `MOVE`, `LOADK`, `LOADBOOL`, `LOADNIL`, `GETGLOBAL`, `NEWTABLE`, arithmetic, concatenation, negation, length, calls, and returns. Unsupported opcodes remain as comments with their PC and operands; the tool does not invent code for them.

- JSON metadata and disassembly for `SETLIST B=0`, including `open_tail` and the PC of the nearest producer, whether `CALL`, `TAILCALL`, or `VARARG`.

- `--format structured`, which produces a state-based Lua representation with a PC counter and explicit transitions.

- Static `--format protectors` detector with JSON evidence for MoonSec V3, Luraph, dynamic loaders, executor or Roblox APIs, dispatcher patterns, and encoded blobs.

- `tests/equivalence_lua51.sh`, which compares exit code, standard output, and standard error for two programs under Lua 5.1.

### Verification

- CMake Release build: **PASS**.

- `tests/test_cli.sh`: **PASS**.

- `tests/test_lua51.sh`: **PASS**.

- Equivalence harness with an identical fixture: **PASS**.

- 18/18 Lua 5.1 chunks derived from the MoonSec corpus processed to JSON without aborting.

- Smoke tests for `--format structured` and `--format protectors`: **PASS**.

### Explicit limits

- The lifter remains partial for closures, upvalues, open varargs, metamethods, complex loops, and multiple returns.

- `SETLIST B=0` is detected and its producer is preserved. Full dynamic-arity expansion still requires a later step.

- Structured output preserves the CFG and transitions but does not always produce idiomatic Lua.

- Protector detectors perform static analysis, not unpacking. Each family requires a desvirtualizer validated against its own samples.

## 0.4.0 - Lua 5.1 reader and automatic format detection

### Added

- Self-contained Lua 5.1 binary reader in `byteveil_decompiler/Lua51.cpp` and `Lua51.h`.

- Validation of the signature, version, format, endianness, type sizes, prototype limits, registers, constants, instructions, strings, line information, locals, and upvalues.

- Support for Lua 5.1 `nil`, boolean, number, and string constants.

- Child-prototype traversal with deterministic IDs and parent-child relationships.

- Lua 5.1 instruction decoding with ABC, Bx, and sBx operands.

- Jump-target detection and Graphviz DOT CFG generation for Lua 5.1.

- Deterministic JSON and IR for Lua 5.1 chunks.

- Lua 5.1 disassembly with opcode names and operands.

- Syntactically valid diagnostic Lua output that preserves the listing when semantic lifting is not yet possible.

- Automatic detection of `\x1bLua` versus `\x1bLuau` in the CLI.

- Luau decompiler exception handling that turns failures into controlled errors.

- Real binary fixture at `tests/fixtures/lua51-sample.luac` and the `tests/test_lua51.sh` suite.

### Verification

- CMake Release build: **PASS**.

- Existing CLI suite: **PASS**.

- Lua 5.1 suite: **PASS**.

- Synthetic Lua 5.1 fixture: JSON, prototypes, disassembly, CFG, and diagnostic output behaved as expected.

- MoonSec V3-derived corpus: **18/18 Lua 5.1 chunks** processed to JSON.

- Lua 5.1 chunks are no longer sent accidentally to the Luau loader.

- Truncated inputs and incompatible formats produce controlled errors.

### Explicit limits

- The Lua 5.1 path is still structural and diagnostic. `--format lua` is not a complete semantic decompiler.

- High-level Lua 5.1 lifting, open `SETLIST` recovery, advanced irreducible-CFG structuring, behavioral equivalence, and protector-specific devirtualization were not yet implemented.

- The high-level Luau path remains experimental.

## 0.3.0

- Initial deterministic Luau IR with function metadata, instructions, basic blocks, CFG successors, and invariant validation.

- JSON, disassembly, CFG, and static analysis without execution.

- Synthetic regressions for determinism, truncated bytecode, and visible indicators.

## Provenance

The Luau architecture draws inspiration from Oracle Decompiler, `xgladius/luauDec`, and `atrexus/unluau`. These are external references. ByteVeil is not an official fork of, and is not affiliated with, those projects.
