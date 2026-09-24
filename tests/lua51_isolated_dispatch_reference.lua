local selected
if value == 1 or value == 2 then
    selected = "matched"
else
    selected = "fallback"
end

if loop then
    return selected
end

while true do end
