-- Spectral norm.
local N = 1000
local sqrt = math.sqrt

local function aij(i, j)
    return 1.0 / ((i + j) * (i + j + 1.0) * 0.5 + i + 1.0)
end

local function mul_av(v, n)
    local out = {}
    for i = 0, n - 1 do
        local s = 0.0
        for j = 0, n - 1 do
            s = s + aij(i, j) * v[j]
        end
        out[i] = s
    end
    return out
end

local function mul_atv(v, n)
    local out = {}
    for i = 0, n - 1 do
        local s = 0.0
        for j = 0, n - 1 do
            s = s + aij(j, i) * v[j]
        end
        out[i] = s
    end
    return out
end

local function mul_at_av(v, n)
    return mul_atv(mul_av(v, n), n)
end

local u = {}
for i = 0, N - 1 do u[i] = 1.0 end
local v = {}
for _ = 1, 10 do
    v = mul_at_av(u, N)
    u = mul_at_av(v, N)
end

local vBv, vv = 0.0, 0.0
for i = 0, N - 1 do
    vBv = vBv + u[i] * v[i]
    vv  = vv  + v[i] * v[i]
end

print(sqrt(vBv / vv))
