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

return luadiant
