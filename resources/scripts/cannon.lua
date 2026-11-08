-- Example pattern script: lobbed mortar shells.
--
-- Point a pattern at it with `script cannon` in resources/patterns.txt.
-- The game calls spawn(ctx) once per shot and fires whatever bullets the
-- returned table lists. Angles are degrees, 0 = right, 90 = down.
--
-- ctx gives you: width, height, player_x, player_y, shot, count, spread,
--                speed, radius, gravity -- the pattern's own values.

local function aim_at(from_x, from_y, ctx)
    local dx = ctx.player_x - from_x
    local dy = ctx.player_y - from_y
    return math.deg(math.atan(dy, dx))
end

function spawn(ctx)
    local bullets = {}
    local n = math.max(1, math.floor(ctx.count))

    for i = 1, n do
        -- fan the shells out around the aim line, then take a little off the
        -- speed so gravity becomes the star of the show
        local t = (n > 1) and ((i - 1) / (n - 1) - 0.5) or 0
        local x = ctx.width * (0.1 + 0.8 * (i - 1) / math.max(1, n - 1))
        local angle = aim_at(x, 0, ctx) - 55 + t * ctx.spread
        bullets[#bullets + 1] = {
            x = x,
            y = 0,
            angle = angle,
            speed = ctx.speed * (0.75 + 0.25 * ((ctx.shot + i) % 3) / 2),
        }
    end

    return bullets
end
