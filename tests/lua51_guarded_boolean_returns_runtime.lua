local originalPath, reconstructedPath = unpack(arg)

local cases = {}
for _, isInstance in ipairs({false, true}) do
    for _, gate1 in ipairs({false, true}) do
        for _, gate2 in ipairs({false, true}) do
            for _, accepted in ipairs({false, true}) do
                local trace = {"kind"}
                local expected = false
                if isInstance then
                    trace[#trace + 1] = "gate1"
                    trace[#trace + 1] = "gate1-call"
                    if gate1 then
                        trace[#trace + 1] = "gate2"
                        trace[#trace + 1] = "gate2-call"
                        if gate2 then
                            trace[#trace + 1] = "result"
                            trace[#trace + 1] = "result-call"
                            expected = accepted
                        end
                    end
                end
                cases[#cases + 1] = {
                    isInstance = isInstance,
                    gate1 = gate1,
                    gate2 = gate2,
                    accepted = accepted,
                    expected = expected,
                    trace = trace,
                }
            end
        end
    end
end

local function run(path, case)
    local trace = {}
    local values = {
        kind = case.isInstance and "Instance" or "Other",
        gate1 = function()
            trace[#trace + 1] = "gate1-call"
            return case.gate1
        end,
        gate2 = function()
            trace[#trace + 1] = "gate2-call"
            return case.gate2
        end,
        result = function()
            trace[#trace + 1] = "result-call"
            return case.accepted and "accepted" or "rejected"
        end,
    }
    local environment = {_G = false}
    environment._G = environment
    setmetatable(environment, {
        __index = function(_, key)
            trace[#trace + 1] = key
            return values[key]
        end,
    })
    local chunk = assert(loadfile(path))
    setfenv(chunk, environment)
    local value = chunk()
    return value, trace
end

for index, case in ipairs(cases) do
    local originalValue, originalTrace = run(originalPath, case)
    local reconstructedValue, reconstructedTrace = run(reconstructedPath, case)
    assert(originalValue == case.expected,
        "bytecode result for guarded boolean case " .. index .. ": " ..
        tostring(originalValue) .. " (" .. table.concat(originalTrace, ",") .. ")")
    assert(table.concat(originalTrace, ",") == table.concat(case.trace, ","),
        "bytecode trace for guarded boolean case " .. index .. ": " ..
        table.concat(originalTrace, ","))
    assert(reconstructedValue == originalValue,
        "reconstructed result for guarded boolean case " .. index)
    assert(table.concat(reconstructedTrace, ",") == table.concat(originalTrace, ","),
        "reconstructed trace for guarded boolean case " .. index)
end
