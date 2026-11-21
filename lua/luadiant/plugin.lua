-- Loading of Lua plugins, available as require "luadiant.plugin".
--
-- A plugin is a directory with an init.lua; the editor loads every
-- plugins/<name>/init.lua below the script directories on startup and
-- when the scripts are reloaded, a plugin in the user's directory replaces
-- a shipped one of the same name. init.lua is called with the plugin
-- object as its only argument:
--
--     local plugin = ...
--     plugin:action{
--         id = "hello",                 -- command Lua.<plugin>.hello
--         title = "Say &Hello",         -- menu item, default: the id
--         menu = "Examples/Greetings",  -- submenu of Lua, default: the plugin name
--         shortcut = "Ctrl+Alt+H",      -- default shortcut, rebindable
--         run = function() luadiant.log("hello") end,
--     }
--
-- The plugin object has the fields name and dir (with trailing slash);
-- plugins may add their own fields to offer functions to other code, all
-- loaded plugins are in luadiant.plugins by name.

local core = require "luadiant.core"

-- the luadiant module hides these internals of the core, keep them
local plugin_action = core.plugin_action
local drop_plugin_actions = core.drop_plugin_actions

local M = {}

-- loaded plugins by name
M.loaded = {}

local Plugin = {}
Plugin.__index = Plugin

-- plugin:action{ ... }, see above; returns the action's command name
Plugin.action = plugin_action

-- Names become part of command names, see luaplugins.h.
local function valid_name(name)
	return type(name) == "string" and name:match("^[%w_%-]+$") ~= nil
end

local function file_exists(path)
	local file = io.open(path, "rb")
	if file then
		file:close()
		return true
	end
	return false
end

-- Loads the plugin in directory dir (its init.lua) and returns the plugin
-- object; name defaults to the directory's name. If init.lua fails, the
-- actions it registered are dropped again and the error is raised.
function M.load(dir, name)
	if type(dir) ~= "string" or dir == "" then
		error("load_plugin: expected the plugin's directory", 2)
	end
	if dir:sub(-1) ~= "/" then
		dir = dir .. "/"
	end
	name = name or dir:match("([^/]+)/$")
	if not valid_name(name) then
		error(("load_plugin: invalid plugin name %q, only letters, digits, '_' and '-' are allowed"):format(tostring(name)), 2)
	end
	if M.loaded[name] then
		error(("load_plugin: a plugin called %s is already loaded"):format(name), 2)
	end

	local chunk, message = loadfile(dir .. "init.lua")
	if not chunk then
		error(message, 0)
	end

	local plugin = setmetatable({ name = name, dir = dir }, Plugin)
	M.loaded[name] = plugin
	local ok, err = xpcall(chunk, debug.traceback, plugin)
	if not ok then
		M.loaded[name] = nil
		drop_plugin_actions(name)
		error(err, 0)
	end
	return plugin
end

-- Loads all plugins of the script directories, in the order of their
-- names; a plugin failing to load is reported and does not affect others.
-- Returns the number of plugins loaded and failed.
function M.load_all()
	local directories, names = {}, {}
	for _, base in ipairs(core.script_directories()) do
		for _, name in ipairs(core.list_directory(base .. "plugins") or {}) do
			local dir = base .. "plugins/" .. name .. "/"
			if file_exists(dir .. "init.lua") then
				if not directories[name] then
					names[#names + 1] = name
				end
				-- later directories (the user's) take precedence
				directories[name] = dir
			end
		end
	end
	table.sort(names)

	local loaded, failed = 0, 0
	for _, name in ipairs(names) do
		local ok, err = pcall(M.load, directories[name], name)
		if ok then
			loaded = loaded + 1
			core.log(("Lua: loaded plugin %s from %s"):format(name, directories[name]))
		else
			failed = failed + 1
			core.log_error(("Lua: plugin %s failed to load: %s"):format(name, err))
		end
	end
	return loaded, failed
end

return M
