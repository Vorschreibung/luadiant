/*
   Copyright (C) 2026 LuaDiant contributors.

   This file is part of LuaDiant.

   LuaDiant is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   LuaDiant is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with LuaDiant; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

// Lua plugins and their actions.
//
// A plugin is a directory lua/plugins/<name>/ with an init.lua, in the
// directory shipped with the application or in the user's settings
// directory (which wins if both have a plugin of the same name). init.lua
// receives the plugin object as `...` and registers actions with
//
//     plugin:action{ id = "...", title = "...", menu = "...",
//                    shortcut = "Ctrl+Shift+A", run = function() ... end }
//
// Every action becomes the editor command Lua.<plugin>.<id>, so its
// shortcut is rebindable in the shortcuts dialog and saved with the
// others, and gets an item in the Lua menu.
//
// Editor commands cannot be removed again, so the registry of actions
// lives as long as the process and survives reloading the scripts:
// reloading drops the Lua functions of all actions and the reloaded
// plugins attach new ones to the same commands. Actions which are not
// registered again stay inert, without shortcut or menu item.

#pragma once

#include "lua.hpp"

class QMenuBar;

/// Loads all plugins found in the script directories into the current Lua
/// state; problems are reported on the console.
void LuaPlugins_loadAll();

/// Forgets the Lua functions of all actions, called when the Lua state is
/// closed.
void LuaPlugins_stateClosed();

/// Registers the editor commands of the Lua menu (reloading the scripts,
/// running a script file); needs to happen before the shortcuts are loaded.
void LuaPlugins_registerCommands();

/// Creates the Lua menu with its commands and the items of all actions.
void LuaPlugins_createMenu( QMenuBar* menubar );

/// Adds the plugin related functions to the module table on top of the
/// stack (plugin_action, drop_plugin_actions, actions, reload_scripts).
void LuaApi_openPlugins( lua_State* L );
