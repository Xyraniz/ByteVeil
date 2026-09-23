local numericSource, numericOutput, genericSource, genericOutput = unpack(arg)

local function loadFunctions(path)
    return assert(loadfile(path))()
end

local function sameNumberSequence(source, output, indices, amounts, expected)
    local original = loadFunctions(source)
    local reconstructed = loadFunctions(output)
    for step, index in ipairs(indices) do
        local originalValue = original[index](amounts[step])
        local reconstructedValue = reconstructed[index](amounts[step])
        assert(originalValue == expected[step],
            "source numeric closure result " .. step .. " was " .. tostring(originalValue))
        assert(reconstructedValue == originalValue,
            "reconstructed numeric closure result " .. step .. " differs")
    end
end

local function sameGenericSequence(source, output)
    local original = loadFunctions(source)
    local reconstructed = loadFunctions(output)
    local indices = {1, 2, 1, 2}
    local increments = {1, 0, 4, 0}
    local suffixes = {"!", "?", "!", "?"}
    local expectedKeys = {2, 2, 6, 2}
    local expectedValues = {"alpha!", "beta?", "alpha!!", "beta??"}
    for step, index in ipairs(indices) do
        local originalKey, originalValue = original[index](increments[step], suffixes[step])
        local reconstructedKey, reconstructedValue = reconstructed[index](increments[step], suffixes[step])
        assert(originalKey == expectedKeys[step] and originalValue == expectedValues[step],
            "source generic closure result " .. step .. " differs")
        assert(reconstructedKey == originalKey and reconstructedValue == originalValue,
            "reconstructed generic closure result " .. step .. " differs")
    end
end

sameNumberSequence(numericSource, numericOutput, {1, 2, 1, 2}, {1, 0, 4, 0}, {2, 2, 6, 2})
sameGenericSequence(genericSource, genericOutput)
