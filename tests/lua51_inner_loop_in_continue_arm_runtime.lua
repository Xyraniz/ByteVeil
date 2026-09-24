local originalPath, reconstructedPath = unpack(arg)

local expectedTrace = {
    "outerGate", "outerGate-call",
    "continueGate", "continueGate-call",
    "innerGate", "innerGate-call", "body", "body-call",
    "innerGate", "innerGate-call", "body", "body-call",
    "innerGate", "innerGate-call",
    "outerGate", "outerGate-call",
    "continueGate", "continueGate-call",
    "outerGate", "outerGate-call",
}

local function run(path)
    local trace = {}
    local outerCalls, continueCalls, innerCalls = 0, 0, 0
    local values = {
        outerGate = function()
            outerCalls = outerCalls + 1
            trace[#trace + 1] = "outerGate-call"
            return outerCalls <= 2
        end,
        continueGate = function()
            continueCalls = continueCalls + 1
            trace[#trace + 1] = "continueGate-call"
            return continueCalls == 1
        end,
        innerGate = function()
            innerCalls = innerCalls + 1
            trace[#trace + 1] = "innerGate-call"
            return innerCalls <= 2
        end,
        body = function()
            trace[#trace + 1] = "body-call"
        end,
    }
    local environment = {_G = false}
    environment._G = environment
    setmetatable(environment, {
        __index = function(_, key)
            trace[#trace + 1] = key
            return values[key]
        end,
    })
    local chunk = assert(loadfile(path))
    setfenv(chunk, environment)
    local result = chunk()
    return result, trace
end

for index, path in ipairs({originalPath, reconstructedPath}) do
    local result, trace = run(path)
    assert(result == nil, "loop result for continue-arm path " .. index)
    assert(table.concat(trace, ",") == table.concat(expectedTrace, ","),
        "loop trace for continue-arm path " .. index .. ": " .. table.concat(trace, ","))
end
