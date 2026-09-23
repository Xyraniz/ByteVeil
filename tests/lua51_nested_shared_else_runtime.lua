local originalPath, reconstructedPath = unpack(arg)

local cases = {
    {outer = false, inner = false, expected = {"fallback"}},
    {outer = true, inner = false, expected = {"fallback"}},
    {outer = true, inner = true, expected = {"body"}},
}

for index, case in ipairs(cases) do
    local function run(path)
        local trace = {}
        _G.outer = case.outer
        _G.inner = case.inner
        _G.body = function() trace[#trace + 1] = "body" end
        _G.fallback = function() trace[#trace + 1] = "fallback" end
        assert(loadfile(path))()
        return trace
    end

    local originalTrace = run(originalPath)
    local reconstructedTrace = run(reconstructedPath)
    assert(table.concat(originalTrace, ",") == table.concat(case.expected, ","),
        "source shared-else case " .. index .. " differs")
    assert(table.concat(reconstructedTrace, ",") == table.concat(originalTrace, ","),
        "reconstructed shared-else case " .. index .. " differs")
end
