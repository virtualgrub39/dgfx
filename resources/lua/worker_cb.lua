-- local ffi = require("ffi")
-- ffi.cdef [[
-- typedef unsigned char uint8_t;
-- typedef uint8_t *uint8_ptr;
-- typedef uint8_t **uint8_pp;
-- ]]

-- function __dgfx_worker_cb(pp_ld, start_idx, work_len, t)
--     local pp = ffi.cast("uint8_pp", pp_ld)
--     local base = pp[0]
--     if base == nil then
--         return
--     end

--     local start_px = tonumber(start_idx)
--     local n_px = tonumber(work_len)
--     if n_px <= 0 then
--         return
--     end

--     local p = base + (start_px * 4)

--     for i = 0, n_px - 1 do
--         local idx = (start_px + i)
--         local n = idx % dgfx.width
--         local m = (idx - n) / dgfx.width

--         r, g, b = rgb(n, m, t)

--         local ir = math.floor(math.max(0, math.min(1, r)) * 255 + 0.5)
--         local ig = math.floor(math.max(0, math.min(1, g)) * 255 + 0.5)
--         local ib = math.floor(math.max(0, math.min(1, b)) * 255 + 0.5)

--         local byte_off = i * 4

--         p[byte_off + 0] = ir -- R
--         p[byte_off + 1] = ig -- G
--         p[byte_off + 2] = ib -- B
--         p[byte_off + 3] = 255 -- A
--     end
-- end

local _rgb_local = rgb
local w = dgfx.width
local h = dgfx.width
local push = table.insert

function __dgfx_worker_cb(start, count, t)
  local out = {}
  local k = 1
  for i = 0, count - 1 do
    local idx = start + i
    local x = idx % w
    local y = (idx - x) / w
    local r,g,b = _rgb_local(x, y, t)
    local ir = math.floor(math.max(0, math.min(1, r)) * 255 + 0.5)
    local ig = math.floor(math.max(0, math.min(1, g)) * 255 + 0.5)
    local ib = math.floor(math.max(0, math.min(1, b)) * 255 + 0.5)
    out[k] = string.char(ir, ig, ib, 255)
    k = k + 1
  end
  return table.concat(out)
end
