local originalPath, reconstructedPath = unpack(arg)

local cases = {}
for _, guard1 in ipairs({false, true}) do
    for _, prepared in ipairs({false, true}) do
        for _, guard2 in ipairs({false, true}) do
            local result = "fallback"
            local trace = {"guard1"}
            if guard1 then
                trace[#trace + 1] = "prepare"
                trace[#trace + 1] = "prepare-call"
                if prepared then
                    trace[#trace + 1] = "guard2"
                    if guard2 then
                        result = "observed"
                        trace[#trace + 1] = "observe"
                        trace[#trace + 1] = "observe-call"
                    else
                        trace[#trace + 1] = "fallback"
                        trace[#trace + 1] = "fallback-call"
                    end
                else
                    trace[#trace + 1] = "fallback"
                    trace[#trace + 1] = "fallback-call"
                end
            else
                trace[#trace + 1] = "fallback"
                trace[#trace + 1] = "fallback-call"
            end
            cases[#cases + 1] = {
                    guard1 = guard1,
                prepared = prepared,
                guard2 = guard2,
                result = result,
                trace = trace,
            }
        end
    end
end

local function run(path, case)
    local trace = {}
    local values = {
        guard1 = case.guard1,
        guard2 = case.guard2,
        prepare = function()
            trace[#trace + 1] = "prepare-call"
            return case.prepared
        end,
        observe = function()
            trace[#trace + 1] = "observe-call"
            return "observed"
        end,
        fallback = function()
            trace[#trace + 1] = "fallback-call"
            return "fallback"
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
    local result = chunk()
    return result, trace
end

for index, case in ipairs(cases) do
    local originalResult, originalTrace = run(originalPath, case)
    local reconstructedResult, reconstructedTrace = run(reconstructedPath, case)
    assert(originalResult == case.result,
        "bytecode result for shared-body case " .. index .. ": " ..
        tostring(originalResult) .. " (" .. table.concat(originalTrace, ",") .. ")")
    assert(table.concat(originalTrace, ",") == table.concat(case.trace, ","),
        "bytecode trace for shared-body case " .. index .. ": " ..
        table.concat(originalTrace, ","))
    assert(reconstructedResult == originalResult,
        "reconstructed result for shared-body case " .. index)
    assert(table.concat(reconstructedTrace, ",") == table.concat(originalTrace, ","),
        "reconstructed trace for shared-body case " .. index)
end
