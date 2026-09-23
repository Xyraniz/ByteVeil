local path = assert(arg[1], "expected reconstructed multi-latch loop path")

local function run(continueFirst, continueSecond, stopAt, expectedResult, expectedSecondCalls, expectedStopCalls)
    local firstCalls, secondCalls, stopCalls = 0, 0, 0
    continueA = function(value)
        firstCalls = firstCalls + 1
        return continueFirst[value] or false
    end
    continueB = function(value)
        secondCalls = secondCalls + 1
        return continueSecond[value] or false
    end
    stop = function(value)
        stopCalls = stopCalls + 1
        return value == stopAt
    end

    local result = assert(loadfile(path))()
    assert(result == expectedResult, "multi-latch loop returned the wrong counter")
    assert(firstCalls == expectedResult, "first continue check did not run once per iteration")
    assert(secondCalls == expectedSecondCalls, "first continue did not skip the later checks")
    assert(stopCalls == expectedStopCalls, "second continue did not skip the stop check")
end

run({[1] = true}, {[2] = true}, 4, 4, 3, 2)
run({}, {}, 3, 3, 3, 3)
