-- Mandelbrot set: count points inside the set on a WxH grid.
local W, H, MAX_ITER = 1000, 1000, 100

local function count_inside()
    local inside = 0
    for py = 0, H - 1 do
        local ci = 2.0 * py / H - 1.0
        for px = 0, W - 1 do
            local cr = 2.0 * px / W - 1.5
            local zr, zi = 0.0, 0.0
            local escaped = false
            for n = 0, MAX_ITER - 1 do
                local zr2 = zr * zr
                local zi2 = zi * zi
                if zr2 + zi2 > 4.0 then escaped = true; break end
                zi = 2.0 * zr * zi + ci
                zr = zr2 - zi2 + cr
            end
            if not escaped then inside = inside + 1 end
        end
    end
    return inside
end

print(count_inside())
