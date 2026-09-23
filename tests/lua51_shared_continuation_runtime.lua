local originalPath, reconstructedPath = unpack(arg)

local cases = {
    {outer = true, inner = false, trace = {"outer", "thenAction", "then-call", "continuation", "continuation-call"}},
    {outer = true, inner = true, trace = {"outer", "thenAction", "then-call", "continuation", "continuation-call"}},
    {outer = false, inner = false, trace = {"outer", "inner"}},
    {outer = false, inner = true, trace = {"outer", "inner", "elseAction", "else-call"}},
}

local function run(path, case)
    local trace = {}
    local values = {
        outer = case.outer,
        inner = case.inner,
        thenAction = function()
            trace[#trace + 1] = "then-call"
        end,
        elseAction = function()
            trace[#trace + 1] = "else-call"
        end,
        continuation = function()
            trace[#trace + 1] = "continuation-call"
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
    assert(originalResult == nil, "bytecode result for shared continuation case " .. index)
    assert(table.concat(originalTrace, ",") == table.concat(case.trace, ","),
        "bytecode trace for shared continuation case " .. index .. ": " ..
        table.concat(originalTrace, ","))
    assert(reconstructedResult == originalResult,
        "reconstructed result for shared continuation case " .. index)
    assert(table.concat(reconstructedTrace, ",") == table.concat(originalTrace, ","),
        "reconstructed trace for shared continuation case " .. index .. ": " ..
        table.concat(reconstructedTrace, ","))
end
