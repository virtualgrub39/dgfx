function map(value, fromMin, fromMax, toMin, toMax)
    return toMin + (value - fromMin) * (toMax - toMin) / (fromMax - fromMin)
end

function palette(t)
    local a = {0.5, 0.5, 0.5}
    local b = {0.5, 0.5, 0.5}
    local c = {1.0, 1.0, 1.0}
    local d = {0.263, 0.416, 0.557}
    
    local r = a[1] + b[1] * math.cos(6.28318 * (c[1] * t + d[1]))
    local g = a[2] + b[2] * math.cos(6.28318 * (c[2] * t + d[2]))
    local b = a[3] + b[3] * math.cos(6.28318 * (c[3] * t + d[3]))
    
    return r, g, b
end

function fract(x)
    return x - math.floor(x)
end

function length(x, y)
    return math.sqrt(x * x + y * y)
end

function rgb(n, m, time)
    local uv_x = (n * 2.0 - config.w) / config.h
    local uv_y = (m * 2.0 - config.h) / config.h
    
    local uv0_x, uv0_y = uv_x, uv_y
    local finalColor_r, finalColor_g, finalColor_b = 0.0, 0.0, 0.0
    
    for i = 0, 3 do
        uv_x = fract(uv_x * 1.5) - 0.5
        uv_y = fract(uv_y * 1.5) - 0.5
        
        local d = length(uv_x, uv_y) * math.exp(-length(uv0_x, uv0_y))
        local col_r, col_g, col_b = palette(length(uv0_x, uv0_y) + i * 0.4 + time * 0.4)
        
        d = math.sin(d * 8.0 + time) / 8.0
        d = math.abs(d)
        d = math.pow(0.01 / d, 1.2)
        
        finalColor_r = finalColor_r + col_r * d
        finalColor_g = finalColor_g + col_g * d
        finalColor_b = finalColor_b + col_b * d
    end
    
    finalColor_r = math.max(0, math.min(1, finalColor_r))
    finalColor_g = math.max(0, math.min(1, finalColor_g))
    finalColor_b = math.max(0, math.min(1, finalColor_b))
    
    return finalColor_r, finalColor_g, finalColor_b
end
