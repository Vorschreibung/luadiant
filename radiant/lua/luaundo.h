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

// Undo integration of Lua scripts.
//
// Changes made through the Lua API are recorded in the editor's undo
// history. An undo operation is opened lazily, right before the first
// change, so scripts which only read the map do not disturb the history
// (starting an operation clears the redo stack):
//
// - an entry point (running a script, an action) brackets its Lua code
//   with LuaUndo_enter() / LuaUndo_leave(); everything the code changes
//   outside of undo blocks becomes one operation named after the entry
//   point, which is rolled back if the code fails
// - luadiant.undo_block( name, fn, ... ) makes the changes of fn their own
//   operation; blocks nest, only the outermost one creates an operation,
//   and an error escaping the outermost block rolls its changes back

#pragma once

#include "lua.hpp"

/// Starts running Lua code from an entry point; changes outside of undo
/// blocks are recorded as an operation called \p name.
void LuaUndo_enter( const char* name );

/// Finishes running the code of an entry point: closes the operation of
/// its changes, or rolls them back if \p success is false.
void LuaUndo_leave( bool success );

/// Must be called by every Lua API function before it changes the map,
/// opens the undo operation if necessary.
void LuaUndo_change();

/// Closes the currently open undo operation, if any, so the next change
/// starts a new one; raises a Lua error inside an undo block, where
/// \p what is not allowed (map loading, saving, undo/redo).
void LuaUndo_checkpoint( lua_State* L, const char* what );

/// Adds undo_block, undo and redo to the module table on top of the stack.
void LuaApi_openUndo( lua_State* L );
