-- Example plugin used by test_plugins.lua: a few actions which create
-- boxes, with all the options of plugin:action.

local plugin = ...

-- a field offered to other code via luadiant.plugins.example
plugin.boxes_created = 0

plugin:action{
	id = "box",
	title = "Create &Box",
	menu = "Examples/Boxes",
	shortcut = "Ctrl+Alt+Shift+B",
	run = function()
		luadiant.create_box({ 0, 0, 0 }, { 64, 64, 64 })
		plugin.boxes_created = plugin.boxes_created + 1
	end,
}

-- only the required fields: title defaults to the id, menu to the
-- plugin's name and there is no default shortcut
plugin:action{
	id = "minimal",
	run = function() end,
}
