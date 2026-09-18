local function classify(value)
    if value < 0 then
        return -value
    else
        return value + 1
    end
end
local total = 0
for i = 1, 3 do
    total += classify(i)
end
return total
