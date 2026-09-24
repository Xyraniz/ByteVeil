local originalPath, reconstructedPath = unpack(arg)
local originalInvoke, originalComputed = assert(loadfile(originalPath))()
local reconstructedInvoke, reconstructedComputed = assert(loadfile(reconstructedPath))()

local function run(invoke, computed)
    local events = {}
    local object
    object = setmetatable({}, {
        __index = function(_, key)
            events[#events + 1] = "lookup:" .. key
            if key == "ping" then
                return function(receiver, value)
                    assert(receiver == object, "method receiver changed")
                    events[#events + 1] = "call:" .. value
                    return "pong:" .. value
                end
            end
        end,
    })

    local result
    if computed then
        result = invoke(object, function()
            events[#events + 1] = "evaluate"
            return "value"
        end)
    else
        result = invoke(object, "value")
    end
    return result, table.concat(events, ",")
end

local expected, expectedTrace = run(originalInvoke, false)
local actual, actualTrace = run(reconstructedInvoke, false)
assert(expected == "pong:value" and actual == expected,
    "method call with a register argument changed its result")
assert(expectedTrace == "lookup:ping,call:value" and actualTrace == expectedTrace,
    "method call with a register argument changed its lookup or call order")

local expectedComputed, expectedComputedTrace = run(originalComputed, true)
local actualComputed, actualComputedTrace = run(reconstructedComputed, true)
assert(expectedComputed == "pong:value" and actualComputed == expectedComputed,
    "method call with an effectful argument changed its result")
assert(expectedComputedTrace == "lookup:ping,evaluate,call:value" and
       actualComputedTrace == expectedComputedTrace,
    "method call fusion moved lookup across an effectful argument")
