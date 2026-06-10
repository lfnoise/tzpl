-- Fannkuch-redux. 0-indexed port of Mike Pall's algorithm, matching the
-- Tzopilotl version line-for-line so the comparison is apples-to-apples.
local N = 10

local function fannkuch(n)
    local p, q, s = {}, {}, {}
    for i = 0, n - 1 do p[i] = i; q[i] = i; s[i] = i end

    local sign = 1
    local maxflips = 0
    local sum = 0

    while true do
        for j = 0, n - 1 do q[j] = p[j] end
        local flips = 0
        local q0 = q[0]
        while q0 ~= 0 do
            local lo, hi = 0, q0
            while lo < hi do
                q[lo], q[hi] = q[hi], q[lo]
                lo = lo + 1
                hi = hi - 1
            end
            flips = flips + 1
            q0 = q[0]
        end
        if flips > maxflips then maxflips = flips end
        sum = sum + sign * flips

        if sign > 0 then
            p[0], p[1] = p[1], p[0]
            sign = -1
        else
            p[1], p[2] = p[2], p[1]
            sign = 1
            local i = 2
            local stop = false
            while i < n do
                local si = s[i]
                if si ~= 0 then
                    s[i] = si - 1
                    break
                end
                if i == n - 1 then stop = true; break end
                s[i] = i
                local t2 = p[0]
                for k = 0, i do p[k] = p[k + 1] end
                p[i + 1] = t2
                i = i + 1
            end
            if stop then break end
        end
    end
    return sum, maxflips
end

local sum, maxflips = fannkuch(N)
print(sum)
print("Pfannkuchen(" .. N .. ") = " .. maxflips)
