local originalPath, reconstructedPath = unpack(arg)

local cases = {
    {gate = false, matches = true, result = "tail", trace = {"else", "observe"}},
    {gate = true, matches = false, result = "tail", trace = {"compare", "observe"}},
    {gate = true, matches = true, result = true, trace = {"compare"}},
}

for index, case in ipairs(cases) do
    local function run(path)
        local trace = {}
        _G.gate = case.gate
        _G.compare = function()
            trace[#trace + 1] = "compare"
            return case.matches
        end
        _G.onElse = function()
            trace[#trace + 1] = "else"
        end
        _G.observe = function()
            trace[#trace + 1] = "observe"
            return "tail"
        end
        local result = assert(loadfile(path))()
        return result, trace
    end

    local originalResult, originalTrace = run(originalPath)
    local reconstructedResult, reconstructedTrace = run(reconstructedPath)
    assert(originalResult == case.result, "bytecode result for shared-else case " .. index)
    assert(table.concat(originalTrace, ",") == table.concat(case.trace, ","),
        "bytecode trace for shared-else case " .. index)
    assert(reconstructedResult == originalResult, "reconstructed result for shared-else case " .. index)
    assert(table.concat(reconstructedTrace, ",") == table.concat(originalTrace, ","),
        "reconstructed trace for shared-else case " .. index)
end
