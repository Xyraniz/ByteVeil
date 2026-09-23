local originalPath, reconstructedPath = unpack(arg)
local originalClassify, originalChoose = assert(loadfile(originalPath))()
local reconstructedClassify, reconstructedChoose = assert(loadfile(reconstructedPath))()

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
