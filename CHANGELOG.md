# Changelog

## 0.4.8 — Endurecimiento del lifter Luau (BlockGen/AstGen) y más cobertura de opcodes

Motivado por pruebas dirigidas contra una muestra real de MoonSec V3
(`Xyraniz/Obfuscator-Samples`, `Moonsec/v3/323928.lua`) usando `--format lua`,
que antes de estos cambios crasheaba o colgaba el proceso de forma
reproducible. El código base de `BlockGen`/`AstGen` es una copia casi
literal de `xgladius/luauDec`; se comparó línea por línea contra el
repositorio upstream para confirmar que estos bugs no son introducidos por
ByteVeil, sino preexistentes en el lifter vendorizado, y se corrigieron
localmente.

### Corregido (crashes/cuelgues confirmados con gdb, no solo teóricos)

- `functionArgs->at(B)` en `LOP_MOVE` lanzaba `std::out_of_range`: el vector
  solo tiene tamaño `proto->numparams`, pero `MOVE` puede referenciar
  cualquier registro local. Ahora tiene bounds-check.
- `BlockGen<false>` (cualquier función que no es la raíz) nunca generaba su
  propio `subFuncs`: una closure definida dentro de otra closure (común en
  VMs de obfuscador) indexaba un vector vacío en `LOP_NEWCLOSURE` →
  segfault. Ahora cada nivel de anidamiento genera sus propios hijos
  recursivamente, igual que ya hacía `Decompile.cpp` para el proto raíz.
- `VirtualAstStack` indexaba su vector interno de 256 registros con
  `operator[]` crudo, sin bounds-check. Cuando el flujo de bytecode se
  desincroniza de los límites reales de instrucción, los índices de
  registro decodificados pueden salirse muchísimo del rango (observado:
  `idx=7074`) → UB/segfault. Ahora fuera de rango devuelve "sin valor" en
  vez de crashear.
- Integer underflow en `getCallAst`/`genTableAst`: `virtualStack.getTop()`
  puede devolver `-1` legítimamente; al mezclarse con una comparación
  `unsigned` en el límite del loop, se envolvía a ~4 mil millones,
  colgando el proceso en un loop casi infinito. Ahora `argCount` se acota
  a un rango sensato antes de usarse como límite.
- `AstStatWhileGenerator::condition` no se inicializaba en el constructor
  y, si `updateCondition()` nunca llegaba a invocarse (control flow que
  este lifter no modela con precisión), se incrustaba un `AstExpr*` nulo o
  con memoria sin inicializar en el `AstStatWhile` final, crasheando el
  *printer* de Luau al imprimirlo. Ahora se inicializa a `nullptr` y
  `generate()` sustituye un placeholder visible si nunca se actualizó.
- `handleAllInstructions()` asumía que `bodyHandler.get()->as<AstStatBlock>()`
  siempre tiene éxito; en control flow que la máquina de estados de
  `BodyHandler` no modela limpiamente, `as<>()` puede devolver `nullptr`,
  que se incrustaba como cuerpo de función y crasheaba el *printer*. Ahora
  hay un fallback a un bloque vacío con un comentario visible.
- Varios sitios en `Handlers.cpp`/`BlockGen.cpp` (llamadas, operadores
  aritméticos y unarios, `JUMPIF*`, `FORNPREP`, tablas, `MOVE`) asumían que
  un registro virtual siempre tiene un valor; un opcode no cubierto podía
  dejar el registro en `nullptr` y el uso posterior (`->is<>()`, `->as<>()`,
  o incrustarlo directo en un nodo AST) crasheaba. Nuevo helper
  `orPlaceholder()` aplicado en todos esos sitios: un registro sin resolver
  ahora se ve como `'--[[ byteveil: unresolved register ]]'` en el output
  en vez de crashear el proceso completo.
- `GETGLOBAL`/`SETGLOBAL`/`GETTABLEKS`/`NAMECALL`/`LOADK` asumían sin
  comprobar que el índice de constante (`aux` o `D`) siempre cae dentro de
  `proto->sizek` y siempre es un string. Nuevo helper `getConstantName()`
  con bounds-check y chequeo de tipo (`tt == LUA_TSTRING`); fuera de rango
  o de tipo incorrecto ahora produce un placeholder legible
  (`?byteveil_bad_const_index?` / `?byteveil_non_string_const?`) en vez de
  leer memoria fuera de límites.

### Añadido

- Cobertura de opcodes Luau ampliada en el lifter de alto nivel (antes
  cubría ~35 de ~80 opcodes definidos): `LOADNIL`, `LOADN`, `LOADB` (estaba
  deshabilitado/comentado en el código heredado), `NOT`, `MINUS`,
  `LENGTH`, `CONCAT`, `DUPCLOSURE` (closures sin upvalues, resuelto
  localizando el `Proto*` objetivo dentro de `proto->p`) y `GETIMPORT`
  (cadenas de acceso a globales tipo `string.byte`, decodificando los
  hasta 3 índices de constante empaquetados de 10 bits en la palabra
  `aux`, según `BytecodeBuilder::getImportId`).
- `--timeout SEGUNDOS` (default 20, `0` = sin límite) en el CLI: abortan
  limpiamente los decompiles que excedan el límite en vez de colgar el
  proceso indefinidamente. Es una salvaguarda adicional sobre las
  correcciones anteriores, no un sustituto: cualquier input adicional
  igual de patológico que aún no se haya probado queda cubierto igual.
- Límite de espacio de direcciones (2 GiB) antes de invocar el
  decompilador Luau: una asignación descontrolada ahora falla de forma
  predecible como `std::bad_alloc` (ya capturado por el `try/catch`
  existente) en vez de terminar el proceso con SIGBUS/SIGSEGV por fallo
  del allocator.

### Verificación

- Build CMake Release y Debug: **PASS**.
- Suite CLI y Lua 5.1 existentes (`tests/test_cli.sh`, `tests/test_lua51.sh`): **PASS**, sin regresiones.
- Comparación línea por línea contra `xgladius/luauDec` (commit vigente al momento de este cambio) para separar bugs heredados de cambios propios de ByteVeil.
- Muestra real `Moonsec/v3/323928.lua` (Xyraniz/Obfuscator-Samples) con `--format lua`: antes crasheaba/colgaba de forma reproducible (confirmado con gdb en cada paso); ahora termina limpiamente y produce output parcial honesto, con placeholders explícitos en las partes que el lifter no pudo resolver.
- Regresión con snippets Lua cotidianos (closures, upvalues, concatenación, tablas, `for` numérico, llamadas a `print`/`ipairs`, POO con `setmetatable`/`:método`, `pairs`, `pcall`): build original (sin parchear) crasheaba con excepción incluso en el snippet más simple con closures; build parcheado ya no crashea en ningún caso probado.

### Límites explícitos

- El lifter de alto nivel para Luau sigue siendo **experimental**, como ya
  advertía el README: sigue sin cubrir el 100% de los opcodes de Luau
  (`GETTABLE`/`SETTABLE` genérico, `FASTCALL*`, `FORGPREP`/`FORGLOOP`
  genérico para `pairs`/`ipairs`, `GETUPVAL`/`SETUPVAL` en todos los
  casos, etc.), y estas correcciones priorizan **nunca crashear ni
  colgarse** por encima de una reconstrucción 100% fiel del código fuente.
  Cualquier parte no cubierta se marca explícitamente con un placeholder
  en vez de fabricarse o inventarse.
- La muestra de MoonSec V3 sigue sin "desofuscarse" en el sentido de
  recuperar el código original antes de la protección: el output es una
  representación parcial y honesta del bytecode compilado del VM
  interpreter, coherente con el propio alcance del proyecto (detector +
  lifter estático, nunca un ejecutor).

## 0.4.7 — Análisis avanzado y metadata de estructura

### Añadido

- Diagnóstico de aliasing conservador basado en accesos y asignaciones de tabla.
- Detección de solapamientos de scopes a partir de intervalos de locals.
- Conteo de sitios de metamétodos dinámicos mediante opcodes de tabla y constantes `__index`, `__newindex` y `__call`.
- Análisis SCC de Tarjan para detectar ciclos y loops que no deben estructurarse como `if`/`while` simples.
- Tres adaptaciones inspiradas en unluac: nombres e intervalos de locals, mapeo de `lineinfo` por PC y normalización de branches para el análisis de ciclos.
- JSON ampliado con `locals`, `upvalues`, `lines`, línea por instrucción y bloque `analysis`.

### Verificación

- Build CMake Release y smoke de versión: **PASS**.
- Metadata debug y análisis avanzado en fixture con tabla, closure y loop: **PASS**.
- Corpus Lua 5.1 existente: **18/18** procesado a JSON.

### Límites explícitos

- Los análisis detectan riesgos y ciclos; no los presentan como equivalencia semántica resuelta.
- El lifting de metamétodos calculados, aliasing complejo y scopes con escapes sigue requiriendo validación conductual por fixture.

## 0.4.6 — Closures, upvalues, retornos múltiples y extracción literal

### Añadido

- El lifter Lua 5.1 representa accesos a upvalues, asignaciones a upvalues, closures anidadas, parámetros vararg, `VARARG`, llamadas con arity abierta y retornos múltiples básicos.
- Los accesos de tabla y asignaciones conservan la forma indexada para no ocultar metamétodos detrás de una simplificación incorrecta.
- Nuevo modo `--format unpack`, que identifica la familia visible y extrae solamente payloads literales de `loadstring`, sin ejecutar código.
- Adaptadores de extracción estática para MoonSec V3, Luraph y cargadores genéricos; los payloads dinámicos quedan identificados de forma explícita.

### Verificación

- Build CMake Release: correcto.
- Suite CLI y Lua 5.1: **PASS**.
- Fixture con closure, upvalue y vararg: parseado y levantado correctamente.
- JSON conserva prototipos anidados y metadata de `SETLIST B=0`.
- Extracción literal `loadstring`: validada; loaders dinámicos quedan diagnosticados como no extraíbles.

### Límites explícitos

- La equivalencia completa todavía requiere validar cada fixture con el arnés; el lifter no garantiza semántica completa para aliasing complejo, metamétodos, loops irreducibles o scopes difíciles.
- `unpack` no ejecuta ni resuelve VMs virtualizadas, blobs calculados ni payloads remotos. No se presenta un detector como desempaquetador universal.

## 0.4.5 — Lifting, CFG estructurado, SETLIST y detectores

### Añadido

- Lifter Lua 5.1 funcional para `MOVE`, `LOADK`, `LOADBOOL`, `LOADNIL`, `GETGLOBAL`, `NEWTABLE`, aritmética, concatenación, negación, longitud, llamadas y retornos. Los opcodes no cubiertos se conservan con PC y operandos en comentarios, sin generar código inventado.
- Metadata JSON y disassembly para `SETLIST B=0`, incluyendo `open_tail` y el PC del `CALL`, `TAILCALL` o `VARARG` productor más cercano.
- Modo `--format structured`, que genera una representación Lua estructurada por estados con contador de PC y transiciones explícitas.
- Detector estático `--format protectors` con JSON de evidencia para MoonSec V3, Luraph, cargadores dinámicos, APIs de executor/Roblox, patrones de dispatcher y blobs codificados.
- Arnés `tests/equivalence_lua51.sh`, que compara código de salida, stdout y stderr de dos programas bajo Lua 5.1.

### Verificación

- Build CMake Release: correcto.
- `tests/test_cli.sh`: **PASS**.
- `tests/test_lua51.sh`: **PASS**.
- Arnés de equivalencia con fixture idéntico: **PASS**.
- 18/18 chunks Lua 5.1 derivados del corpus MoonSec procesados a JSON sin abortos.
- Smoke de `--format structured` y `--format protectors`: **PASS**.

### Límites explícitos

- El lifter todavía es parcial para closures, upvalues, varargs abiertos, metamétodos, loops complejos y múltiples retornos.
- `SETLIST B=0` ya se detecta y conserva su productor; la expansión completa de arity dinámica aún requiere un paso posterior.
- La salida estructurada conserva el CFG y transiciones, pero no siempre produce Lua idiomático.
- Los detectores de protectores son análisis estático, no desempaquetadores; cada familia necesita un desvirtualizador validado con muestras propias.

## 0.4.0 — Lector Lua 5.1 y detección automática

### Añadido

- Lector binario Lua 5.1 autocontenido en `byteveil_decompiler/Lua51.cpp` y `Lua51.h`.
- Validación de firma, versión, formato, endianess, tamaños de tipos, límites de prototipos, registros, constantes, instrucciones, strings, lineinfo, locals y upvalues.
- Soporte de constantes Lua 5.1 `nil`, booleanas, números y strings.
- Recorrido de prototipos hijos con IDs deterministas y relaciones padre/hija.
- Decodificación de instrucciones Lua 5.1 con operandos ABC, Bx y sBx.
- Detección de destinos de salto y generación de CFG DOT para Lua 5.1.
- JSON/IR determinista para chunks Lua 5.1.
- Disassembly Lua 5.1 con nombres de opcode y operandos.
- Salida Lua diagnóstica sintácticamente válida que conserva el listing cuando todavía no es posible hacer lifting semántico.
- Detección automática de `\x1bLua` frente a `\x1bLuau` en el CLI.
- Captura de excepciones del decompiler Luau para convertir fallos en errores controlados.
- Fixture binario real `tests/fixtures/lua51-sample.luac` y suite `tests/test_lua51.sh`.

### Verificación

- Build CMake Release: correcto.
- Suite CLI existente: **PASS**.
- Suite Lua 5.1: **PASS**.
- Fixture sintético Lua 5.1: JSON, prototipos, disassembly, CFG y salida diagnóstica correctos.
- Corpus derivado MoonSec V3: **18/18 chunks Lua 5.1** procesados correctamente a JSON.
- Los chunks Lua 5.1 ya no se envían por error al loader Luau.
- Entradas truncadas y formatos incompatibles producen errores controlados.

### Limitaciones explícitas

- La ruta Lua 5.1 todavía es una representación estructural y diagnóstica; `--format lua` no pretende ser un decompiler semántico completo.
- Faltan lifting de alto nivel Lua 5.1, recuperación de SETLIST abierto, estructuración avanzada de CFG irreducible, equivalencia conductual y desvirtualización específica de protectores.
- La ruta Luau de alto nivel continúa siendo experimental.

## 0.3.0

- IR inicial determinista para Luau con metadatos de funciones, instrucciones, bloques básicos, sucesores de CFG y validación de invariantes.
- JSON, disassembly, CFG y análisis estático sin ejecución.
- Regresiones sintéticas para determinismo, bytecode truncado e indicadores visibles.

## Procedencia

La arquitectura Luau toma inspiración de Oracle Decompiler, `xgladius/luauDec` y `atrexus/unluau`. Son referencias externas; ByteVeil no es un fork oficial ni está afiliado a esos proyectos.
