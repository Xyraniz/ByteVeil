local originalPath, reconstructedPath = unpack(arg)
local original = assert(loadfile(originalPath))()
local reconstructed = assert(loadfile(reconstructedPath))()

local cases = {
    {{"first", "stop", "last"}, "stop"},
    {{"first", "last"}, "missing"},
    {{}, "missing"},
    {{"stop", "later"}, "stop"},
}
for index, case in ipairs(cases) do
    local originalResult = original(case[1])
    local reconstructedResult = reconstructed(case[1])
    assert(originalResult == case[2], "source early-return case " .. index .. " differs")
    assert(reconstructedResult == originalResult,
        "reconstructed early-return case " .. index .. " differs")
end
