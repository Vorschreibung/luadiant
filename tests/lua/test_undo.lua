-- Undo integration: implicit operations, undo blocks with nesting and
-- rollback, undo/redo and the modified state of the map. Run by
-- './dev.py test' with the test's temporary directory as the first
-- argument.

local workdir = ...
local map = luadiant.map
local create_box = luadiant.create_box

local function box_at(x)
	return create_box({ x, 0, 0 }, { x + 8, 8, 8 })
end

-- changes outside of undo blocks form one operation until undo, redo or
-- an undo block close it
local a, b = box_at(0), box_at(16)
assert(luadiant.undo() == true)
assert(not a:valid() and not b:valid(), "both boxes were one operation")
assert(luadiant.redo() == true)
assert(a:valid() and b:valid(), "redo restores the very same nodes")
assert(luadiant.redo() == false, "nothing left to redo")

-- an undo block is one operation of its own
local c, d
luadiant.undo_block("two boxes", function()
	c, d = box_at(32), box_at(48)
end)
assert(luadiant.undo() == true)
assert(not c:valid() and not d:valid() and a:valid(), "the block was undone, not the earlier changes")
assert(luadiant.redo() == true and c:valid() and d:valid())

-- arguments are passed on and results returned
local x, y = luadiant.undo_block("values", function(p, q)
	return p + 1, q
end, 1, "q")
assert(x == 2 and y == "q")

-- an error rolls the block back, including deletions, and is rethrown as
-- it is, so it can be caught as usual
local e
local ok, message = pcall(luadiant.undo_block, "fails", function()
	e = box_at(64)
	a:delete()
	error("boom")
end)
assert(not ok and message:find("boom", 1, true), tostring(message))
assert(not e:valid() and a:valid(), "the block was rolled back")
assert(luadiant.redo() == false, "a rolled back block cannot be redone")

local error_object = {}
ok, message = pcall(luadiant.undo_block, "fails", function()
	error(error_object)
end)
assert(not ok and message == error_object, "error values are kept")

-- only the outermost block rolls back; nested blocks join it
local f, g
luadiant.undo_block("outer", function()
	f = box_at(80)
	local inner_ok = pcall(luadiant.undo_block, "inner", function()
		g = box_at(96)
		error("caught by the outer block")
	end)
	assert(not inner_ok)
	assert(g:valid(), "a nested block does not roll back on its own")
end)
assert(luadiant.undo() == true)
assert(not f:valid() and not g:valid(), "the nested block was part of the outer one")
assert(luadiant.redo() == true)

-- undo history and map changes are not possible inside undo blocks
local function assert_not_in_block(fn, ...)
	local args = { ... }
	local block_ok, block_message = pcall(luadiant.undo_block, "not allowed", function()
		fn(unpack(args))
	end)
	assert(not block_ok and tostring(block_message):find("inside an undo block", 1, true), tostring(block_message))
end
assert_not_in_block(luadiant.undo)
assert_not_in_block(luadiant.redo)
assert_not_in_block(map.new)
assert_not_in_block(map.save, workdir .. "/blocked.map")
local path = workdir .. "/undo.map"
map.save(path)
assert_not_in_block(map.load, path)

assert(pcall(luadiant.undo_block) == false, "the name and function are required")
assert(pcall(luadiant.undo_block, "name") == false, "the function is required")

-- the modified state follows undo and redo
map.load(path)
assert(not map.modified())
map.worldspawn():set("message", "changed")
assert(map.modified())
assert(luadiant.undo() == true)
assert(not map.modified() and map.worldspawn():get("message") == nil, "back at the saved state")
assert(luadiant.redo() == true)
assert(map.modified() and map.worldspawn():get("message") == "changed")

-- loading a map clears the history
map.load(path)
assert(luadiant.undo() == false, "nothing to undo after loading")
