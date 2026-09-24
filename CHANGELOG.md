# Changelog

## Unreleased - Omit dead Lua 5.1 argument setup registers

### Changed

- Simple `MOVE`, `LOADK`, and `LOADNIL` instructions used only to prepare call
  arguments can now disappear from structured output when their values can be
  rendered in the call and the registers are not captured, read before being
  overwritten, or reached by a control-flow entry. Unsafe and effectful setup
  stays explicit.

### Validation

- Extended runtime differentials to cover a literal argument, a register
  argument, and an effectful argument whose evaluation must follow function
  lookup. All nine CTest suites pass.
- On the 21 MoonSec V3 samples, aggregate output fell by 309,753 bytes to
  3,972,666 bytes. All outputs parse with `luaparse` and compile with Lua 5.1;
  no unsupported markers remain and two bounded dispatchers remain. The
  obfuscated samples were not executed.

## Unreleased - Inline Lua 5.1 table lookup call targets

### Changed

- Calls whose function comes from a global or table lookup can now use that
  lookup directly when the intervening argument instructions are simple,
  side-effect-free loads or moves. Captured call registers, changed lookup
  dependencies, branch entries, open calls, and effectful argument setup stay
  explicit. Fused identifier keys use Lua property syntax where valid.

### Validation

- Added a Lua 5.1 runtime differential regression for `Factory.new(value)` and
  for an effectful argument, checking lookup, evaluation, and call order. The
  existing MOVE-overwritten-register regression also passes on the new output.
- On the 21 MoonSec V3 samples, aggregate output fell by 118,734 bytes to
  4,282,419 bytes and 1,824 global/table calls use a fused target expression.
  All outputs parse with `luaparse` and compile with Lua 5.1; there are no
  unsupported markers and two bounded dispatchers remain. The obfuscated
  samples were not executed.

## Unreleased - Reconstruct Lua 5.1 method calls with simple arguments

### Changed

- `SELF` plus a fixed `CALL` or `TAILCALL` now emits Lua's `object:method(...)`
  form when the intervening instructions only load or move argument registers.
  Branch entries, receiver-register overwrites, and effectful argument setup
  keep their explicit instruction sequence so evaluation order is preserved.

### Validation

- Added a Lua 5.1 runtime differential regression for a method with a register
  argument and for an effectful argument whose evaluation must remain after
  method lookup. All nine CTest suites pass.
- On the 21 MoonSec V3 samples, `:GetService(...)` occurrences rose from 2 to
  367 and aggregate output fell by 313,281 bytes to 4,401,153 bytes. All outputs
  still parse with `luaparse` and compile with Lua 5.1; zero unsupported markers
  and two bounded dispatchers remain. The obfuscated samples were not executed.

## Unreleased - Inline adjacent Lua 5.1 table reads

### Changed

- Structured output now folds adjacent `GETGLOBAL` and `GETTABLE` reads into
  one source expression when no branch can enter the consuming instruction.
  PC-dispatcher output stays instruction-by-instruction.

### Validation

- Added a Lua 5.1 runtime differential regression that checks the lookup
  result, order, and count of `__index` calls. All nine CTest suites pass.
- On the 21 MoonSec V3 samples, all outputs parse with `luaparse` and compile
  with Lua 5.1. Aggregate output fell by 254,848 bytes to 4,714,434 bytes;
  whole-function dispatchers remain at zero and two bounded dispatchers remain
  in root functions. The obfuscated samples were not executed.

## Unreleased - Isolate Lua 5.1 dispatchers to single-exit regions

### Changed

- When structured Lua 5.1 recovery encounters shared branch exits, ByteVeil
  now checks whether the affected control-flow region has one entry and one
  exit. It keeps the PC dispatcher inside that bounded region and reconstructs
  the function prefix and suffix normally. Irregular graphs still use the
  whole-function dispatcher.

### Validation

- Added a Lua 5.1 runtime differential regression for a shared-exit branch
  chain; the output keeps only its isolated dispatcher and matches all three
  tested routes.
- All nine CTest suites pass. On the 21 MoonSec V3 samples, every output parses
  with `luaparse` and compiles with Lua 5.1. Whole-function dispatchers fell
  from two to zero, with isolated dispatchers retained in two root functions;
  output size fell from 5,298,220 to 4,969,282 bytes. No boundary or unsupported
  opcode markers remain. The obfuscated samples were not executed, so these
  checks establish syntax and compilation, not behavioral equivalence.

## Unreleased - Preserve Lua 5.1 repeat latches inside conditional blocks

### Corrected

- Conditional bodies now retain their final backward jump when it forms the
  condition-and-jump latch of a `repeat` loop. The prefix scan also avoids
  lifting a nested `repeat` out of an earlier conditional branch, while still
  allowing completed structured loops in the prefix.

### Validation

- Extended Lua 5.1 runtime differential coverage to a `repeat` loop nested in
  an `if`, checking both flag values and bounds from -1 through 5. The output
  matches the Lua 5.1 runtime and has no PC dispatcher.
- All nine CTest suites pass. On the 21 MoonSec V3 samples, every output parses
  and compiles with Lua 5.1; PC dispatcher comments fell from 178 to **172**
  and aggregate output from 8,390,193 to **8,305,657** bytes. The samples were
  not executed, so parse and compile checks do not establish their behavioral
  equivalence.

## Unreleased - Reconstruct shared-exit Lua 5.1 condition chains

### Changed

- Consecutive Lua 5.1 tests whose false branches share an exit now reconstruct
  as a short-circuit `and` condition when the body has a proven join. Loop
  headers are recognized first so a loop latch is not mistaken for an ordinary
  condition chain.
- Corrected condition negation for already-parenthesized comparisons. The
  previous helper removed their parentheses without inverting the boolean,
  which could reverse a loop exit or `break` condition.

### Validation

- Added Lua 5.1 runtime differential coverage for falsy and truthy `and` chains
  and a `while` loop with a chained condition over inputs 0 through 6. The
  reconstructed code agrees with the original and uses no PC dispatcher.
- All nine CTest suites pass. On the 21 MoonSec V3 samples, every output parses
  and compiles with Lua 5.1; PC dispatcher comments fell from 181 to **178**
  and aggregate output from 8,408,589 to **8,390,193** bytes. The obfuscated
  samples were not executed, so parsing and compilation do not establish their
  behavioral equivalence.

## Unreleased - Preserve Lua 5.1 calls with multiple results

### Corrected

- Direct global-call folding now preserves the destination registers of calls
  that return values. Previously, folding `ipairs(values)` could discard its
  iterator, state, and control values, changing generic-for behavior. Folding
  is limited to matching one-argument tail calls and calls whose results are
  explicitly discarded.

### Validation

- Added a Lua 5.1 runtime differential regression for an `ipairs` loop with an
  early return, including empty, matching, and non-matching inputs. Its
  irreducible control flow remains in the PC dispatcher to preserve semantics.
- All nine CTest suites pass. On the 21 MoonSec V3 samples, every output parses
  and compiles with Lua 5.1; the corpus has 181 PC dispatcher comments and
  totals 8,408,589 bytes. Samples were not executed, so parse and compile checks
  do not establish behavioral equivalence for the obfuscated corpus.

## Unreleased - Reconstruct loop exits and repeat bodies

### Corrected

- Numeric `for`, generic `for`, `while`, and `repeat` bodies now reconstruct
  their `break` jumps as Lua `break` statements. The renderer tracks each
  enclosing loop's exit PC through nested branches, and finds a `repeat`
  latch even when one-time setup instructions precede the loop body.

### Validation

- Added Lua 5.1 runtime differential coverage for all four loop forms,
  including `break` inside a conditional. The reconstructed function matches
  all five return values for inputs from 0 through 6 and uses no PC dispatcher.
- All nine CTest suites pass. On the 21 MoonSec V3 samples, every output parses
  and compiles with Lua 5.1; PC dispatcher comments fell from 183 to **181**
  and aggregate output from 8,516,868 to **8,398,694** bytes. The obfuscated
  samples were not executed, so corpus parsing and compilation do not establish
  behavioral equivalence.

## Unreleased - Reconstruct short-circuit Lua 5.1 condition chains

### Changed

- Reconstructs adjacent comparison and truth-test plus jump pairs as one
  structured `or` condition when each successful branch shares the same body
  and the final false branch reaches the join. This covers Lua 5.1 bytecode
  emitted for conditions such as `a == x or a == y` without a PC dispatcher.

### Validation

- Added Lua 5.1 runtime differential coverage for comparison chains and
  truthiness chains. The test checks both source-equivalent results and that
  the reconstructed function contains no PC dispatcher.
- All nine CTest suites pass. On the 21 MoonSec V3 samples, every output parses
  and compiles with Lua 5.1; PC dispatcher comments fell from 193 to **183**
  and aggregate output from 8,789,379 to **8,516,868** bytes. These corpus
  samples were parsed and compiled, not executed, so this does not establish
  behavioral equivalence for the obfuscated samples.

## Unreleased - Structure captured Lua 5.1 loops

### Changed

- Numeric and generic `for` loops remain structured when their loop variables
  are captured by closures. The renderer creates a fresh captured cell for
  each iteration, preserving Lua 5.1 closure lifetimes while avoiding a
  program-counter dispatcher for these loops.

### Validation

- Added runtime differential regressions where numeric- and generic-for
  closures mutate their captured variables. The reconstructed functions
  produce the same values as the Lua 5.1 originals and contain no PC dispatcher.
- All nine CTest suites pass. On the 21-sample MoonSec V3 corpus, every output
  parses and compiles with Lua 5.1; output size fell from 8,851,997 to
  **8,789,379** bytes and PC dispatcher comments from 201 to **193**. Parse and
  compile checks do not establish behavioral equivalence for the obfuscated
  samples.

## Unreleased - Correct Lua 5.1 JMP field handling

### Corrected

- Removed the incorrect assumption that a nonzero `JMP` A field closes
  upvalues. Lua 5.1.5 ignores that field; only the explicit `CLOSE` opcode
  detaches captured locals. This corrects the earlier claim in this changelog
  that `JMP A>0` performs a close.
- The renderer no longer splits captured cells or forces a dispatcher because
  of the unused field. Control-flow jumps and conditional edges still retain
  their normal Lua 5.1 branch behavior.

### Validation

- Added Lua 5.1 VM differential regressions for explicit `CLOSE`, the unused
  nonzero `JMP A=1` field, and a conditional jump whose two outcomes reach
  different instructions. Each reconstructed closure sequence is compared to
  the same synthetic bytecode executed by Lua 5.1.5, including `SETUPVAL`
  writes. The reader rejects an out-of-range `JMP A` register while accepting
  valid nonzero values.

## Unreleased - Compact Lua 5.1 dispatcher registers

### Changed

- Functions without lexical upvalues now use local `rN` register names even in
  PC-dispatcher output. Functions with captures keep prototype-qualified names
  to avoid shadowing their enclosing upvalues.

### Validation

- Added a regression for compact names in a dispatcher with no upvalues. The
  largest MoonSec sample has 176 prototypes without upvalues; on the full
  corpus, the shorter names reduced source from 9,250,060 to **8,744,996**
  bytes. All 21 outputs still parse and compile with Lua 5.1.


## Unreleased - Cleaner Lua 5.1 closure output

### Changed

- Removed per-capture provenance comments from generated Lua. Capture ownership
  and binding PCs remain available in JSON and disassembly, while source output
  keeps only diagnostics that affect reconstruction.

### Validation

- Re-ran the 21 MoonSec V3 corpus samples through Lua 5.1 parsing and
  compilation after the output change: **21/21 PASS**. A local snapshot of the
  current Synergy output for the same files passed both checks on **15/21**.
- Removed 2,746 generated capture-comment lines and reduced aggregate output
  from 9,604,763 to **9,250,060** bytes. These parser and compiler checks do not
  establish behavioral equivalence for the obfuscated samples.


## Unreleased - Lua 5.1 captured upvalue lifetimes

### Corrected

- Captured Lua 5.1 local registers affected by a close now use shared cells.
  `CLOSE A` detaches captured cells at or above `A`, so closures created before
  register reuse keep their original value while later closures capture the new
  cell. Captures that cannot be closed keep direct lexical references.
- Nested closures pass managed cells through their capture wrappers, keeping
  inherited upvalue reads and writes attached to the same cell.

### Validation

- Added Lua 5.1 runtime regressions for explicit `CLOSE`; closures mutate
  captured upvalues with `SETUPVAL` to verify that cell separation persists
  after the outer function returns.
- Runtime-test discovery now accepts Lua 5.1 `lua`/`lua.exe` and
  `luac`/`luac.exe` names and verifies their version before running. This keeps
  Windows installs from silently skipping runtime coverage due to alias names.
- Added source-to-bytecode differential checks for closures over numeric and
  generic `for` variables; each reconstructed closure returns the same
  per-iteration values as the Lua 5.1 original.
- All 21 MoonSec V3 sample outputs parse and compile with Lua 5.1 `luac -p`;
  this corpus run left **0** explicit-close diagnostics and produced
  **9,604,763** bytes of source. The samples were compiled only, never run.


## Unreleased - Lua 5.1 open SETLIST tails and terminal returns

### Added

- `SETLIST B=0` now consumes an adjacent open-result `CALL` or `VARARG`. The
  generated helper counts and packs every result before writing table indices,
  preserving nil holes as well as fixed values that precede the call.
- Open producer folding is disabled when a control-flow edge can enter the
  consumer without executing that producer.
- The PC dispatcher treats `TAILCALL` as terminal. Its following compiler
  `RETURN` is no longer emitted as a reachable block unless a real branch
  targets it.

### Validation

- Added Lua 5.1.5 execution coverage for open `CALL` and `VARARG` table tails
  with fixed prefixes and nil results, plus a `TAILCALL`/`RETURN` sentinel.
  Full CTest: **9/9 PASS**.
- All **21/21** public MoonSec V3 sample outputs parse as Lua 5.1, with **0**
  remaining open-tail diagnostics. Aggregate output is **9,327,466** bytes.

## Unreleased - Lua 5.1 non-reducible branch recovery

### Added

- When a Lua 5.1 function cannot be reconstructed as structured `if` and loop
  regions, the readable output now uses a stable-register program-counter
  dispatcher over basic blocks. Comparison polarity, tests, conditional
  assignments, jumps, loop transitions, and block fallthroughs remain explicit
  in executable Lua instead of ending at a lost-edge comment.
- Direct method-call syntax is disabled when another control-flow edge can
  enter the `CALL` while skipping its adjacent `SELF`. This keeps that path's
  function and argument registers intact.

### Explicit limit

- The dispatcher is intentionally register-oriented and less idiomatic than
  structured Lua. Open-result flows are folded only when their producer and
  consumer are adjacent and no branch bypasses the producer; other unsupported
  shapes remain visible diagnostics.

### Validation

- Added Lua 5.1.5 execution coverage for nested shared-tail branches and a
  branch that enters `CALL` without running `SELF`. Full CTest: **9/9 PASS**.
- All **21/21** normalized public MoonSec V3 samples still parse as Lua 5.1.
  The pass removed **270** lost-branch markers and **212** unsupported
  comparison/test markers. Aggregate output is **9,330,518** bytes; the first
  per-instruction dispatcher was **28,244,981** bytes on the same corpus.
- At this intermediate revision, open-tail diagnostics remained explicit:
  **10** `CALL` sites, **4** `VARARG` sites, **7** `RETURN` tails, and **10**
  `SETLIST` tails. The follow-up above removes those adjacent supported cases.

## Unreleased - Lua 5.1 method-call recovery

### Added

- A `SELF` followed directly by a no-argument `CALL` or `TAILCALL` with a
  literal identifier key now renders as Lua's `receiver:method()` syntax. The
  same expression is preserved when the method's open results feed the next
  open-argument call.

### Explicit limit

- This fold only applies to adjacent instructions, a fixed receiver register,
  one implicit receiver argument, and a safe literal identifier. Other method
  calls retain explicit register and lookup instructions.

### Validation

- Added Lua 5.1.5 execution regressions for direct colon calls and a method
  whose multiple results feed an open-argument call. Full CTest: **9/9 PASS**.
- On the public MoonSec V3 corpus, **531** eligible call shapes use colon
  syntax and aggregate lifted output fell by **60,976** bytes; all **21/21**
  files still parse as Lua 5.1.

## Unreleased - Lua 5.1 open-arity calls and returns

### Added

- The readable lifter now folds an adjacent open-result `CALL` or `VARARG`
  into a following `CALL`, `TAILCALL`, or `RETURN`. This preserves all values
  in expression forms such as `consume(fixed, produce(...))`, `consume(...)`,
  and `return produce(...)` instead of keeping only one result or dropping
  dynamic arguments.
- Lua 5.1 JSON now records the producer PC for open-argument calls, open
  returns, and open tail calls as well as open `SETLIST` instructions.

### Explicit limit

- The fold requires adjacent producer and consumer instructions and a
  representable register range. Open `SETLIST` tails, separated control-flow
  paths, and unresolved producers remain visible as diagnostics.

### Validation

- Added Lua 5.1.5 execution regressions for nested multi-result calls,
  variadic argument forwarding, and returning every result from an open call.
  Full CTest: **9/9 PASS**.
- The public MoonSec V3 corpus remains **21/21 parseable**. Open-arity
  diagnostics fell from **2,327 to 10**, and aggregate lifted output fell from
  **4,811,490** to **4,662,317** bytes.

## Unreleased - Lua 5.1 conditional and shared-range recovery

### Corrected

- Readable `EQ`, `LT`, and `LE` branches now use operand `A` to choose the
  fallthrough condition that enters the structured body.
- Register value propagation now preserves constants when a forward jump lands
  after their use, while still refusing to fold a value when an edge can skip
  its definition and reach that use.
- A nested conditional that leaves its current reconstruction range no longer
  expands the shared tail inside every enclosing branch. ByteVeil emits the
  in-range path once and marks the escaping edge in the output.

### Validation

- Added binary regressions for both `EQ` polarities and a nested edge to a
  shared tail. The Lua 5.1.5 interpreter compares original and lifted results
  for `-1`, `0`, `1`, and `3`; full CTest: **9/9 PASS**.
- The public MoonSec V3 corpus remains **21/21 parseable**. On
  `23948930.lua`, output fell from **54,156,235** bytes to **124,291** bytes;
  unresolved-opcode markers fell from **20,362** to **38**.

## Unreleased - Lua 5.1 MOVE value snapshots

### Corrected

- Readable Lua 5.1 output no longer substitutes a copied register with the
  source register's newer value after the source has been overwritten. The
  destination register is kept for later uses, including call arguments.

### Validation

- Added a binary Lua 5.1 regression that copies a value, overwrites its source,
  then checks that the call still uses the copied destination. Full CTest:
  **9/9 PASS**.

## Unreleased - Lua 5.1 TESTSET short-circuit recovery

### Corrected

- The readable route now models a forward `TESTSET + JMP` pair. It writes `A = B` only on the VM branch where `truth(B) == C`, takes that pair's jump on the same branch, and emits the fallthrough range as the other branch.
- Register-value propagation now stops at a definition that a previous forward edge can bypass. This avoids turning a path-dependent `TESTSET` result into a false fixed constant at a later `RETURN` or call.

### Explicit limit

- `TESTSET` with a missing/non-`JMP` follower, a backward target, or an unproven join remains a visible diagnostic. It is not treated as a complete general CFG solution.

### Validation

- Added self-contained binary regressions for both `C=0` (`and`-style) and `C=1` (`or`-style) short-circuit forms. Each asserts the conditional write, the fallthrough assignment, the join value, and absence of the unresolved-opcode marker.

## Unreleased - Lua 5.1 CLOSURE capture integrity

### Corrected

- Lua 5.1 `CLOSURE` now consumes its immediately following pseudo-instructions as capture bindings, rather than allowing their `MOVE` or `GETUPVAL` records to appear as independently executed assignments.
- The reader verifies the exact binding count against the child prototype's upvalue count, accepts only `MOVE` (enclosing local) or `GETUPVAL` (enclosing upvalue), checks the captured source index, and rejects jumps into a consumed binding record. Truncated, wrong-kind, out-of-range, and non-boundary cases fail with a function/PC diagnostic.
- Readable nested functions use a separate register namespace and resolve `GETUPVAL`/`SETUPVAL` through the statically proven lexical capture source. Capture provenance remains available in JSON and disassembly metadata.

### Added

- Lua 5.1 JSON exposes the capture list on each `CLOSURE` plus owner-PC and slot metadata on its consumed binding records. The disassembler labels the same relationship.

### Validation

- Added self-contained Lua 5.1 binary regressions for a local capture with an intentionally ignored binding `A` field, a nested inherited-upvalue capture, and malformed truncated, wrong-opcode, and out-of-range capture records.

## Unreleased - Lua 5.1 readable-operation coverage

### Corrected

- The readable lifter now emits `GETUPVAL`, `SETUPVAL`, `SELF`, `VARARG`, full-range `LOADNIL`, `CLOSE`, and fixed-result `CALL` operations instead of omitting them from reconstructed source.
- `LOADBOOL C=1` now skips the following instruction on the readable route, matching the Lua 5.1 VM.
- Open-arity calls, open `SETLIST` tails, unpaired loop instructions, unpaired `TESTSET`, and every other unresolved opcode now carry an explicit PC/operand diagnostic. No reachable instruction is silently discarded by the readable renderer.

### Validation

- Added a self-contained readable-coverage chunk that asserts upvalue access, method-call setup, varargs, `LOADNIL` ranges, and the `LOADBOOL` skip. Full CTest: **9/9 PASS**.
- Re-ran the readable route on three normalized MoonSec V3 trees. All complete in 17–23 ms; remaining non-reducible conditions are now counted and surfaced rather than omitted.

## Unreleased - Lua 5.1 conditional-CFG semantics

### Corrected

- Readable Lua 5.1 conditions now honor the VM's polarity operands: `EQ`, `LT`, and `LE` use `A`, while `TEST` uses `C`. The old renderer treated every comparison as positive, which could invert a lifted branch.
- `TEST + JMP` pairs now participate in conservative `if`, `while`, and `repeat/until` recognition. The readable lifter consequently follows ordinary truthiness-loop back-edges structurally instead of revisiting their entry PC.
- Closed, unconditional entry loops are rendered as `while true do`; a cycle with exits or competing latches remains an explicit diagnostic. CFG diagnostics include the affected nested function ID.

### Validation

- Added a self-contained `TEST`-based `repeat/until` bytecode fixture, alongside the unconditional-loop fixture. Full CTest: **9/9 PASS**.
- On the three normalized MoonSec V3 trees used for regression, all lifts still finish in 19–30 ms. The 858-function tree's visible cycle markers fell from three to zero; remaining complex exits are reported with their function IDs.

## Unreleased - Lua 5.1 generic-loop reconstruction hardening

### Corrected

- The readable Lua 5.1 lifter now recognizes the compiler's `JMP -> TFORLOOP -> backward JMP` generic-`for` CFG shape and renders its iterator triple and all loop result variables as a Lua `for ... in ... do` block.
- An unstructured backward control-flow edge no longer makes `--format lua` spin forever. The renderer emits an explicit `ByteVeil: stopped at repeated control-flow` marker, making the analysis boundary visible to callers.

### Validation

- Added self-contained binary fixtures for a generic `for` loop and a valid self-jump. The generic loop must be reconstructed without a cycle marker; the self-jump must return promptly with a visible marker. Full CTest: **9/9 PASS**.
- Re-ran the readable lifter over normalized Lua 5.1 bytecode obtained from three MoonSec V3 corpus members, including a tree with 858 functions. All three now finish in 21–30 ms instead of hanging on generic-loop back-edges.

## Unreleased - Native MoonSec V3 serialized-bytecode extraction

### Added

- Added a native MoonSec V3 adapter that lexes quoted Lua strings without evaluating the input, decodes the family’s 16-symbol alphabet/nibble transport, and validates the entire serialized prototype tree recursively.
- The `moonsec` format reports the recovered decoder key, source offset, prototype layout, constant-tag mapping, function/instruction/constant counts, and a deterministic FNV-1a checksum. `unpack` automatically returns this richer result when extraction succeeds.
- `moonsec-ir` exposes the complete recovered tree: exact serialized constant bytes, discarded transport slots, virtual opcode numbers, operands, RK flags, child functions, and parameter counts. It explicitly labels the opcode mapping as unresolved.
- `moonsec-bytecode -o FILE` writes the validated pre-devirtualization serialized tree for low-level analysis without invoking LuaDec, UnLuaC, Java, Node.js, or a Lua runtime.
- Added an isolated regression fixture and a CTest suite that verifies successful extraction and the visible failure result for a malformed candidate.

### Explicit limit

- The extracted tree still uses MoonSec’s sample-specific virtual opcode numbers. This change deliberately does not label it as a normal Lua 5.1 chunk or as recovered source; native opcode-handler recovery is the next required stage before ByteVeil’s Lua 5.1 lifter can consume it.

## Unreleased - Lua 5.1 instruction-boundary validation

### Corrected

- Lua 5.1 `SETLIST C=0` now consumes and exposes its following block-number word as `EXTRAARG` instead of decoding it as a second instruction. The reconstructed list index uses that block number and preserves open multiple-return tails.
- Lua 5.1 `TFORLOOP`, comparison skips, and `LOADBOOL` skips now point at their real instruction boundaries; jumps into `SETLIST` extra words are rejected.
- The reader validates opcode values, register/RK/upvalue/constant ranges, call/return/loop result ranges, child-prototype indices, jump targets, line/debug spans, and aggregate prototype/code/constant limits before emitting output.
- CTest now resolves a real Git/MSYS `bash` when Windows' Store WSL alias is first on `PATH`, so Windows paths are translated correctly when tests run from PowerShell or CI.

### Validation

- Added valid `SETLIST`-with-extra-word coverage and malformed Lua 5.1 chunks for out-of-range jumps, constants, registers, and missing extra words. Debug and Release CTest: **8/8 PASS**.

## Unreleased - Binary-safe Lua 5.1 strings and metadata

### Added

- Lua 5.1 JSON now includes typed `constant_table` entries plus exact hexadecimal bytes for string constants, chunk source names, local names, and upvalue names.
- `--dump-constants` recursively lists typed constants for every Lua 5.1 prototype with source-safe values and exact string bytes.

### Corrected

- Lua 5.1 string literals use fixed-width decimal byte escapes accepted by Lua 5.1 instead of non-portable `\xNN` escapes.
- JSON encoding for Lua 5.1 metadata now escapes every control or invalid UTF-8 byte and preserves valid UTF-8, preventing malformed JSON for binary debug strings.
- Invalid or reserved debug identifiers no longer leak into reconstructed source; registers and upvalues fall back to deterministic safe names.
- The Lua 5.1 reader builds cleanly under the repository's warning audit after removing signed-index, unused-variable, and misleading-indentation diagnostics.

### Validation

- Added a self-contained synthetic Lua 5.1 chunk covering quotes, newlines, embedded NUL, control bytes, valid UTF-8, invalid `0xff`, and unsafe local/upvalue/source names without requiring a system Lua installation.

## Unreleased - Optimized-bytecode equivalence matrix

### Added

- The vendored Luau test harness can now emit identity-encoded bytecode at compiler optimization levels 0, 1, and 2 with full debug metadata.
- Behavioral equivalence tests decompile each optimized binary through the real `--bytecode` route and execute the reconstruction, covering optimizer-dependent `FASTCALL`, import, closure, loop, table, vararg, and register-layout choices.

### Validation

- Source reconstruction and all three optimized-bytecode variants produce identical stdout/stderr for the comprehensive Luau fixture.

## Unreleased - Exact constant and prototype metadata

### Added

- JSON functions now expose a typed `constant_table`, exact `string_bytes_hex`, debug-local register/lifetime records, and upvalue names recursively for every prototype.
- `--dump-constants` prints every typed value in every nested function instead of returning only the root count.
- `--dump-prototypes` prints the complete indented function tree with stable IDs, parent/prototype indices, source line, parameters, registers, upvalues, constants, instructions, and child counts.

### Corrected

- JSON string encoding now handles every control character, preserves valid UTF-8, and escapes invalid byte sequences without producing malformed JSON. Exact Luau string bytes remain recoverable from the hexadecimal field.
- Function and source names are constructed with their explicit Luau string lengths instead of assuming null termination.

### Validation

- Added constant/prototype regressions covering nested functions, debug metadata, quotes, newlines, NUL/control bytes, valid UTF-8, and an invalid UTF-8 byte.

## 0.5.0 - Register-state Luau reconstruction

### Added

- A new `RegisterDecompile` pipeline renders complex Luau bytecode as a valid, explicit program-counter state machine when the inherited `luauDec` AST lifter cannot safely structure it.
- The renderer models register values, fixed and multiple-result calls, returns, globals, imports, table reads and writes, arithmetic and logical operations, numeric loops, generic iterators, varargs, method calls, nested prototypes, and by-value/by-reference/upvalue closure captures.
- Risky prototype trees are routed to the state renderer before the legacy lifter can lose loop bodies or closure values. Legacy output remains available automatically for compatible straight-line functions.
- Every reconstructed result is compiled and loaded with the vendored Luau toolchain before ByteVeil returns success.
- A test-only Luau runner executes trusted fixtures with the vendored VM. The equivalence suite compares original and reconstructed output across branches, forward and backward numeric loops, while/repeat loops, generic iteration, table operations, multiple returns, varargs, methods, and captured upvalue mutation.

### Corrected

- Complex Luau samples that previously returned `structurally incomplete output` now produce source-valid, control-flow-complete reconstruction instead of stopping at the integrity guard.
- Captured register cells preserve `false` and `nil` without falling through to stale register storage.

### Validation

- Eight-test CTest suite in Debug and Release: **PASS**.
- Luau behavioral-equivalence fixtures: **PASS**.

## Unreleased - Exact Luau CFG and multi-register dataflow

### Corrected

- Basic blocks now retain ordinary fallthrough edges when a block boundary exists only because another branch targets the next instruction. `RETURN` also terminates and splits blocks, so unreachable trailing code no longer contaminates the reachable CFG.
- Generic-for preparation and `LOADB` skips are modeled as unconditional transfers; conditional jumps, loop latches, and `FASTCALL*` retain both paths.
- Comparison instructions read their second register from AUX, not the unrelated `B` byte. `SETGLOBAL`, `SETUPVAL`, `SETTABLE*`, `SETLIST`, captures, fast calls, and variable ranges now expose their actual register uses.
- `CALL`, `NAMECALL`, `GETVARARGS`, numeric loops, and generic loops record every register they define. Liveness, definition counts, and SSA consume the full definition set instead of assuming every opcode writes only register A.
- SSA assigns a deterministic version to each individual register definition. JSON includes `definitions`, `definition_versions`, the raw `aux` value, and block `reachable` state while keeping the original primary `destination_register` field for compatibility.
- Function IDs are allocated recursively without collisions between a nested prototype and a later sibling.
- Natural loops with multiple latches are merged by header, loop scopes contain register sets instead of block IDs, and backward per-block liveness peaks are computed from `live_out` with definition kills.
- Bytecode validation now distinguishes decoded instruction boundaries from requested jump targets, rejecting jumps into AUX words. AUX constants/registers and multi-register loop/name-call ranges receive explicit bounds checks.
- MinGW builds link the GCC and C++ runtimes statically. This prevents Git Bash/MSYS from loading an ABI-incompatible `libstdc++-6.dll` from `PATH`, a failure that only reproduced in optimized builds launched from Bash.

### Validation

- Added a semantic IR regression covering table/global/upvalue writes, AUX comparisons, multi-result calls, method calls, varargs, numeric and generic loops, nested prototypes, CFG reciprocity/reachability, unique function IDs, and SSA definition alignment.
- Seven-test CTest suite in both Debug and Release: **PASS**.

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
