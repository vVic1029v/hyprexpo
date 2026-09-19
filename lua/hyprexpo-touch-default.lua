-- SHIPPED REFERENCE (read-only mirror of a live config; do not edit here).
-- touch/hyprexpo.lua — DEFAULT hyprexpo touch layer (Phase 2): the touch
-- decision state machine, ported 1:1 from the C++ touch path. With this
-- loaded every handler consumes, so the C++ touch path idles (it stays as
-- the fallback for configs without this file — same behavior either way).
--
-- Thresholds mirror plugin:hyprexpo:* and C++ defaults (see
-- OverviewInteraction.cpp): HOLD_MS 350, DRAG_PX 12, SLOP 20
-- (touch_hold_slop_px), drag-drop assumed enabled.
-- Timers use hl.timer oneshot + generation counters (a stale timer is a
-- no-op instead of needing cancellation, same as the C++ self-check).
-- Coordinates everywhere are global compositor px (faucet contract).

if not hl.plugin.hyprexpo then return end
local H = hl.plugin.hyprexpo

local HOLD_MS, DRAG_PX, SLOP = 350, 12.0, 20.0

local press = nil
local gen   = 0

local function dist2(p)
  local dx, dy = p.x - p.x0, p.y - p.y0
  return dx * dx + dy * dy
end

H.on("touchdown", function(ev)
  press = { id = ev.id, x0 = ev.x, y0 = ev.y, x = ev.x, y = ev.y,
            region = H.region_at(ev.x, ev.y),
            panning = false, dragging = false }
  gen       = gen + 1
  press.gen = gen
  if press.region == "grid" then H.drawer_down(ev.x, ev.y) end
  H.hover_at(ev.x, ev.y)
  local g, id = gen, ev.id
  hl.timer(function()
    local p = press
    if not p or p.gen ~= g or p.id ~= id or p.dragging or p.panning then return end
    if dist2(p) >= SLOP * SLOP then return end -- drifted: pan/scroll owns it
    if p.region ~= "ribbon" then return end    -- grid holds pull instead
    if H.drag_begin(p.x, p.y) then p.dragging = true end
  end, { timeout = HOLD_MS, type = "oneshot" })
  H.consume()
end)

H.on("touchmotion", function(ev)
  local p = press
  if not p or p.id ~= ev.id then return end
  p.x, p.y = ev.x, ev.y
  if p.dragging then
    H.drag_update(ev.x, ev.y)
    H.focus_at(ev.x, ev.y) -- touch hover-select (touchscreen-only feature)
    H.consume()
    return
  end
  if p.region == "grid" then
    local dx, dy = ev.x - p.x0, ev.y - p.y0
    H.drawer_motion(ev.dy, math.sqrt(dx * dx + dy * dy))
    H.consume()
    return
  end
  if p.region ~= "ribbon" then return end -- Search: hold still
  if not p.panning then
    local dx, dy = ev.x - p.x0, ev.y - p.y0
    if math.abs(dx) > math.abs(dy) and math.abs(dx) >= DRAG_PX then
      p.panning = true -- hold timer sees the shared table and stands down
    else
      H.hover_at(ev.x, ev.y)
    end
  end
  if p.panning then
    H.ribbon_pan(ev.dx)
    H.hover_at(ev.x, ev.y)
  end
  H.consume()
end)

H.on("touchup", function(ev)
  local p = press
  if not p or p.id ~= ev.id then return end
  press = nil
  if p.region == "grid" then
    H.drawer_up(ev.x, ev.y, ev.x - p.x0, ev.y - p.y0)
  elseif p.region == "search" then
    H.focus_search()
  elseif p.panning then
    H.ribbon_release()
  elseif p.dragging then
    H.drag_end()
  else
    local dx, dy = ev.x - p.x0, ev.y - p.y0
    if dx * dx + dy * dy < DRAG_PX * DRAG_PX then
      H.tap_select(ev.x, ev.y)
    end
  end
  H.consume()
end)

H.on("touchcancel", function(ev)
  local p = press
  if not p or p.id ~= ev.id then return end
  press = nil
  if p.region == "grid" then H.drawer_cancel() end
  if p.dragging then H.drag_cancel() end
  H.consume()
end)
