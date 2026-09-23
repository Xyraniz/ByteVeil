local originalPath, reconstructedPath = unpack(arg)
local originalCheck, originalBounded, originalCount, originalChoose, originalChooseMixed = assert(loadfile(originalPath))()
local reconstructedCheck, reconstructedBounded, reconstructedCount, reconstructedChoose, reconstructedChooseMixed = assert(loadfile(reconstructedPath))()

local cases = {
    {false, true, true, "no"},
    {true, false, true, "no"},
    {true, true, false, "no"},
    {true, true, true, "yes"},
    {1, "value", {}, "yes"},
    {"", true, true, "yes"},
}
for index, case in ipairs(cases) do
    local originalResult = originalCheck(case[1], case[2], case[3])
    local reconstructedResult = reconstructedCheck(case[1], case[2], case[3])
    assert(originalResult == case[4], "source and-chain case " .. index .. " differs")
    assert(reconstructedResult == originalResult,
        "reconstructed and-chain case " .. index .. " differs")
end

for n = 0, 6 do
    local originalResult = originalBounded(n)
    local reconstructedResult = reconstructedBounded(n)
    assert(reconstructedResult == originalResult,
        "reconstructed while-and-chain case " .. n .. " differs")
end

for _, flag in ipairs({false, true}) do
    for n = -1, 5 do
        local originalResult = originalCount(flag, n)
        local reconstructedResult = reconstructedCount(flag, n)
        assert(reconstructedResult == originalResult,
            "reconstructed if-repeat case " .. tostring(flag) .. ", " .. n .. " differs")
    end
end

local chooseCases = {
    {{true, false, false, false}, "yes", 1},
    {{false, true, false, false}, "yes", 2},
    {{false, false, true, false}, "yes", 3},
    {{false, false, false, true}, "yes", 4},
    {{false, false, false, false}, "no", 4},
}
for index, case in ipairs(chooseCases) do
    local originalCalls, reconstructedCalls = {}, {}
    local function probe(values, calls)
        return function(item)
            calls[#calls + 1] = item
            return values[item]
        end
    end
    local originalResult = originalChoose(probe(case[1], originalCalls))
    local reconstructedResult = reconstructedChoose(probe(case[1], reconstructedCalls))
    assert(originalResult == case[2], "source or-chain case " .. index .. " differs")
    assert(reconstructedResult == originalResult,
        "reconstructed or-chain case " .. index .. " differs")
    assert(#originalCalls == case[3] and #reconstructedCalls == #originalCalls,
        "or-chain evaluated the wrong number of calls in case " .. index)
    for callIndex, item in ipairs(originalCalls) do
        assert(item == callIndex and reconstructedCalls[callIndex] == item,
            "or-chain evaluated calls out of order in case " .. index)
    end
end

for _, first in ipairs({false, true}) do
    for _, second in ipairs({false, true}) do
        for _, third in ipairs({false, true}) do
            local values = {first, second, third}
            local originalCalls, reconstructedCalls = {}, {}
            local function probe(items, calls)
                return function(item)
                    calls[#calls + 1] = item
                    return items[item]
                end
            end
            local originalResult = originalChooseMixed(probe(values, originalCalls))
            local reconstructedResult = reconstructedChooseMixed(probe(values, reconstructedCalls))
            local expected = first or (second and third)
            assert(originalResult == (expected and "yes" or "no"), "source mixed-chain result differs")
            assert(reconstructedResult == originalResult, "reconstructed mixed-chain result differs")
            assert(#reconstructedCalls == #originalCalls, "mixed-chain evaluated the wrong number of calls")
            for index, item in ipairs(originalCalls) do
                assert(reconstructedCalls[index] == item, "mixed-chain evaluated calls out of order")
            end
        end
    end
end
