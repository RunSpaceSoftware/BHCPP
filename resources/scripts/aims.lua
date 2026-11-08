-- Built-in aim logic for every pattern that does not name its own script.
--
-- Each function gets ctx (what pattern_script.c fills in: the pattern's fields
-- plus width/height/player/shot/spin) and returns a list of bullets:
--     { x = , y = , angle = , speed = }     -- angle in degrees, 0 = right
-- Radius, damage, colour, sprite and gravity come from the pattern itself.
--
-- angles in ctx are degrees (like patterns.txt); everything here works in
-- radians, so each function converts up front.

local TAU = math.pi * 2

local function rnd(a, b) return a + (b - a) * math.random() end

local function clamp(v, lo, hi)
  if v < lo then return lo end
  if v > hi then return hi end
  return v
end

-- degrees -> radians for the four angle fields
local function units(c)
  return math.rad(c.angle), math.rad(c.spin), math.rad(c.spread), math.rad(c.jitter)
end

local function emit(out, x, y, a, speed)
  out[#out + 1] = { x = x, y = y, angle = math.deg(a), speed = speed }
end

-- ctx.gap: 0 = none, -1 = random slot, n = leave slot n-1 empty
local function gap_slot(gap, n)
  if gap == -1 then return math.random(0, n - 1) end
  if gap > 0 then return gap - 1 end
  return -1
end

local function edge_point(c)
  local k = math.random(0, 3)
  if k == 0 then return rnd(0, c.width), -c.margin end
  if k == 1 then return c.width + c.margin, rnd(0, c.height) end
  if k == 2 then return rnd(0, c.width), c.height + c.margin end
  return -c.margin, rnd(0, c.height)
end

local function origin_point(c)
  local o = c.origin
  if o == "edge" then return edge_point(c) end
  if o == "top" then return rnd(0, c.width), -c.margin end
  if o == "bottom" then return rnd(0, c.width), c.height + c.margin end
  if o == "left" then return -c.margin, rnd(0, c.height) end
  if o == "right" then return c.width + c.margin, rnd(0, c.height) end
  if o == "point" then
    -- point coords are written for base_w x base_h, so scale with the window
    return c.point_x * c.width / c.base_w, c.point_y * c.height / c.base_h
  end
  return c.player_x, c.player_y
end

local function heading(c, ox, oy, angle)
  if c.has_angle then return angle end
  return math.atan(c.player_y - oy, c.player_x - ox)
end

local function base_angle(c, ox, oy, angle, spin)
  if c.aim == "player" then
    return math.atan(c.player_y - oy, c.player_x - ox)
  elseif c.aim == "angle" then
    if not c.has_angle and c.spin == 0 then
      local o = c.origin
      if o == "top" then return math.pi / 2 end
      if o == "bottom" then return -math.pi / 2 end
      if o == "left" then return 0 end
      if o == "right" then return math.pi end
      return 0
    end
    if c.spin ~= 0 then return spin end
    return angle
  elseif c.aim == "radial" then
    return math.atan(oy - c.height / 2, ox - c.width / 2)
  end
  return 0
end

-- the plain fan: player / angle / radial
local function fan(c)
  local angle, spin, spread, jitter = units(c)
  local ox, oy = origin_point(c)
  local base = base_angle(c, ox, oy, angle, spin)

  local n = c.count
  if n < 1 then n = 1 end
  if n > 256 then n = 256 end

  local angles = {}
  if c.aim == "radial" and math.abs(spread - TAU) < 1e-4 then
    local offset = 0
    if c.spin ~= 0 then offset = spin end
    for i = 1, n do angles[i] = TAU * (i - 1) / n + offset end
  elseif c.aim == "angle" and spread == 0 and n > 1 then
    for i = 1, n do angles[i] = base + TAU * (i - 1) / n end
  elseif n == 1 then
    angles[1] = base
  else
    for i = 1, n do angles[i] = base + spread * ((i - 1) / (n - 1) - 0.5) end
  end

  local out = {}
  for i = 1, n do
    local a = angles[i]
    if jitter ~= 0 then a = a + rnd(-jitter, jitter) end
    emit(out, ox, oy, a, c.speed)
  end
  return out
end

local function wall_from(c, side, n, gap, angle)
  local out = {}
  for i = 0, n - 1 do
    if i ~= gap then
      local t = (i + 0.5) / n
      local x, y, a
      if side == "top" then x, y, a = c.width * t, -c.margin, math.pi / 2
      elseif side == "bottom" then x, y, a = c.width * t, c.height + c.margin, -math.pi / 2
      elseif side == "left" then x, y, a = -c.margin, c.height * t, 0
      else x, y, a = c.width + c.margin, c.height * t, math.pi end
      emit(out, x, y, c.has_angle and angle or a, c.speed)
    end
  end
  return out
end

-- the game reads this table: aims[<aim name>](ctx)
aims = {}

aims.player = fan
aims.angle = fan
aims.radial = fan

function aims.wall(c)
  local angle = units(c)
  local n = c.count
  if n < 1 then n = 1 end
  return wall_from(c, c.side, n, gap_slot(c.gap, n), angle)
end

-- the same wall, opposite side, every other shot
function aims.alternate(c)
  local angle = units(c)
  local n = c.count
  if n < 1 then n = 1 end
  local side = "left"
  if c.shot % 2 == 1 then side = "right" end
  return wall_from(c, side, n, gap_slot(c.gap, n), angle)
end

function aims.rain(c)
  local angle, _, _, jitter = units(c)
  local side = c.origin
  local def
  if side == "bottom" then def = -math.pi / 2
  elseif side == "left" then def = 0
  elseif side == "right" then def = math.pi
  else side = "top"; def = math.pi / 2 end

  local dir = def
  if c.has_angle then dir = angle end

  local out = {}
  for _ = 1, c.count do
    local x, y
    if side == "top" then x, y = rnd(0, c.width), -c.margin
    elseif side == "bottom" then x, y = rnd(0, c.width), c.height + c.margin
    elseif side == "left" then x, y = -c.margin, rnd(0, c.height)
    else x, y = c.width + c.margin, rnd(0, c.height) end
    emit(out, x, y, dir + rnd(-jitter, jitter), c.speed)
  end
  return out
end

function aims.crossfire(c)
  local out = {}
  local xs = { -c.margin, c.width + c.margin }
  for i = 1, 2 do
    local y = rnd(0, c.height)
    emit(out, xs[i], y, math.atan(c.player_y - y, c.player_x - xs[i]), c.speed)
  end
  return out
end

-- spawn on a ring around the player, then converge inward
function aims.encircle(c)
  local angle, spin, _, _ = units(c)
  local r = c.spawn_radius
  local cx = clamp(c.player_x, c.margin + r, c.width - c.margin - r)
  local cy = clamp(c.player_y, c.margin + r, c.height - c.margin - r)
  local offset = 0
  if c.has_angle then offset = angle end
  offset = offset + spin

  local n = c.count
  if n < 1 then n = 1 end
  local gap = gap_slot(c.gap, n)

  local out = {}
  for i = 0, n - 1 do
    if i ~= gap then
      local a = offset + TAU * i / n
      local x = cx + math.cos(a) * r
      local y = cy + math.sin(a) * r
      -- on top of the player: leave that side open
      if math.sqrt((x - c.player_x) ^ 2 + (y - c.player_y) ^ 2) >= c.min_radius then
        emit(out, x, y, math.atan(c.player_y - y, c.player_x - x), c.speed)
      end
    end
  end
  return out
end

aims.cross = aims.encircle

-- a long thin column of bullets that reads as one piercing shot
function aims.spear(c)
  local angle, spin, spread, jitter = units(c)
  local ox, oy = origin_point(c)
  local base = heading(c, ox, oy, angle) + spin

  local n = c.count
  if n < 1 then n = 1 end
  if n > 256 then n = 256 end

  -- bullets sit behind the head, spaced along the shaft
  local step = 0
  if n > 1 then step = c.length / (n - 1) end
  local dx = math.cos(base) * step
  local dy = math.sin(base) * step

  local out = {}
  for i = 0, n - 1 do
    local a = base
    if spread ~= 0 and n > 1 then a = a + spread * (i / (n - 1) - 0.5) end
    if jitter ~= 0 then a = a + rnd(-jitter, jitter) end
    emit(out, ox - dx * i, oy - dy * i, a, c.speed)
  end
  return out
end

-- segments vertices on a ring of radius length; each fires a fanned group
function aims.formation(c)
  local angle, spin, spread, jitter = units(c)
  local ox, oy = origin_point(c)

  local segs = clamp(c.segments, 1, 64)
  local n = clamp(c.count, 1, 64)
  local aimed = not c.has_angle
  local pinned = heading(c, ox, oy, angle)

  local out = {}
  for s = 0, segs - 1 do
    local va = TAU * s / segs + spin
    local vx = ox + math.cos(va) * c.length
    local vy = oy + math.sin(va) * c.length
    local aim = pinned
    if aimed then aim = math.atan(c.player_y - vy, c.player_x - vx) end

    for i = 0, n - 1 do
      local a = aim
      if n > 1 then a = a + spread * (i / (n - 1) - 0.5) end
      if jitter ~= 0 then a = a + rnd(-jitter, jitter) end
      emit(out, vx, vy, a, c.speed)
    end
  end
  return out
end
