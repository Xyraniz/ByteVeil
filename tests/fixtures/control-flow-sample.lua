local function classify(value)
    if value < 0 then
        return "negative"
    elseif value == 0 then
        return "zero"
    else
        return "positive"
    end
end

local total = 0
for i = 1, 3 do
    total = total + i
end

local n = 3
while n > 0 do
    n = n - 1
end

repeat
    total = total - 1
until total <= 0

return classify(total)
