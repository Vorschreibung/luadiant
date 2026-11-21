-- Plugin loading and action registration. Headless luadiant does not load
-- plugins by itself, so the plugins under tests/lua/plugins/ are loaded
-- explicitly. Run by './dev.py test' with the test's temporary directory
-- as the first argument.

local here = arg[0]:match("^(.*/)") or "./"
local plugins_dir = here .. "plugins/"

local function find_action(command)
	for _, action in ipairs(luadiant.actions()) do
		if action.command == command then
			return action
		end
	end
end

local function expect_error(pattern, fn, ...)
	local ok, message = pcall(fn, ...)
	assert(not ok, "expected an error matching " .. pattern)
	assert(tostring(message):find(pattern), tostring(message))
end

-- directory listings are sorted and without "." and ".."
local entries = luadiant.list_directory(plugins_dir)
assert(#entries == 2 and entries[1] == "broken" and entries[2] == "example", table.concat(entries, ","))
local missing, message = luadiant.list_directory(plugins_dir .. "missing")
assert(missing == nil and message:find("not a directory", 1, true), tostring(message))

-- nothing is loaded headless unless asked for
assert(next(luadiant.plugins) == nil, "no plugins loaded by default")
assert(#luadiant.actions() == 0, "no actions registered by default")

-- loading a plugin registers its actions as Lua.<plugin>.<id>
local example = luadiant.load_plugin(plugins_dir .. "example")
assert(example.name == "example" and example.dir == plugins_dir .. "example/", example.dir)
assert(luadiant.plugins.example == example, "luadiant.plugins has the loaded plugins")
expect_error("already loaded", luadiant.load_plugin, plugins_dir .. "example/")

local actions = luadiant.actions()
assert(#actions == 2, "two actions")
local box = actions[1]
assert(box.command == "Lua.example.box", box.command)
assert(box.plugin == "example" and box.id == "box")
assert(box.title == "Create &Box" and box.menu == "Examples/Boxes")
assert(box.shortcut == "Ctrl+Alt+Shift+B", box.shortcut)
assert(type(box.run) == "function")

local minimal = actions[2]
assert(minimal.command == "Lua.example.minimal", minimal.command)
assert(minimal.title == "minimal", "the title defaults to the id")
assert(minimal.menu == "example", "the menu defaults to the plugin name")
assert(minimal.shortcut == "", "no default shortcut")

-- the actions' functions are the plugin's own
local count = #luadiant.map.entities()
box.run()
assert(example.boxes_created == 1)
assert(luadiant.undo() == true, "the box creation can be undone")

-- invalid action definitions are rejected with an error at the caller
expect_error("must be called on a plugin", example.action, {}, { id = "x", run = print })
expect_error("id must be", example.action, example, { run = print })
expect_error("id must be", example.action, example, { id = "a.b", run = print })
expect_error("title must be", example.action, example, { id = "x", title = 1, run = print })
expect_error("menu must be", example.action, example, { id = "x", menu = {}, run = print })
expect_error("shortcut must be", example.action, example, { id = "x", shortcut = "Ctlr+Q", run = print })
expect_error("run must be", example.action, example, { id = "x" })
expect_error("already registered", example.action, example, { id = "box", run = print })
expect_error("^[^\n]*test_plugins%.lua:%d+: action x", function()
	example:action{ id = "x" }
end)
assert(find_action("Lua.example.x") == nil, "nothing registered by failed calls")

-- actions can also be added after loading
assert(example:action{ id = "later", run = print } == "Lua.example.later")
assert(find_action("Lua.example.later"), "registered later")

-- a plugin failing to load loses the actions it registered
expect_error("broken on purpose", luadiant.load_plugin, plugins_dir .. "broken")
assert(luadiant.plugins.broken == nil, "the broken plugin is not loaded")
assert(find_action("Lua.broken.orphan") == nil, "its action was dropped")
-- and can be loaded again, e.g. after being fixed
expect_error("broken on purpose", luadiant.load_plugin, plugins_dir .. "broken")

-- plugin names are part of command names and restricted like ids
expect_error("invalid plugin name", luadiant.load_plugin, plugins_dir .. "example", "a.b")
expect_error("expected the plugin's directory", luadiant.load_plugin)
expect_error("init%.lua", luadiant.load_plugin, plugins_dir .. "missing")

-- reloading is up to the editor's event loop
expect_error("not available in headless mode", luadiant.reload_scripts)

-- the core internals of the loader are not part of the API
assert(luadiant.plugin_action == nil and luadiant.drop_plugin_actions == nil)
assert(#luadiant.map.entities() == count, "the map is back to how it was")
