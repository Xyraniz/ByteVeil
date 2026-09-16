# ByteVeil

**ByteVeil 0.4.8** es un decompilador e inspector estático autónomo para Lua 5.1 y Luau. Mantiene rutas separadas para bytecode Luau y chunks Lua 5.1, con detección automática de formato. Esta versión endurece el lifter Luau de alto nivel (heredado de luauDec) contra crashes y cuelgues encontrados al probarlo contra una muestra real de MoonSec V3, y amplía su cobertura de opcodes. Ver CHANGELOG.md para el detalle completo.

## Construcción

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
```

Requiere CMake 3.20 o posterior y un compilador C++17. El árbol Luau necesario está incluido bajo `luau/`.

## Uso

```bash
./build/byteveil --version
./build/byteveil --bytecode sample.luac --format json
./build/byteveil --bytecode sample.luac --disassemble
./build/byteveil --bytecode sample.luac --cfg graph.dot
./build/byteveil --bytecode sample.luac --format structured
./build/byteveil --bytecode sample.luac --format lua -o lifted.lua
./build/byteveil --format protectors script.lua
./build/byteveil --format unpack script.lua
./build/byteveil --format json script.luau
./build/byteveil --analyze script.lua
```

El formato se detecta por firma: `\x1bLua` activa Lua 5.1 y `\x1bLuau` activa Luau. La entrada fuente no binaria se compila como Luau en la ruta Luau. `--format protectors` devuelve JSON de indicadores estáticos; nunca ejecuta ni desempaqueta la entrada.

## Lifting Lua 5.1

La ruta `--format lua` emite código Lua 5.1 para instrucciones comunes como `MOVE`, `LOADK`, `LOADBOOL`, `LOADNIL`, accesos a upvalues y globals, tablas, operaciones aritméticas, concatenación, negación, longitud, closures, llamadas, `VARARG` y retornos múltiples. Las instrucciones no cubiertas se conservan como comentarios con PC y operandos, y la salida termina de forma sintácticamente válida. Esto es un lifter ampliado verificable, no una promesa de equivalencia para cualquier chunk.

Los modos `json` e `ir` exponen prototipos anidados, parámetros, registros, constantes, instrucciones, saltos y la metadata `open_tail`/`open_producer_pc` de `SETLIST B=0`. `--disassemble` muestra operandos ABC, Bx y sBx y marca el productor de un tail abierto.

## CFG irreducible y SETLIST abierto

`--format structured` genera una representación Lua estructurada por estados, con un contador de PC y transiciones explícitas para cada bloque e instrucción. Esta ruta evita inventar un `if` o `while` cuando el CFG no tiene una forma reducible segura. `SETLIST B=0` se identifica en el lector y se relaciona con el `CALL`, `TAILCALL` o `VARARG` abierto anterior; esa metadata constituye la base para una recuperación semántica posterior y evita perder el tail durante el análisis.

## Protectores

El detector `--format protectors` busca evidencia estática de MoonSec V3, Luraph, cargadores dinámicos, APIs de executor/Roblox, patrones de dispatcher y blobs codificados. Devuelve familia, confianza y evidencia. Es deliberadamente un detector, no un desempaquetador: cada protector necesita un desvirtualizador independiente validado con fixtures propios.

## Equivalencia conductual

`tests/equivalence_lua51.sh ORIGINAL.lua CANDIDATE.lua` ejecuta ambos programas con Lua 5.1 y compara código de salida, stdout y stderr. El arnés es funcional y sirve para validar cada lifting; no ejecuta chunks automáticamente durante el análisis normal de ByteVeil.

## Extracción por familia

`--format unpack` identifica la familia visible y extrae únicamente payloads que sean literales de `loadstring`. El resultado es JSON, marca `executed: false` y conserva el payload para que pueda validarse con el lifter y el arnés de equivalencia. No ejecuta loaders, VMs ni código de red; cuando el payload está calculado dinámicamente devuelve `no-literal-payload` en lugar de inventar una extracción.

## Análisis avanzado y adaptaciones de unluac

El JSON Lua 5.1 incluye `analysis.alias_hazards`, `scope_overlaps`, `dynamic_metamethod_sites` e `irreducible_or_loop_sccs`. Estos valores proceden de instrucciones, constantes, intervalos de debug y un recorrido Tarjan de la gráfica de branches; son diagnósticos conservadores, no afirmaciones de equivalencia.

La metadata `locals` conserva nombre, `start_pc` y `end_pc`; `lines` conserva el mapeo PC-línea; y cada instrucción incluye `line`. Estas tres capacidades se adaptan de la información que unluac utiliza en sus fases de naming y structure, pero se mantienen dentro de la IR C++ de ByteVeil sin copiar su pipeline Rust.

## Validación

```bash
./tests/test_cli.sh ./build/byteveil
./tests/test_lua51.sh ./build/byteveil
./tests/equivalence_lua51.sh original.lua candidate.lua
```

La validación local incluye las suites CLI y Lua 5.1, el arnés de equivalencia y **18/18** chunks Lua 5.1 derivados del corpus MoonSec V3 procesados a JSON sin abortos.

## Limitaciones conocidas

El lifter representa closures, upvalues, varargs y retornos múltiples básicos, pero todavía no cubre toda su semántica en presencia de aliasing complejo, `SETLIST` con arity dinámica completa, metamétodos calculados, loops irreducibles y scopes difíciles. El análisis SCC detecta y cuenta ciclos; no los convierte automáticamente en Lua idiomático. El desempaquetado automático sigue limitado a payloads literales: los protectores que calculan o virtualizan el payload requieren adaptadores específicos adicionales. La ruta Luau de alto nivel continúa siendo experimental.

## Licencias

Consulta `THIRD_PARTY_NOTICES.md` y las licencias bajo `luau/` para la procedencia y condiciones de redistribución.
