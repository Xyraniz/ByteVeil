local originalPath, reconstructedPath = unpack(arg)
local original = assert(loadfile(originalPath))()
local reconstructed = assert(loadfile(reconstructedPath))()

local function run(read)
    local accesses = {}
    local leaf = setmetatable({}, {
        __index = function(_, key)
            accesses[#accesses + 1] = "leaf." .. key
            return "read-result"
        end,
    })
    local root = setmetatable({}, {
        __index = function(_, key)
            accesses[#accesses + 1] = "root." .. key
            if key == "branch" then return leaf end
        end,
    })
    return read(root), table.concat(accesses, ",")
end

local expected, originalTrace = run(original)
local actual, reconstructedTrace = run(reconstructed)
assert(expected == "read-result" and actual == expected,
    "inlined GETTABLE chain changed its value")
assert(originalTrace == "root.branch,leaf.leaf" and reconstructedTrace == originalTrace,
    "inlined GETTABLE chain changed lookup order or count")
