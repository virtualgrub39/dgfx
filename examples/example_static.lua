local function map(v, a, b, c, d)
    return (v - a) * (d - c) / (b - a) + c
end

local function clamp(x, lo, hi)
    if x < lo then
        return lo
    end
    if x > hi then
        return hi
    end
    return x
end

local function hsv2rgb(h, s, v)
    if s == 0 then
        return v, v, v
    end
    h = h * 6
    local i = math.floor(h)
    local f = h - i
    local p = v * (1 - s)
    local q = v * (1 - s * f)
    local t = v * (1 - s * (1 - f))
    i = i % 6
    if i == 0 then
        return v, t, p
    end
    if i == 1 then
        return q, v, p
    end
    if i == 2 then
        return p, v, t
    end
    if i == 3 then
        return p, q, v
    end
    if i == 4 then
        return t, p, v
    end
    return v, p, q
end

function rgb(n, m)
    local cx = map(n, 0, dgfx.width, -2.5, 1)
    local cy = map(m, 0, dgfx.height, -1.2, 1.2)
    local zx, zy = 0, 0
    local maxIter = 100
    local iter = 0
    while iter < maxIter do
        local x2 = zx * zx - zy * zy + cx
        local y2 = 2 * zx * zy + cy
        zx, zy = x2, y2
        if zx * zx + zy * zy > 4 then
            break
        end
        iter = iter + 1
    end
    if iter == maxIter then
        return 0, 0, 0
    else
        local mag = math.sqrt(zx * zx + zy * zy)
        local mu = iter + 1 - math.log(math.log(mag)) / math.log(2)
        local hue = (mu / maxIter) % 1
        local r, g, b = hsv2rgb(hue, 0.8, 0.9)
        return r, g, b
    end
end
