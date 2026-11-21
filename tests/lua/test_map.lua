-- The map and scene API: creating, inspecting and changing entities and
-- brushes, saving and loading maps. Run by './dev.py test' with the test's
-- temporary directory as the first argument.

local workdir = ...
local map = luadiant.map

-- asserts that fn( ... ) raises an error containing the plain text pattern
local function assert_error(pattern, fn, ...)
	local ok, message = pcall(fn, ...)
	assert(not ok, "expected an error containing: " .. pattern)
	assert(tostring(message):find(pattern, 1, true), "unexpected error: " .. tostring(message))
end

local function count(t)
	local n = 0
	for _ in pairs(t) do
		n = n + 1
	end
	return n
end

-- vectors
local v = luadiant.vec3(1, 2, 3)
assert(luadiant.is_vec3(v) and not luadiant.is_vec3({ 1, 2, 3 }))
assert(tostring(v) == "(1 2 3)", tostring(v))
assert(v + { 1, 1, 1 } == luadiant.vec3(2, 3, 4))
assert(v - v == luadiant.vec3(0, 0, 0))
assert(2 * v == v * 2 and v * 2 == luadiant.vec3(2, 4, 6))
assert(v / 2 == luadiant.vec3(0.5, 1, 1.5) and -v == luadiant.vec3(-1, -2, -3))
assert(v:dot({ x = 1, y = 1, z = 1 }) == 6)
assert(luadiant.vec3(1, 0, 0):cross({ 0, 1, 0 }) == luadiant.vec3(0, 0, 1))
assert(luadiant.vec3(3, 0, 4):length() == 5 and luadiant.vec3(3, 0, 4):normalized():near({ 0.6, 0, 0.8 }))
assert(v[1] == 1 and v[2] == 2 and v[3] == 3 and select(3, v:unpack()) == 3)
assert(luadiant.vec3({ x = 1, y = 2, z = 3 }) == v)
assert_error("vector", function() return v + 1 end)

-- headless scripts start on a new, unmodified map
assert(map.unnamed() and not map.modified())

-- the worldspawn is created on demand, handles of a node are identical
local world = map.worldspawn()
assert(world:kind() == "entity" and world:classname() == "worldspawn")
assert(world:is_group() and world == map.worldspawn())
assert(world:parent() == nil and world:valid())

-- entities with keys
local light = luadiant.create_entity("light", { origin = "8 16 32", light = 300 })
assert(light:classname() == "light" and not light:is_group())
assert(light:get("origin") == "8 16 32" and light:get("light") == "300")
assert(light:get("missing") == nil)
light:set("_color", "1 0.5 0")
light:set("light", nil)
local keys = light:keys()
assert(keys.classname == "light" and keys._color == "1 0.5 0" and keys.light == nil, "keys")
assert(count(keys) == 3, "classname, origin and _color")
assert_error("classname", light.set, light, "classname", "info_null")
assert_error("expected", luadiant.create_entity)

-- boxes go into the worldspawn by default, or into a group entity
local box = luadiant.create_box({ 0, 0, 0 }, { 64, 32, 16 }, "textures/base/wall")
assert(box:kind() == "brush" and box:parent() == world)
local mins, maxs = box:bounds()
assert(mins == luadiant.vec3(0, 0, 0) and maxs == luadiant.vec3(64, 32, 16), tostring(mins) .. tostring(maxs))
assert(box:shader() == "textures/base/wall" and not box:is_detail())
assert(#box:faces() == 6)

local trigger = luadiant.create_entity("trigger_multiple", { wait = "1" })
local trigger_brush = luadiant.create_box({ 128, 0, 0 }, { 192, 64, 64 }, "textures/common/trigger", trigger)
assert(trigger_brush:parent() == trigger)
assert(#trigger:primitives() == 1 and trigger:primitives()[1] == trigger_brush)
assert_error("group", luadiant.create_box, { 0, 0, 0 }, { 8, 8, 8 }, nil, light)
assert_error("smaller than max", luadiant.create_box, { 0, 0, 0 }, { 0, 8, 8 })

-- a brush from planes: a wedge; points are clockwise seen from outside
local wedge = luadiant.create_brush({
	{ { 0, 0, 0 }, { 64, 0, 0 }, { 0, 64, 0 }, shader = "textures/base/floor" }, -- bottom
	{ { 0, 0, 0 }, { 0, 64, 0 }, { 0, 0, 64 } }, -- x = 0
	{ { 0, 0, 0 }, { 0, 0, 64 }, { 64, 0, 0 } }, -- y = 0
	{ { 0, 64, 0 }, { 64, 64, 0 }, { 0, 64, 64 } }, -- y = 64
	{ { 64, 0, 0 }, { 0, 0, 64 }, { 64, 64, 0 } }, -- slope
})
assert(wedge:kind() == "brush" and #wedge:faces() == 5)
local wmins, wmaxs = wedge:bounds()
assert(wmins == luadiant.vec3(0, 0, 0) and wmaxs == luadiant.vec3(64, 64, 64), tostring(wmins) .. tostring(wmaxs))
assert(wedge:shader() == nil, "the faces have different shaders")
assert_error("at least 4 planes", luadiant.create_brush, { { { 0, 0, 0 }, { 64, 0, 0 }, { 0, 64, 0 } } })

-- faces
local top
for _, face in ipairs(box:faces()) do
	local normal, dist = face:plane()
	assert(luadiant.is_vec3(normal) and #face:winding() == 4 and #face:points() == 3)
	assert(face:brush() == box)
	if normal == luadiant.vec3(0, 0, 1) then
		assert(dist == 16)
		top = face
	end
end
assert(top, "the box has a top face")
assert(box:faces()[1] == box:faces()[1] and box:faces()[1] ~= box:faces()[2], "faces compare by identity")
top:set_shader("textures/base/floor")
assert(top:shader() == "textures/base/floor" and box:shader() == nil)
top:set_texdef({ shift = { 8, 4 }, rotate = 90 })
local texdef = top:texdef()
assert(texdef.shift[1] == 8 and texdef.shift[2] == 4 and texdef.rotate == 90, "texdef")
box:set_shader("textures/base/wall")
assert(top:shader() == "textures/base/wall")

-- fitting the wall texture (a real image from tests/lua/engine, loaded
-- headless as well) twice horizontally halves the horizontal scale
top:set_texdef({ rotate = 0 })
top:fit()
local fitted = top:texdef()
top:fit(2, 1)
texdef = top:texdef()
assert(math.abs(texdef.scale[1] - fitted.scale[1] / 2) < 1e-4 and math.abs(texdef.scale[2] - fitted.scale[2]) < 1e-4,
	("fitted scales %g %g, then %g %g"):format(fitted.scale[1], fitted.scale[2], texdef.scale[1], texdef.scale[2]))

box:set_detail(true)
assert(box:is_detail())
box:set_detail(false)

-- selection
luadiant.select_none()
assert(#luadiant.selection() == 0)
box:select()
assert(box:selected() and #luadiant.selection() == 1 and luadiant.selection()[1] == box)
box:select(false)
assert(not box:selected())

-- deleting invalidates the handle, but not other ones
local doomed = luadiant.create_box({ 256, 0, 0 }, { 264, 8, 8 })
local doomed_face = doomed:faces()[1]
doomed:delete()
assert(not doomed:valid() and box:valid())
assert_error("not part of the map", doomed.bounds, doomed)
assert_error("not part of the map", doomed_face.shader, doomed_face)

-- saving does not rename the map, so it can be saved to several files
assert(map.modified())
local path = workdir .. "/test.map"
map.save(path)
assert(map.unnamed() and map.modified(), "saving a copy keeps the map unnamed and modified")
assert_error("no file name", map.save)
assert_error("unsupported map file type", map.save, workdir .. "/test.unknown")

local file = assert(io.open(path))
local text = file:read("*a")
file:close()
assert(text:find('"classname" "trigger_multiple"', 1, true), "the file contains the trigger")
assert(text:find("base/floor", 1, true), "the file contains the shaders")

-- loading replaces the map and invalidates all handles
map.load(path)
assert(not box:valid() and not light:valid())
assert(map.name() == path and not map.unnamed() and not map.modified())
local entities = map.entities()
assert(#entities == 3, "worldspawn, light and trigger, got " .. #entities)
local by_class = {}
for _, entity in ipairs(entities) do
	by_class[entity:classname()] = entity
end
assert(by_class.light:get("origin") == "8 16 32" and by_class.light:get("_color") == "1 0.5 0")
assert(#by_class.trigger_multiple:primitives() == 1)
assert(#map.worldspawn():primitives() == 2, "box and wedge")

-- saving under the map's own name marks it unmodified
by_class.light:set("light", "200")
assert(map.modified())
map.save()
assert(not map.modified())

assert_error("map file not found", map.load, workdir .. "/missing.map")

map.new()
assert(map.unnamed() and #map.entities() == 0)
