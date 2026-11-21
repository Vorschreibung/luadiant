# AGENTS.md

LuaDiant: NetRadiant-custom fork with deep LuaJIT scripting. Binary: `luadiant`.

## Goals
- Headless: `luadiant --headless [--game NAME] [--gamepacks DIR] script.lua [args]` to load/modify/save maps, run build tools, CI.
- Editor: Lua plugins (`lua/plugins/*/init.lua` in AppPath and SettingsPath) add actions with menus and rebindable shortcuts; Reload Scripts / Run Script.

## Approach
- `vendor/luajit` built statically by the Makefile; `vendor/xxhash` unused for now.
- Code in `radiant/lua/`, Lua stdlib in `lua/` (installed to `install/lua/`).
- Plain Lua C API, no binding library. Scene nodes are refcounted userdata handles, validated through scene instances.
- No `-fexceptions`: Lua errors skip C++ destructors. Never hold RAII objects across Lua-raising calls, and never call Lua inside C++ traversals (collect first).
- Lua changes are undoable: an implicit undo op opens on the first change, `luadiant.undo_block` nests, errors roll back.
- Lua actions register as `Lua.<plugin>.<id>` commands before `user_shortcuts_init()`.
- Headless means no dialogs or GL: offscreen Qt, non-interactive message boxes, and prefs are not saved. Shaders and textures are realised without GL (guard GL calls with `GlobalOpenGL().contextValid`); UI windows never exist, so their callbacks must handle that.

## Workflow
- `./dev.py build`, `./dev.py test`; headless Lua tests (`tests/lua/test_*.lua`, exit 0 = pass) use the minimal game in `tests/lua/gamepack` with game data from `tests/lua/engine` and run in CI.
- One commit per finished change; comment non-trivial code.
