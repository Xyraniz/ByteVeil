local originalPath, reconstructedPath = unpack(arg)

local cases = {
    {guard1 = false, guard2 = true, status = 200, result = 1000, trace = {"warn:rejected"}},
    {guard1 = true, guard2 = false, status = 200, result = 1000, trace = {"warn:rejected"}},
    {guard1 = true, guard2 = true, status = 404, result = 1000, trace = {"status", "warn:rejected"}},
    {guard1 = true, guard2 = true, status = 200, result = "decoded", trace = {"status", "observe"}},
}

for index, case in ipairs(cases) do
    local function run(path)
        local trace = {}
        _G.guard1 = case.guard1
        _G.guard2 = case.guard2
        _G.status = setmetatable({}, {
            __index = function(_, key)
                assert(key == "StatusCode")
                trace[#trace + 1] = "status"
                return case.status
            end,
        })
        _G.warn = function(message)
            trace[#trace + 1] = "warn:" .. message
        end
        _G.observe = function(message)
            assert(message == "decoded")
            trace[#trace + 1] = "observe"
            return message
        end
        local result = assert(loadfile(path))()
        return result, trace
    end

    local originalResult, originalTrace = run(originalPath)
    local reconstructedResult, reconstructedTrace = run(reconstructedPath)
    assert(originalResult == case.result, "bytecode result for shared-return case " .. index)
    assert(table.concat(originalTrace, ",") == table.concat(case.trace, ","),
        "bytecode trace for shared-return case " .. index)
    assert(reconstructedResult == originalResult, "reconstructed result for shared-return case " .. index)
    assert(table.concat(reconstructedTrace, ",") == table.concat(originalTrace, ","),
        "reconstructed trace for shared-return case " .. index)
end
