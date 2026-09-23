local path = assert(arg[1], "expected reconstructed nested generic-for path")

local function run(enabledValue, expected)
    local seen = {}
    enabled = enabledValue
    iterator = function(_, control)
        local value = (control or 0) + 1
        if value > 3 then return nil end
        return value, value * 10
    end
    seenValue = function(value) seen[#seen + 1] = value end

    assert(loadfile(path))()
    assert(table.concat(seen, ",") == expected,
        "nested generic-for iteration differs for enabled=" .. tostring(enabledValue))
end

run(true, "1,2,3")
run(false, "")
