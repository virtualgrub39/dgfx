do
    local _rgb = rgb
    local width = dgfx.width
    local s_char = string.char
    local t_concat = table.concat

    local start = dgfx.worker.start
    local count = dgfx.worker.len

    local out = {}
    for i = 1, count do -- pre-allocate
        out[i] = "\xFF\xFF\xFF\xFF"
    end

    function __dgfx_worker_cb(t)
        for i = 1, count do
            local pixel_idx = start + i - 1
            local x = pixel_idx % width
            local y = (pixel_idx - x) / width

            local r, g, b = _rgb(x, y, t)
            out[i] = s_char(r * 255, g * 255, b * 255, 255)
        end

        return t_concat(out)
    end
end
