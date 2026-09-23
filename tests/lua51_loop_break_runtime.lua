local originalPath, reconstructedPath = unpack(arg)
local original = assert(loadfile(originalPath))()
local reconstructed = assert(loadfile(reconstructedPath))()

for n = 0, 6 do
    local expectedTotal = math.min(n, 2) * (math.min(n, 2) + 1) / 2
    local expectedCountdown = math.max(n - 1, 0)
    local expectedFinalN = n > 0 and 1 or 0
    local originalValues = {original(n)}
    local reconstructedValues = {reconstructed(n)}
    local expected = {expectedTotal, 1, expectedCountdown, expectedFinalN, 2}
    for index, value in ipairs(expected) do
        assert(originalValues[index] == value,
            "source loop-break result " .. index .. " differs for n=" .. n)
        assert(reconstructedValues[index] == originalValues[index],
            "reconstructed loop-break result " .. index .. " differs for n=" .. n)
    end
end
