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

// luadiant.core: logging, paths, version and environment information.

#include "luaapi.h"
#include "luaengine.h"

#include "itextstream.h"
#include "../mainframe.h"
#include "../preferences.h"

namespace
{
/// Converts all arguments with tostring() and joins them with tabs, like
/// Lua's print does; the result is left on top of the stack.
void lua_joinArguments( lua_State* L ){
	const int count = lua_gettop( L );
	luaL_checkstack( L, count * 2 + 2, "too many arguments" );

	lua_getglobal( L, "tostring" );
	const int tostringIndex = lua_gettop( L );
	for ( int i = 1; i <= count; ++i ){
		if ( i > 1 ) {
			lua_pushliteral( L, "\t" );
		}
		lua_pushvalue( L, tostringIndex );
		lua_pushvalue( L, i );
		lua_call( L, 1, 1 );
		if ( !lua_isstring( L, -1 ) ) {
			luaL_error( L, "'tostring' must return a string" );
		}
	}
	if ( count == 0 ) {
		lua_pushliteral( L, "" );
	}
	else
	{
		lua_concat( L, count * 2 - 1 );
	}
	lua_remove( L, tostringIndex );
}

/// Writes the joined arguments as one line to the given stream.
int lua_writeLine( lua_State* L, TextOutputStream& stream ){
	lua_joinArguments( L );
	std::size_t length;
	const char* text = lua_tolstring( L, -1, &length );
	stream.write( text, length );
	stream.write( "\n", 1 );
	return 0;
}

/// luadiant.log(...) and the global print(...): prints to the console.
int l_log( lua_State* L ){
	return lua_writeLine( L, globalOutputStream() );
}

/// luadiant.warn(...): prints a warning to the console.
int l_warn( lua_State* L ){
	return lua_writeLine( L, globalWarningStream() );
}

/// luadiant.log_error(...): prints an error message to the console; this
/// does not raise a Lua error.
int l_log_error( lua_State* L ){
	return lua_writeLine( L, globalErrorStream() );
}

/// luadiant.app_path(): the installation directory, with trailing slash.
int l_app_path( lua_State* L ){
	lua_pushstring( L, AppPath_get() );
	return 1;
}

/// luadiant.settings_path(): the per-user settings directory.
int l_settings_path( lua_State* L ){
	lua_pushstring( L, SettingsPath_get() );
	return 1;
}

/// luadiant.engine_path(): the configured game engine directory.
int l_engine_path( lua_State* L ){
	lua_pushstring( L, EnginePath_get() );
	return 1;
}

/// luadiant.game(): a table describing the active game configuration.
int l_game( lua_State* L ){
	lua_createtable( L, 0, 5 );
	if ( g_pGameDescription != nullptr ) {
		lua_pushstring( L, g_pGameDescription->mGameFile.c_str() );
		lua_setfield( L, -2, "file" );
		lua_pushstring( L, g_pGameDescription->getKeyValue( "name" ) );
		lua_setfield( L, -2, "name" );
		lua_pushstring( L, g_pGameDescription->mGameType.c_str() );
		lua_setfield( L, -2, "type" );
		lua_pushstring( L, basegame_get() );
		lua_setfield( L, -2, "basegame" );
		lua_pushstring( L, gamename_get() );
		lua_setfield( L, -2, "gamename" );
	}
	return 1;
}

/// luadiant.script_directories(): the directories searched for Lua modules
/// and plugins, the shipped one first.
int l_script_directories( lua_State* L ){
	const std::vector<std::string> directories = LuaEngine_scriptDirectories();
	lua_createtable( L, static_cast<int>( directories.size() ), 0 );
	for ( std::size_t i = 0; i < directories.size(); ++i ){
		lua_pushlstring( L, directories[i].data(), directories[i].size() );
		lua_rawseti( L, -2, static_cast<int>( i + 1 ) );
	}
	return 1;
}

const luaL_Reg c_coreFunctions[] = {
	{ "log", l_log },
	{ "warn", l_warn },
	{ "log_error", l_log_error },
	{ "app_path", l_app_path },
	{ "settings_path", l_settings_path },
	{ "engine_path", l_engine_path },
	{ "game", l_game },
	{ "script_directories", l_script_directories },
	{ nullptr, nullptr }
};
}

void LuaApi_openCore( lua_State* L ){
	luaL_register( L, nullptr, c_coreFunctions );

	lua_pushliteral( L, RADIANT_VERSION );
	lua_setfield( L, -2, "version" );
	lua_pushliteral( L, LUAJIT_VERSION );
	lua_setfield( L, -2, "luajit_version" );
	lua_pushboolean( L, LuaEngine_isHeadless() );
	lua_setfield( L, -2, "headless" );

	// route the global print through the console as well
	lua_pushcfunction( L, l_log );
	lua_setglobal( L, "print" );
}
