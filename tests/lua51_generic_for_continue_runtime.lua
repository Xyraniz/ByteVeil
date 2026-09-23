local path = assert(arg[1], "expected reconstructed generic-for path")
local seen, processed = {}, {}

iterator = function(_, control)
    local value = (control or 0) + 1
    if value > 4 then return nil end
    return value, value * 10
end
seenValue = function(value) seen[#seen + 1] = value end
processValue = function(value) processed[#processed + 1] = value end

assert(loadfile(path))()

assert(table.concat(seen, ",") == "1,2,3",
    "continue did not advance to the next generic-for iteration")
assert(table.concat(processed, ",") == "1",
    "break did not exit the generic-for loop after its continue edge")
