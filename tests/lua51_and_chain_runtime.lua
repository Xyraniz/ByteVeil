local originalPath, reconstructedPath = unpack(arg)
local originalCheck, originalBounded, originalCount = assert(loadfile(originalPath))()
local reconstructedCheck, reconstructedBounded, reconstructedCount = assert(loadfile(reconstructedPath))()

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
