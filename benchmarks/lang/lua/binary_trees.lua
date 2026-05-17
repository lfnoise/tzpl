-- Binary trees. Same shape as the Tzopilotl port.
local MIN_DEPTH = 4
local MAX_DEPTH = 11

local function build(depth)
    if depth <= 0 then return { nil, nil }
    else return { build(depth - 1), build(depth - 1) } end
end

local function check(t)
    if t[1] then return 1 + check(t[1]) + check(t[2])
    else return 1 end
end

local stretch_depth = MAX_DEPTH + 1
print(string.format("stretch tree of depth %d\t check: %d", stretch_depth, check(build(stretch_depth))))

local long_lived = build(MAX_DEPTH)

for depth = MIN_DEPTH, MAX_DEPTH, 2 do
    local iterations = 1
    for _ = 1, MAX_DEPTH - depth + MIN_DEPTH do iterations = iterations * 2 end
    local sum = 0
    for _ = 1, iterations do
        sum = sum + check(build(depth))
    end
    print(string.format("%d\t trees of depth %d\t check: %d", iterations, depth, sum))
end

print(string.format("long lived tree of depth %d\t check: %d", MAX_DEPTH, check(long_lived)))
