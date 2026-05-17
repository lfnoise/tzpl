-- Same LCG + cumulative-table lookup as the Tzopilotl port.
local N_STEPS = 5000000
local IM = 139968
local FIM = 139968.0

local P0 = 0.27
local P1 = P0 + 0.12
local P2 = P1 + 0.12
local P3 = P2 + 0.27
local P4 = P3 + 0.02
local P5 = P3 + 0.04
local P6 = P3 + 0.06
local P7 = P3 + 0.08
local P8 = P3 + 0.10
local P9 = P3 + 0.12
local P10 = P3 + 0.14
local P11 = P3 + 0.16
local P12 = P3 + 0.18
local P13 = P3 + 0.20

local seed = 42
local c0, c1, c2, c3, c4 = 0, 0, 0, 0, 0
local c5, c6, c7, c8, c9 = 0, 0, 0, 0, 0
local c10, c11, c12, c13, c14 = 0, 0, 0, 0, 0

for _ = 1, N_STEPS do
    seed = (seed * 3877 + 29573) % IM
    local p = seed / FIM
    if     p < P0  then c0  = c0  + 1
    elseif p < P1  then c1  = c1  + 1
    elseif p < P2  then c2  = c2  + 1
    elseif p < P3  then c3  = c3  + 1
    elseif p < P4  then c4  = c4  + 1
    elseif p < P5  then c5  = c5  + 1
    elseif p < P6  then c6  = c6  + 1
    elseif p < P7  then c7  = c7  + 1
    elseif p < P8  then c8  = c8  + 1
    elseif p < P9  then c9  = c9  + 1
    elseif p < P10 then c10 = c10 + 1
    elseif p < P11 then c11 = c11 + 1
    elseif p < P12 then c12 = c12 + 1
    elseif p < P13 then c13 = c13 + 1
    else                c14 = c14 + 1 end
end

print(c0); print(c1); print(c2); print(c3); print(c4)
print(c5); print(c6); print(c7); print(c8); print(c9)
print(c10); print(c11); print(c12); print(c13); print(c14)
