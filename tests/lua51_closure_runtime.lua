local closeSource, closeBytecode, jumpSource, jumpBytecode, conditionalSource, conditionalBytecode = unpack(arg)

local function values(path, flagValue)
    _G.flag = flagValue
    local closures = assert(loadfile(path))()
    return {
        closures[1](1),
        closures[2](2),
        closures[1](4),
        closures[2](0),
    }
end

local function expect(label, actual, expected)
    for index, value in ipairs(expected) do
        assert(actual[index] == value,
            label .. " result " .. index .. ": expected " .. value .. ", got " .. tostring(actual[index]))
    end
end

local explicitlyClosed = {2, 4, 6, 4}
expect("Lua 5.1 CLOSE VM", values(closeBytecode), explicitlyClosed)
expect("Lua 5.1 CLOSE reconstruction", values(closeSource), explicitlyClosed)

local sharedUpvalue = {3, 5, 9, 9}
expect("Lua 5.1 JMP A VM", values(jumpBytecode), sharedUpvalue)
expect("Lua 5.1 JMP A reconstruction", values(jumpSource), sharedUpvalue)

local conditionalExpectations = {
    {false, {2, 4, 8, 8}},
    {true, sharedUpvalue},
}
for _, scenario in ipairs(conditionalExpectations) do
    local flagValue, expected = scenario[1], scenario[2]
    expect("Lua 5.1 conditional JMP A VM (flag=" .. tostring(flagValue) .. ")",
        values(conditionalBytecode, flagValue), expected)
    expect("Lua 5.1 conditional JMP A reconstruction (flag=" .. tostring(flagValue) .. ")",
        values(conditionalSource, flagValue), expected)
end
