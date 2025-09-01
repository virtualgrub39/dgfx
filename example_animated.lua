function rgb(n, m, t)
    local aspect = config.w / config.h
    local screen_x = (2.0 * n / config.w - 1.0) * aspect
    local screen_y = 1.0 - 2.0 * m / config.h
    
    local cam_x = 3 * math.sin(t * 0.3)
    local cam_y = 2 + math.sin(t * 0.4) * 0.5
    local cam_z = 5 + math.cos(t * 0.2) * 2
    
    local focal_length = 2.0
    local ray_dx = screen_x
    local ray_dy = screen_y
    local ray_dz = -focal_length
    
    local ray_len = math.sqrt(ray_dx*ray_dx + ray_dy*ray_dy + ray_dz*ray_dz)
    ray_dx, ray_dy, ray_dz = ray_dx/ray_len, ray_dy/ray_len, ray_dz/ray_len
    
    local closest_dist = 1000
    local hit_color_r, hit_color_g, hit_color_b = 0, 0, 0
    local hit_normal_x, hit_normal_y, hit_normal_z = 0, 0, 0
    local hit_pos_x, hit_pos_y, hit_pos_z = 0, 0, 0
    
    for i = 1, 6 do
        local sphere_angle = t * 0.5 + i * math.pi / 3
        local sphere_x = 3 * math.cos(sphere_angle)
        local sphere_y = math.sin(t * 2 + i) * 1.5
        local sphere_z = 3 * math.sin(sphere_angle) - 8
        local sphere_radius = 0.8 + 0.3 * math.sin(t * 3 + i)
        
        local hit_dist, hit_x, hit_y, hit_z = raySphereIntersect(
            cam_x, cam_y, cam_z, ray_dx, ray_dy, ray_dz,
            sphere_x, sphere_y, sphere_z, sphere_radius
        )
        
        if hit_dist > 0 and hit_dist < closest_dist then
            closest_dist = hit_dist
            hit_pos_x, hit_pos_y, hit_pos_z = hit_x, hit_y, hit_z
            
            hit_normal_x = (hit_x - sphere_x) / sphere_radius
            hit_normal_y = (hit_y - sphere_y) / sphere_radius
            hit_normal_z = (hit_z - sphere_z) / sphere_radius
            
            local hue = (i / 6.0 + t * 0.1) % 1.0
            hit_color_r, hit_color_g, hit_color_b = hsvToRgb(hue, 0.8, 0.9)
        end
    end
    
    for i = 1, 4 do
        local cube_angle = t * 0.7 + i * math.pi / 2
        local cube_x = 2 * math.cos(cube_angle)
        local cube_y = math.cos(t * 1.5 + i * 2) * 2
        local cube_z = 2 * math.sin(cube_angle) - 5
        local cube_size = 0.6
        
        local hit_dist, hit_x, hit_y, hit_z, normal_x, normal_y, normal_z = rayBoxIntersect(
            cam_x, cam_y, cam_z, ray_dx, ray_dy, ray_dz,
            cube_x, cube_y, cube_z, cube_size, cube_size, cube_size
        )
        
        if hit_dist > 0 and hit_dist < closest_dist then
            closest_dist = hit_dist
            hit_pos_x, hit_pos_y, hit_pos_z = hit_x, hit_y, hit_z
            hit_normal_x, hit_normal_y, hit_normal_z = normal_x, normal_y, normal_z
            
            local hue = (i / 4.0 + t * 0.2 + 0.5) % 1.0
            hit_color_r, hit_color_g, hit_color_b = hsvToRgb(hue, 1.0, 1.0)
        end
    end
    
    local ground_y = -3
    if ray_dy < 0 then
        local ground_dist = (ground_y - cam_y) / ray_dy
        local ground_hit_x = cam_x + ray_dx * ground_dist
        local ground_hit_z = cam_z + ray_dz * ground_dist
        
        if ground_dist > 0 and ground_dist < closest_dist then
            closest_dist = ground_dist
            hit_pos_x, hit_pos_y, hit_pos_z = ground_hit_x, ground_y, ground_hit_z
            hit_normal_x, hit_normal_y, hit_normal_z = 0, 1, 0
            
            local checker = math.floor(ground_hit_x + 0.5) + math.floor(ground_hit_z + 0.5)
            if checker % 2 == 0 then
                hit_color_r, hit_color_g, hit_color_b = 0.8, 0.8, 0.8
            else
                hit_color_r, hit_color_g, hit_color_b = 0.2, 0.2, 0.2
            end
        end
    end
    
    if closest_dist < 1000 then
        return calculateSimpleLighting(
            hit_pos_x, hit_pos_y, hit_pos_z,
            hit_normal_x, hit_normal_y, hit_normal_z,
            hit_color_r, hit_color_g, hit_color_b,
            cam_x, cam_y, cam_z, t
        )
    end
    
    local sky_gradient = (screen_y + 1) * 0.5
    local horizon_blend = math.max(0, sky_gradient)
    
    local r = 0.05 + horizon_blend * 0.3
    local g = 0.1 + horizon_blend * 0.4  
    local b = 0.3 + horizon_blend * 0.2
    
    return r, g, b
end

function raySphereIntersect(ray_x, ray_y, ray_z, dir_x, dir_y, dir_z, sphere_x, sphere_y, sphere_z, radius)
    local oc_x = ray_x - sphere_x
    local oc_y = ray_y - sphere_y
    local oc_z = ray_z - sphere_z
    
    local a = dir_x*dir_x + dir_y*dir_y + dir_z*dir_z
    local b = 2 * (oc_x*dir_x + oc_y*dir_y + oc_z*dir_z)
    local c = oc_x*oc_x + oc_y*oc_y + oc_z*oc_z - radius*radius
    
    local discriminant = b*b - 4*a*c
    if discriminant < 0 then
        return -1, 0, 0, 0
    end
    
    local sqrt_disc = math.sqrt(discriminant)
    local t1 = (-b - sqrt_disc) / (2*a)
    local t2 = (-b + sqrt_disc) / (2*a)
    
    local t = (t1 > 0) and t1 or t2
    if t <= 0 then
        return -1, 0, 0, 0
    end
    
    local hit_x = ray_x + dir_x * t
    local hit_y = ray_y + dir_y * t
    local hit_z = ray_z + dir_z * t
    
    return t, hit_x, hit_y, hit_z
end

function rayBoxIntersect(ray_x, ray_y, ray_z, dir_x, dir_y, dir_z, box_x, box_y, box_z, size_x, size_y, size_z)
    local min_x, max_x = box_x - size_x, box_x + size_x
    local min_y, max_y = box_y - size_y, box_y + size_y
    local min_z, max_z = box_z - size_z, box_z + size_z
    
    local t_min_x = (min_x - ray_x) / dir_x
    local t_max_x = (max_x - ray_x) / dir_x
    if t_min_x > t_max_x then t_min_x, t_max_x = t_max_x, t_min_x end
    
    local t_min_y = (min_y - ray_y) / dir_y
    local t_max_y = (max_y - ray_y) / dir_y
    if t_min_y > t_max_y then t_min_y, t_max_y = t_max_y, t_min_y end
    
    local t_min_z = (min_z - ray_z) / dir_z
    local t_max_z = (max_z - ray_z) / dir_z
    if t_min_z > t_max_z then t_min_z, t_max_z = t_max_z, t_min_z end
    
    local t_near = math.max(t_min_x, t_min_y, t_min_z)
    local t_far = math.min(t_max_x, t_max_y, t_max_z)
    
    if t_near > t_far or t_far < 0 then
        return -1, 0, 0, 0, 0, 0, 0
    end
    
    local t = (t_near > 0) and t_near or t_far
    local hit_x = ray_x + dir_x * t
    local hit_y = ray_y + dir_y * t
    local hit_z = ray_z + dir_z * t
    
    local normal_x, normal_y, normal_z = 0, 0, 0
    local epsilon = 0.001
    
    if math.abs(hit_x - min_x) < epsilon then normal_x = -1
    elseif math.abs(hit_x - max_x) < epsilon then normal_x = 1
    elseif math.abs(hit_y - min_y) < epsilon then normal_y = -1
    elseif math.abs(hit_y - max_y) < epsilon then normal_y = 1
    elseif math.abs(hit_z - min_z) < epsilon then normal_z = -1
    elseif math.abs(hit_z - max_z) < epsilon then normal_z = 1
    end
    
    return t, hit_x, hit_y, hit_z, normal_x, normal_y, normal_z
end

function calculateSimpleLighting(hit_x, hit_y, hit_z, normal_x, normal_y, normal_z, base_r, base_g, base_b, cam_x, cam_y, cam_z, t)
    local light_x = 5 * math.sin(t * 0.6)
    local light_y = 3
    local light_z = 5 * math.cos(t * 0.6) - 3
    
    local to_light_x = light_x - hit_x
    local to_light_y = light_y - hit_y
    local to_light_z = light_z - hit_z
    local light_dist = math.sqrt(to_light_x*to_light_x + to_light_y*to_light_y + to_light_z*to_light_z)
    to_light_x, to_light_y, to_light_z = to_light_x/light_dist, to_light_y/light_dist, to_light_z/light_dist
    
    local light_intensity = math.max(0, normal_x*to_light_x + normal_y*to_light_y + normal_z*to_light_z)
    light_intensity = light_intensity / (1 + light_dist * 0.1)
    
    local ambient = 0.3
    
    local final_r = base_r * (ambient + light_intensity * 0.8)
    local final_g = base_g * (ambient + light_intensity * 0.8)
    local final_b = base_b * (ambient + light_intensity * 0.8)
    
    return math.min(1, final_r), math.min(1, final_g), math.min(1, final_b)
end

function hsvToRgb(h, s, v)
    local c = v * s
    local x = c * (1 - math.abs(((h * 6) % 2) - 1))
    local m = v - c
    
    local r, g, b = 0, 0, 0
    if h < 1/6 then r, g, b = c, x, 0
    elseif h < 2/6 then r, g, b = x, c, 0
    elseif h < 3/6 then r, g, b = 0, c, x
    elseif h < 4/6 then r, g, b = 0, x, c
    elseif h < 5/6 then r, g, b = x, 0, c
    else r, g, b = c, 0, x
    end
    
    return r + m, g + m, b + m
end

function fastNoise(x, y, z)
    local n = math.sin(x * 12.9898 + y * 78.233 + z * 37.719) * 43758.5453
    return n - math.floor(n)
end
