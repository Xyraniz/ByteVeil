local originalPath, reconstructedPath = unpack(arg)

local cases = {
    {
        pre = false,
        trace = {"pre", "fallback", "fallback-call"},
    },
    {
        pre = true,
        trace = {
            "pre",
            "loopGate", "loopGate-call", "body", "body-call", "latch", "latch-call",
            "loopGate", "loopGate-call", "body", "body-call", "latch", "latch-call",
            "extra", "extra-call",
            "loopGate", "loopGate-call",
        },
    },
}

local function run(path, case)
    local trace = {}
    local loopCalls, latchCalls = 0, 0
    local values = {
        pre = case.pre,
        loopGate = function()
            loopCalls = loopCalls + 1
            trace[#trace + 1] = "loopGate-call"
            return loopCalls < 3
        end,
        body = function()
            trace[#trace + 1] = "body-call"
        end,
        latch = function()
            latchCalls = latchCalls + 1
            trace[#trace + 1] = "latch-call"
            return latchCalls == 2
        end,
        extra = function()
            trace[#trace + 1] = "extra-call"
        end,
        fallback = function()
            trace[#trace + 1] = "fallback-call"
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
    return chunk(), trace
end

for index, case in ipairs(cases) do
    local originalResult, originalTrace = run(originalPath, case)
    local reconstructedResult, reconstructedTrace = run(reconstructedPath, case)
    assert(originalResult == nil, "bytecode result for loop-tail case " .. index)
    assert(table.concat(originalTrace, ",") == table.concat(case.trace, ","),
        "bytecode trace for loop-tail case " .. index .. ": " ..
        table.concat(originalTrace, ","))
    assert(reconstructedResult == originalResult,
        "reconstructed result for loop-tail case " .. index)
    assert(table.concat(reconstructedTrace, ",") == table.concat(originalTrace, ","),
        "reconstructed trace for loop-tail case " .. index .. ": " ..
        table.concat(reconstructedTrace, ","))
end
