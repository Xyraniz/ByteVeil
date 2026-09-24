local originalPath, reconstructedPath = unpack(arg)

local cases = {
    {left = 1, right = 1, result = nil},
    {left = "inside-range", right = "inside-range", result = "range-tail"},
    {left = 1, right = 2, result = "range-tail"},
}

local function run(path, case)
    local chunk = assert(loadfile(path))
    local environment = {_G = false, left = case.left, right = case.right}
    environment._G = environment
    setfenv(chunk, environment)
    return chunk()
end

for index, case in ipairs(cases) do
    local originalResult = run(originalPath, case)
    local reconstructedResult = run(reconstructedPath, case)
    assert(originalResult == case.result,
        "bytecode result for branch-range case " .. index .. ": " .. tostring(originalResult))
    assert(reconstructedResult == originalResult,
        "reconstructed result for branch-range case " .. index .. ": " ..
        tostring(reconstructedResult))
end
