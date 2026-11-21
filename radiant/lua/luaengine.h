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

// The embedded LuaJIT engine.
//
// There is exactly one lua_State at a time. It is created on startup (both
// in the editor and in headless mode) and can be thrown away and recreated
// to reload all scripts. All Lua code runs on the main (Qt) thread.
//
// radiant is compiled without C++ exceptions, so a Lua error unwinds
// through C++ frames without running destructors. Code calling into Lua or
// raising Lua errors must therefore not keep objects with non-trivial
// destructors alive across such calls; LuaEngine_pcall() is the safe way to
// call Lua from C++.

#pragma once

#include <string>
#include <vector>

struct lua_State;

/// Creates the Lua state, opens the standard libraries and the luadiant API
/// and sets up the module search paths. Any previous state is destroyed
/// first. Returns false if the state could not be created.
bool LuaEngine_create( bool headless );

/// Closes the Lua state, if any.
void LuaEngine_destroy();

/// Returns the current Lua state or nullptr if there is none.
lua_State* LuaEngine_state();

/// True if luadiant runs without user interface (--headless).
bool LuaEngine_isHeadless();

/// True while Lua code runs, i.e. inside LuaEngine_pcall(); entry points
/// triggered from the event loop (actions, reloading) must not start then.
bool LuaEngine_running();

/// Message handler for lua_pcall which appends a stack traceback to the
/// error message, unless the message already carries one (errors which were
/// re-raised after being caught by a nested handler).
int LuaEngine_messageHandler( lua_State* L );

/// Calls the function on the stack below its `nargs` arguments in protected
/// mode. On error the message (with traceback) is printed to the error
/// stream prefixed by `context`, nothing is left on the stack and false is
/// returned; on success `nresults` results are left on the stack.
bool LuaEngine_pcall( lua_State* L, int nargs, int nresults, const char* context );

/// Loads and runs a Lua script file. `args` are passed to the chunk as
/// `...` and are also put into the global `arg` table, with the script path
/// at index 0. If `exitCode` is given, it receives the exit code requested
/// by the script: a number returned by the chunk, 1 for `false` and on
/// errors, 0 otherwise. Returns false on load or runtime errors.
bool LuaEngine_runFile( const char* path, const std::vector<std::string>& args, int* exitCode = nullptr );

/// Compiles and runs a string of Lua code; `chunkname` is used in error
/// messages. Returns false on errors (which are printed).
bool LuaEngine_runString( const char* code, const char* chunkname );

/// Search directories for Lua modules and plugins, each with a trailing
/// slash: the one shipped with the application first, then the per-user
/// one in the settings directory.
std::vector<std::string> LuaEngine_scriptDirectories();
