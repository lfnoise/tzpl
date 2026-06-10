-- Dense matrix multiply, same algorithm as the Tzopilotl port.
local N = 500
local FN = 500.0
local sin = math.sin

local function init_matrix(phase)
    local rows = {}
    for i = 0, N - 1 do
        local row = {}
        for j = 0, N - 1 do
            row[j] = sin(i * FN + j + phase)
        end
        rows[i] = row
    end
    return rows
end

local A = init_matrix(0.0)
local B = init_matrix(0.5)

local C = {}
for i = 0, N - 1 do
    local Ci = {}
    local Ai = A[i]
    for j = 0, N - 1 do
        local s = 0.0
        for k = 0, N - 1 do
            s = s + Ai[k] * B[k][j]
        end
        Ci[j] = s
    end
    C[i] = Ci
end

local trace = 0.0
for i = 0, N - 1 do trace = trace + C[i][i] end
print(trace)
