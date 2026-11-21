-- A plugin which fails while loading, after registering an action: the
-- action must be dropped again. Used by test_plugins.lua.

local plugin = ...

plugin:action{ id = "orphan", run = function() end }

error("broken on purpose")
