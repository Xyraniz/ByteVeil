local originalPath, reconstructedPath = unpack(arg)

local function run(path)
    local trace = {}
    local outerCount = 0
    local innerCount = 0

    _G.outerGate = function()
        outerCount = outerCount + 1
        return outerCount <= 2
    end
    _G.innerGate = function()
        innerCount = innerCount + 1
        return innerCount == 1
    end
    _G.innerBody = function()
        trace[#trace + 1] = "inner"
    end
    _G.outerBody = function()
        trace[#trace + 1] = "outer"
        innerCount = 0
    end

    assert(loadfile(path))()
    return table.concat(trace, ",")
end

local originalTrace = run(originalPath)
local reconstructedTrace = run(reconstructedPath)
assert(originalTrace == "inner,outer,inner,outer", "source nested-loop trace differs")
assert(reconstructedTrace == originalTrace, "reconstructed nested-loop trace differs")
