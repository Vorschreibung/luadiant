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

#include "luaundo.h"

#include <string>

#include "iundo.h"
#include "iscenegraph.h"

namespace
{
/// Name of the operation of changes outside of undo blocks.
std::string g_undoEntryName = "Lua";
/// Name of the outermost undo block, if one is running.
std::string g_undoBlockName;
/// Nesting depth of the running undo blocks.
int g_undoDepth = 0;
/// Whether an undo operation was started and not yet finished.
bool g_undoOpen = false;
/// The size of the undo stack right after starting the operation: it
/// recorded changes if it is larger after finishing.
std::size_t g_undoStartSize = 0;

/// Finishes the open operation; if \p commit is false, undoes its
/// changes and drops them from the redo stack, as if they never happened.
void LuaUndo_close( bool commit ){
	if ( !g_undoOpen ) {
		return;
	}
	g_undoOpen = false;

	UndoSystem& undo = GlobalUndoSystem();
	const char* name = g_undoBlockName.empty() ? g_undoEntryName.c_str() : g_undoBlockName.c_str();
	undo.finish( name );
	const bool changed = undo.size() > g_undoStartSize;
	if ( !commit && changed ) {
		undo.undo();
		// an empty operation clears the redo stack without adding anything
		undo.start();
		undo.finish( name );
	}
	SceneChangeNotify();
}

/// luadiant.undo_block( name, fn, ... ): calls fn( ... ) and returns its
/// results; its changes become one undo operation called name.
int l_undo_block( lua_State* L ){
	luaL_checkstring( L, 1 );
	luaL_checktype( L, 2, LUA_TFUNCTION );

	const bool outermost = g_undoDepth == 0;
	if ( outermost ) {
		// earlier changes outside of the block are an operation of their own
		LuaUndo_close( true );
		g_undoBlockName = lua_tostring( L, 1 );
	}

	// errors are rethrown unchanged, so callers can catch them as usual;
	// the error message still tells where it happened
	lua_remove( L, 1 );
	++g_undoDepth;
	const int status = lua_pcall( L, lua_gettop( L ) - 1, LUA_MULTRET, 0 );
	--g_undoDepth;

	if ( outermost ) {
		LuaUndo_close( status == 0 );
		g_undoBlockName.clear();
	}
	if ( status != 0 ) {
		return lua_error( L );
	}
	return lua_gettop( L );
}

/// luadiant.undo(): undoes the last operation, like Edit > Undo; returns
/// whether there was one.
int l_undo( lua_State* L ){
	LuaUndo_checkpoint( L, "undo" );
	UndoSystem& undo = GlobalUndoSystem();
	const std::size_t size = undo.size();
	undo.undo();
	SceneChangeNotify();
	lua_pushboolean( L, undo.size() != size );
	return 1;
}

/// luadiant.redo(): redoes the last undone operation; returns whether
/// there was one.
int l_redo( lua_State* L ){
	LuaUndo_checkpoint( L, "redo" );
	UndoSystem& undo = GlobalUndoSystem();
	const std::size_t size = undo.size();
	undo.redo();
	SceneChangeNotify();
	lua_pushboolean( L, undo.size() != size );
	return 1;
}

const luaL_Reg c_undoFunctions[] = {
	{ "undo_block", l_undo_block },
	{ "undo", l_undo },
	{ "redo", l_redo },
	{ nullptr, nullptr }
};
}

void LuaUndo_enter( const char* name ){
	LuaUndo_close( true );
	g_undoEntryName = name;
	g_undoBlockName.clear();
	g_undoDepth = 0;
}

void LuaUndo_leave( bool success ){
	LuaUndo_close( success );
	g_undoDepth = 0;
}

void LuaUndo_change(){
	if ( !g_undoOpen ) {
		GlobalUndoSystem().start();
		g_undoStartSize = GlobalUndoSystem().size();
		g_undoOpen = true;
	}
}

void LuaUndo_checkpoint( lua_State* L, const char* what ){
	if ( g_undoDepth > 0 ) {
		luaL_error( L, "%s is not possible inside an undo block", what );
	}
	LuaUndo_close( true );
}

void LuaApi_openUndo( lua_State* L ){
	luaL_register( L, nullptr, c_undoFunctions );
}
