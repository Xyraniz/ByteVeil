-- Synthetic MoonSec V3 alphabet/nibble payload.  The decoded bytes contain
-- one serialized Lua 5.1 prototype with a single virtual RETURN instruction.
-- It is deliberately not executable Lua; the adapter must only lex and
-- validate it without evaluating the file.
([[This file was protected with MoonSec V3]]):gsub('.', function() end)
local serialized = "0123456789abcdeffcf2ebe4ddd7d1c8c1bab3aca58ed6938b827b74dcd16058514a435a352e272119120b04fdf6efe8"
