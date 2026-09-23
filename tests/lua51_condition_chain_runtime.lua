local originalPath, reconstructedPath = unpack(arg)
local originalClassify, originalChoose, originalCompareWithCalls, originalCallChainElse, originalMixedValue, originalCaptureBoundary = assert(loadfile(originalPath))()
local reconstructedClassify, reconstructedChoose, reconstructedCompareWithCalls, reconstructedCallChainElse, reconstructedMixedValue, reconstructedCaptureBoundary = assert(loadfile(reconstructedPath))()

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

local elseCases = {
    {false, {first = true, second = true}, {"fallback"}},
    {true, {first = false, second = true}, {"probe:first", "fallback"}},
    {true, {first = true, second = false}, {"probe:first", "probe:second", "fallback"}},
    {true, {first = true, second = true}, {"probe:first", "probe:second", "body"}},
}
for index, case in ipairs(elseCases) do
    local function run(callChain)
        local trace = {}
        local function probe(name)
            trace[#trace + 1] = "probe:" .. name
            return case[2][name]
        end
        local function record(name)
            trace[#trace + 1] = name
        end
        callChain(case[1], probe, record)
        return trace
    end
    local originalTrace = run(originalCallChainElse)
    local reconstructedTrace = run(reconstructedCallChainElse)
    assert(table.concat(originalTrace, ",") == table.concat(case[3], ","),
        "source call-chain else case " .. index .. " differs")
    assert(table.concat(reconstructedTrace, ",") == table.concat(originalTrace, ","),
        "reconstructed call-chain else effects differ in case " .. index)
end

local mixedValueCases = {
    {value = "payload", mapResult = true, fallback = "fallback", expected = "payload"},
    {value = "payload", mapResult = false, fallback = "fallback", expected = "fallback"},
    {value = false, mapResult = true, fallback = "fallback", expected = "fallback"},
    {value = nil, mapResult = "mapped", fallback = "fallback", expected = "fallback"},
    {value = 0, mapResult = "mapped", fallback = "fallback", expected = 0},
}
for index, case in ipairs(mixedValueCases) do
    local function run(mixedValue)
        local trace = {}
        local function getValue()
            trace[#trace + 1] = "get"
            return case.value
        end
        local function mapValue(value)
            trace[#trace + 1] = "map"
            assert(value == case.value, "mixed-value operand changed in case " .. index)
            return case.mapResult
        end
        return mixedValue(getValue, mapValue, case.fallback), table.concat(trace, ",")
    end
    local originalResult, originalTrace = run(originalMixedValue)
    local reconstructedResult, reconstructedTrace = run(reconstructedMixedValue)
    assert(originalResult == case.expected, "source mixed-value case " .. index .. " differs")
    assert(reconstructedResult == originalResult,
        "reconstructed mixed-value result differs in case " .. index)
    assert(originalTrace == "get,map" and reconstructedTrace == originalTrace,
        "mixed-value expression changed call order in case " .. index)
end

local captureBoundaryCases = {
    {tag = "go", value = "replacement", expected = "replacement", calls = 1},
    {tag = "go", value = false, expected = "go", calls = 1},
    {tag = "other", value = "unused", expected = "other", calls = 0},
}
for index, case in ipairs(captureBoundaryCases) do
    local function run(capture)
        local calls = 0
        local function maybeGetter()
            calls = calls + 1
            return case.value
        end
        local captured = capture(case.tag, maybeGetter)
        return captured(), calls
    end
    local originalResult, originalCalls = run(originalCaptureBoundary)
    local reconstructedResult, reconstructedCalls = run(reconstructedCaptureBoundary)
    assert(originalResult == case.expected and originalCalls == case.calls,
        "source TESTSET boundary case " .. index .. " differs")
    assert(reconstructedResult == originalResult and reconstructedCalls == originalCalls,
        "reconstructed TESTSET boundary result differs in case " .. index)
end
