local path = assert(arg[1], "expected reconstructed guarded-chain path")

local cases = {
    {
        values = {gate1 = true, candidate1 = "first", gate2 = true,
            candidate2 = "second", fallback = "fallback"},
        expected = "first",
        trace = {"gate1", "candidate1", "candidate1:available", "observe"},
    },
    {
        values = {gate1 = true, candidate1 = false, gate2 = true,
            candidate2 = "second", fallback = "fallback"},
        expected = "second",
        trace = {"gate1", "candidate1", "candidate1:available", "gate2",
            "candidate2", "candidate2:available", "observe"},
    },
    {
        values = {gate1 = false, candidate1 = "unused", gate2 = false,
            candidate2 = "unused", fallback = "fallback"},
        expected = "fallback",
        trace = {"gate1", "gate2", "fallback", "observe"},
    },
}

for index, case in ipairs(cases) do
    local trace = {}
    local observed
    local environment = {}
    environment._G = environment
    setmetatable(environment, {
            __index = function(_, key)
                trace[#trace + 1] = key
                if key == "observe" then
                    return function(value) observed = value end
                end
                if key == "candidate1" or key == "candidate2" then
                    local value = case.values[key]
                    return setmetatable({}, {
                        __index = function(_, field)
                            trace[#trace + 1] = key .. ":" .. field
                            assert(field == "available", "candidate used the wrong property key")
                            return value
                        end,
                    })
                end
                return case.values[key]
        end,
    })

    local chunk = assert(loadfile(path))
    setfenv(chunk, environment)
    chunk()

    assert(observed == case.expected,
        "guarded short-circuit chain selected the wrong value in scenario " .. index)
    assert(table.concat(trace, ",") == table.concat(case.trace, ","),
        "guarded short-circuit chain changed lazy lookup order in scenario " .. index ..
        ": " .. table.concat(trace, ","))
end
