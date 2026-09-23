local path = assert(arg[1], "expected reconstructed single-latch loop path")
local result = assert(loadfile(path))()
assert(result == 3, "single-latch loop did not break on its third iteration")
