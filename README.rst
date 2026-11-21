LuaDiant
========

A fork of `NetRadiant-custom <https://github.com/Garux/netradiant-custom>`_
that deeply integrates a Lua scripting layer into the editor.

Scripts can drive the editor in two ways:

* **Headless**: load, inspect, modify and save maps and run the build
  tools without a GUI, e.g. for batch processing, automation and CI.
* **Integrated**: hook into the running editor's UI and plugin system to
  add commands, tools and workflows on top of the regular editing
  experience.

Apart from that it tracks upstream NetRadiant-custom closely. The original
upstream README is kept at `.legacy/README.md <.legacy/README.md>`_.

The editor binary is called ``luadiant``; Lua is `LuaJIT
<https://luajit.org/>`_ 2.1, built from ``vendor/luajit`` and linked
statically.

Headless scripts
----------------

::

    luadiant --headless [--game NAME] [--engine-path PATH] [--gamepacks DIR] \
             [-q] [--] script.lua [args...]

The script starts with an empty map and gets its arguments as ``...`` and
in the global ``arg`` table (``arg[0]`` is the script path). A number
returned by the script becomes the exit code, ``false`` or an error give
1. ``print`` writes to stdout, the editor log goes to stderr (``-q`` keeps
only its warnings and errors). Headless mode needs no display or OpenGL
and never saves preferences.

.. code-block:: lua

    -- caulk the back faces of every brush in a map
    local path = ...
    luadiant.map.load(path)
    for _, entity in ipairs(luadiant.map.entities()) do
        for _, primitive in ipairs(entity:primitives()) do
            if primitive:kind() == "brush" then
                for _, face in ipairs(primitive:faces()) do
                    local normal = face:plane()
                    if normal.z < -0.9 then
                        face:set_shader("textures/common/caulk")
                    end
                end
            end
        end
    end
    luadiant.map.save()

Plugins
-------

A plugin is a directory ``lua/plugins/<name>/`` with an ``init.lua``,
either next to the binary or in the user's settings directory
(``~/.luadiant/<version>/lua/plugins/``; it wins over a shipped plugin
of the same name). The editor loads all plugins on startup;
**Lua > Reload Scripts** recreates the Lua state and loads them again,
**Lua > Run Script...** runs a single script file.

``init.lua`` receives the plugin object and registers actions:

.. code-block:: lua

    local plugin = ...

    plugin:action{
        id = "caulk_selection",          -- command Lua.<plugin>.caulk_selection
        title = "&Caulk Selection",      -- menu item, default: the id
        menu = "Tools/Texturing",        -- submenu of Lua, default: the plugin name
        shortcut = "Ctrl+Alt+C",         -- default shortcut, optional
        run = function()
            for _, node in ipairs(luadiant.selection()) do
                if node:kind() == "brush" then
                    node:set_shader("textures/common/caulk")
                end
            end
        end,
    }

Every action is a regular editor command, so its shortcut can be changed
in **Edit > Shortcuts...** and is saved with the others. Changes made by
an action are one undo step named after its title and are rolled back if
the action raises an error. Errors are printed to the console.

Headless mode does not load plugins; scripts can load one with
``luadiant.load_plugin(dir)`` or all installed ones with
``luadiant.load_plugins()``.

Lua modules are searched in the shipped ``lua/`` directory first, then in
the user's, so plugins can ``require`` shared code from there.

API overview
------------

Everything lives in the global ``luadiant`` table (also
``require "luadiant"``):

* General: ``log``, ``warn``, ``log_error``, ``version``,
  ``luajit_version``, ``headless``, ``app_path()``, ``settings_path()``,
  ``engine_path()``, ``game()``, ``script_directories()``,
  ``list_directory(path)``.
* Map: ``map.new()``, ``map.load(path)``, ``map.save([path])``,
  ``map.name()``, ``map.unnamed()``, ``map.modified()``,
  ``map.worldspawn()``, ``map.entities()``.
* Creating: ``create_entity(classname[, keys])``,
  ``create_box(min, max[, shader[, entity]])``,
  ``create_brush(planes[, entity])``.
* Selection: ``selection()``, ``select_none()``, ``node:select([bool])``.
* Nodes (entities, brushes, patches): ``kind()``, ``valid()``,
  ``parent()``, ``bounds()``, ``selected()``, ``delete()``; entities:
  ``classname()``, ``get(key)``, ``set(key, value)``, ``keys()``,
  ``is_group()``, ``primitives()``; brushes: ``faces()``, ``is_detail()``,
  ``set_detail(bool)``, ``shader()``, ``set_shader(name)``; patches:
  ``size()``, ``control_point(column, row)``, ``shader()``,
  ``set_shader(name)``.
* Faces: ``brush()``, ``shader()``, ``set_shader(name)``, ``plane()``,
  ``points()``, ``winding()``, ``texdef()``, ``set_texdef{...}``,
  ``fit([s, t])``.
* Undo: ``undo_block(name, fn, ...)`` makes the changes of ``fn`` one undo
  step (rolled back on errors), ``undo()``, ``redo()``.
* Vectors: ``vec3(x, y, z)`` with arithmetic operators, ``dot``,
  ``cross``, ``length``, ``normalized``, ``distance``, ``near``;
  functions taking vectors also accept plain ``{x, y, z}`` tables.
* Plugins: ``plugins`` (loaded plugins by name), ``load_plugin(dir)``,
  ``load_plugins()``, ``actions()``, ``reload_scripts()`` (editor only).

The C++ side documents every function next to its implementation in
``radiant/lua/``; the tests in ``tests/lua/`` show them in use.

Development
-----------

See ``./dev.py --help`` for building, running and testing; ``./dev.py
test`` also runs the headless Lua tests (``tests/lua/test_*.lua``) against
a minimal bundled test game.
