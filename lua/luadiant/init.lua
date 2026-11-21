-- The public luadiant API, available to all scripts as the global
-- `luadiant` and via require "luadiant".
--
-- It is the C++ core module (luadiant.core) extended by the helpers which
-- are easier to write in Lua; the submodules below lua/luadiant/ are
-- loaded here and exposed as fields of this table.

local luadiant = require "luadiant.core"

local vec3 = require "luadiant.vec3"
luadiant.vec3 = vec3.new
luadiant.is_vec3 = vec3.is_vec3

-- plugins; the internals used by the loader are not part of the API
local plugin = require "luadiant.plugin"
luadiant.plugins = plugin.loaded
luadiant.load_plugin = plugin.load
luadiant.load_plugins = plugin.load_all
luadiant.plugin_action = nil
luadiant.drop_plugin_actions = nil

return luadiant
