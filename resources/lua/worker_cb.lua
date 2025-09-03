do
  local _rgb = rgb
  local width = dgfx.width

  local floor = math.floor
  local s_char = string.char
  local t_concat = table.concat

  local start = dgfx.worker.start
  local count = dgfx.worker.len 

  local start_x = start % width
  local start_y = (start - start_x) / width

  local out = {}
  
  for i = 1, count do -- pre-allocate
    out[i] = ""
  end

  function __dgfx_worker_cb(t)
    local x = start_x
    local y = start_y
    
    local i = 1
    local remaining = count
    
    local r, g, b

    while remaining >= 4 do -- unroll 4 iterations
      -- Pixel 1
      r, g, b = _rgb(x, y, t)
      out[i] = s_char(r * 255, g * 255, b * 255, 255)
      x = x + 1
      if x == width then x = 0; y = y + 1 end
      
      -- Pixel 2
      r, g, b = _rgb(x, y, t)
      out[i + 1] = s_char(r * 255, g * 255, b * 255, 255)
      x = x + 1
      if x == width then x = 0; y = y + 1 end
      
      -- Pixel 3
      r, g, b = _rgb(x, y, t)
      out[i + 2] = s_char(r * 255, g * 255, b * 255, 255)
      x = x + 1
      if x == width then x = 0; y = y + 1 end
      
      -- Pixel 4
      r, g, b = _rgb(x, y, t)
      out[i + 3] = s_char(r * 255, g * 255, b * 255, 255)
      x = x + 1
      if x == width then x = 0; y = y + 1 end
      
      i = i + 4
      remaining = remaining - 4
    end
    
    -- remaining :3
    while remaining > 0 do
      r, g, b = _rgb(x, y, t)
      out[i] = s_char(r * 255, g * 255, b * 255, 255)
      
      x = x + 1
      if x == width then
        x = 0
        y = y + 1
      end
      
      i = i + 1
      remaining = remaining - 1
    end

    return t_concat(out)
  end
end
