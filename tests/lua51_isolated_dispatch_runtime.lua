local originalPath, reconstructedPath = unpack(arg)

local cases = {
    {value = 1, expected = "matched"},
    {value = 2, expected = "matched"},
    {value = 3, expected = "fallback"},
}

local function run(path, case)
    local chunk = assert(loadfile(path))
    local environment = {value = case.value, loop = true}
    environment._G = environment
    setfenv(chunk, environment)
    return chunk()
end

for _, case in ipairs(cases) do
    local original = run(originalPath, case)
    local reconstructed = run(reconstructedPath, case)
    assert(original == case.expected,
        "fixture produced an unexpected result: " .. tostring(original))
    assert(reconstructed == original,
        "localized dispatcher changed behavior: " .. tostring(reconstructed))
end
