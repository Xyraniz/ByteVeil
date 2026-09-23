local originalPath, reconstructedPath = unpack(arg)
local originalClassify, originalChoose, originalCompareWithCalls = assert(loadfile(originalPath))()
local reconstructedClassify, reconstructedChoose, reconstructedCompareWithCalls = assert(loadfile(reconstructedPath))()

for _, value in ipairs({"alpha", "beta", "gamma", "delta", "", 0}) do
    assert(reconstructedClassify(value) == originalClassify(value),
        "reconstructed comparison chain differs for " .. tostring(value))
end

local scenarios = {
    {false, false, false},
    {false, "fallback", false},
    {false, false, true},
    {0, false, false},
}
for index, values in ipairs(scenarios) do
    assert(reconstructedChoose(values[1], values[2], values[3]) ==
        originalChoose(values[1], values[2], values[3]),
        "reconstructed truth-test chain differs in scenario " .. index)
end

local callCases = {
    {1, 1, "left", "right", {first = "left", second = "right"}, true, 2},
    {1, 2, "left", "right", {first = "left", second = "right"}, false, 0},
    {1, 1, "wrong", "right", {first = "left", second = "right"}, false, 1},
    {1, 1, "left", "wrong", {first = "left", second = "right"}, false, 2},
}
for index, case in ipairs(callCases) do
    local originalCalls, reconstructedCalls = {}, {}
    local function probe(values, calls)
        return function(name)
            calls[#calls + 1] = name
            return values[name]
        end
    end
    local originalResult = originalCompareWithCalls(case[1], case[2], case[3], case[4], probe(case[5], originalCalls))
    local reconstructedResult = reconstructedCompareWithCalls(case[1], case[2], case[3], case[4], probe(case[5], reconstructedCalls))
    assert(originalResult == case[6], "source comparison chain with calls case " .. index .. " differs")
    assert(reconstructedResult == originalResult,
        "reconstructed comparison chain with calls case " .. index .. " differs")
    assert(#originalCalls == case[7] and #reconstructedCalls == #originalCalls,
        "comparison chain evaluated the wrong number of calls in case " .. index)
    for callIndex, name in ipairs(originalCalls) do
        assert(name == (callIndex == 1 and "first" or "second"),
            "source comparison chain evaluated calls out of order")
        assert(reconstructedCalls[callIndex] == name,
            "reconstructed comparison chain evaluated calls out of order")
    end
end
