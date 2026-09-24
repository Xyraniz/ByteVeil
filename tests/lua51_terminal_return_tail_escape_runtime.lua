local originalPath, reconstructedPath = unpack(arg)

local cases = {
    {mode = "match", inner = true, result = "local", trace = {"mode", "inner", "local", "local-call"}},
    {mode = "match", inner = false, result = "tail", trace = {"mode", "inner", "tail", "tail-call"}},
    {mode = "other", inner = true, result = "other", trace = {"mode", "other", "other-call"}},
}

local function run(path, case)
    local trace = {}
    local values = {
        mode = case.mode,
        inner = case.inner,
        ["local"] = function()
            trace[#trace + 1] = "local-call"
            return "local"
        end,
        other = function()
            trace[#trace + 1] = "other-call"
            return "other"
        end,
        tail = function()
            trace[#trace + 1] = "tail-call"
            return "tail"
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
    return chunk(case.mode), trace
end

for index, case in ipairs(cases) do
    local originalResult, originalTrace = run(originalPath, case)
    local reconstructedResult, reconstructedTrace = run(reconstructedPath, case)
    assert(originalResult == case.result,
        "bytecode result for terminal-tail case " .. index .. ": " .. tostring(originalResult))
    assert(table.concat(originalTrace, ",") == table.concat(case.trace, ","),
        "bytecode trace for terminal-tail case " .. index .. ": " ..
        table.concat(originalTrace, ","))
    assert(reconstructedResult == originalResult,
        "reconstructed result for terminal-tail case " .. index)
    assert(table.concat(reconstructedTrace, ",") == table.concat(originalTrace, ","),
        "reconstructed trace for terminal-tail case " .. index .. ": " ..
        table.concat(reconstructedTrace, ","))
end
