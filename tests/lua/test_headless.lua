-- The environment headless scripts run in: run by './dev.py test' with the
-- test's temporary directory as the first argument.

local workdir = ...
assert(type(workdir) == "string" and workdir ~= "", "expected the test directory as argument")
assert(arg[1] == workdir, "the arguments are also passed in the global arg table")
assert(arg[0]:match("test_headless%.lua$"), "arg[0] is the script path")

assert(luadiant.headless == true, "luadiant.headless")
assert(type(luadiant.version) == "string" and luadiant.version ~= "")
assert(luadiant.luajit_version:match("^LuaJIT 2%.1"), luadiant.luajit_version)
assert(jit and jit.version == luadiant.luajit_version, "the jit module matches")

-- the test game from tests/lua/gamepack
local game = luadiant.game()
assert(game.file == "luadiant-test.game", game.file)
assert(game.name == "LuaDiant test game", game.name)
assert(game.type == "q3", game.type)
assert(game.basegame == "base", game.basegame)

local function ends_with_slash(path)
	return type(path) == "string" and path:sub(-1) == "/"
end
assert(ends_with_slash(luadiant.app_path()), luadiant.app_path())
assert(ends_with_slash(luadiant.settings_path()), luadiant.settings_path())
assert(ends_with_slash(luadiant.engine_path()), luadiant.engine_path())
assert(luadiant.engine_path():find(workdir, 1, true) == 1, "--engine-path is applied")

-- modules are found in the installed lua/ directory first
local directories = luadiant.script_directories()
assert(#directories == 2, "shipped and user script directories")
assert(directories[1] == luadiant.app_path() .. "lua/", directories[1])
assert(package.loaded["luadiant.core"], "luadiant.core is registered")
assert(package.loaded.luadiant == luadiant, "the global luadiant is the luadiant module")
assert(package.path:find(directories[1] .. "?.lua", 1, true) == 1, package.path)

-- the logging functions must not raise
luadiant.log("log", 1, nil, true)
luadiant.warn("warning")
luadiant.log_error("not an error, just testing log_error")
