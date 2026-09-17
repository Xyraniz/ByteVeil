# Third-party notices

## Luau

ByteVeil includes the Luau source tree required to build the compiler, VM, AST, and analysis libraries offline. Luau is distributed under the MIT license and related notices included in `luau/LICENSE.txt` and `luau/lua_LICENSE.txt`.

## Design references

ByteVeil was inspired by the public design and implementation ideas of the following projects:

- Oracle Decompiler
- https://github.com/xgladius/luauDec
- https://github.com/atrexus/unluau
- https://github.com/viruscamp/luadec
- https://github.com/ItsLucas/luadecng-rs
- https://github.com/luau-lang/luau

The reference repositories are **not bundled in this distribution**. Their names are retained only as attribution for the architectural and research inspiration described in the README. ByteVeil is not an official fork or affiliated project.

The CFG, dominator, liveness, and intermediate-representation concepts were
reviewed against luadecng-rs and Luau's public bytecode implementation. No
code from those repositories is copied into ByteVeil; this remains a separate
C++17 implementation under the applicable project licenses.

## ByteVeil changes

This distribution provides the ByteVeil CLI, static format and loader-marker inspection, source-to-bytecode compilation, bytecode loading, block lifting, and Lua output generation. It also removes debug instruction dumps so generated output is not mixed with diagnostics.
