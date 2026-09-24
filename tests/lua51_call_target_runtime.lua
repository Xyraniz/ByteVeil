local originalPath, reconstructedPath = unpack(arg)
local originalCreate, originalComputed, originalFixed = assert(loadfile(originalPath))()
local reconstructedCreate, reconstructedComputed, reconstructedFixed = assert(loadfile(reconstructedPath))()

local events
Factory = setmetatable({}, {
    __index = function(_, key)
        events[#events + 1] = "lookup:" .. key
        if key == "new" then
            return function(value)
                events[#events + 1] = "call:" .. value
                return "created:" .. value
            end
        end
    end,
})

local function run(create, computed)
    events = {}
    local result
    if computed then
        result = create(function()
            events[#events + 1] = "evaluate"
            return "value"
        end)
    else
        result = create("value")
    end
    return result, table.concat(events, ",")
end

local expected, expectedTrace = run(originalCreate, false)
local actual, actualTrace = run(reconstructedCreate, false)
assert(expected == "created:value" and actual == expected,
    "inlined call target changed its result")
assert(expectedTrace == "lookup:new,call:value" and actualTrace == expectedTrace,
    "inlined call target changed table lookup order or count")

local expectedComputed, expectedComputedTrace = run(originalComputed, true)
local actualComputed, actualComputedTrace = run(reconstructedComputed, true)
assert(expectedComputed == "created:value" and actualComputed == expectedComputed,
    "effectful call argument changed its result")
assert(expectedComputedTrace == "lookup:new,evaluate,call:value" and
       actualComputedTrace == expectedComputedTrace,
    "call target fusion moved a lookup across an effectful argument")

local expectedFixed, expectedFixedTrace = run(originalFixed, false)
local actualFixed, actualFixedTrace = run(reconstructedFixed, false)
assert(expectedFixed == "created:literal" and actualFixed == expectedFixed,
    "omitting an unused constant argument register changed its result")
assert(expectedFixedTrace == "lookup:new,call:literal" and actualFixedTrace == expectedFixedTrace,
    "omitting an unused constant argument register changed call order")
