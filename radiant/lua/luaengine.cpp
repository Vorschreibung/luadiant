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

#include "luaengine.h"
#include "luaapi.h"
#include "luaundo.h"

#include <cstring>

#include "itextstream.h"
#include "os/path.h"
#include "stream/stringstream.h"
#include "../mainframe.h"

namespace
{
lua_State* g_luaState = nullptr;
bool g_luaHeadless = false;

#ifdef WIN32
const char* const c_nativeModuleSuffix = "?.dll";
#else
const char* const c_nativeModuleSuffix = "?.so";
#endif

/// Called by LuaJIT on errors outside of any protected call, right before
/// it terminates the process; makes sure the reason ends up in the log.
int lua_panicHandler( lua_State* L ){
	const char* message = lua_tostring( L, -1 );
	globalErrorStream() << "Lua panic: " << ( message != nullptr ? message : "(no message)" ) << '\n';
	return 0;
}

/// Prepends our script directories to package.path or package.cpath, so
/// modules shipped with luadiant and user modules are found before
/// anything in the default (environment dependent) locations.
void lua_prependSearchPath( lua_State* L, const char* field, const std::vector<const char*>& patterns ){
	std::string path;
	for ( const std::string& directory : LuaEngine_scriptDirectories() ){
		for ( const char* pattern : patterns ){
			path += directory;
			path += pattern;
			path += ';';
		}
	}

	lua_getglobal( L, "package" );
	lua_getfield( L, -1, field );
	const char* previous = lua_tostring( L, -1 );
	if ( previous != nullptr ) {
		path += previous;
	}
	lua_pop( L, 1 );
	lua_pushlstring( L, path.data(), path.size() );
	lua_setfield( L, -2, field );
	lua_pop( L, 1 );
}

/// Protected part of the state setup: registers "luadiant.core" in
/// package.loaded, then requires the "luadiant" Lua module (lua/luadiant/)
/// which builds the public API on top of it and is made available as the
/// global `luadiant`. Without the Lua part (e.g. not installed) the bare
/// core module is used, so scripts still get the basics.
int lua_setupModules( lua_State* L ){
	lua_getglobal( L, "package" );
	lua_getfield( L, -1, "loaded" );
	lua_newtable( L );
	LuaApi_openCore( L );
	LuaApi_openScene( L );
	LuaApi_openMap( L );
	LuaApi_openUndo( L );
	lua_pushvalue( L, -1 );
	lua_setfield( L, -3, "luadiant.core" );
	// stack: package, loaded, core

	lua_getglobal( L, "require" );
	lua_pushliteral( L, "luadiant" );
	if ( !LuaEngine_pcall( L, 1, 1, "Lua: loading the luadiant module" ) ) {
		globalWarningStream() << "Lua: falling back to the bare luadiant.core module\n";
		lua_pushvalue( L, -1 );
	}
	lua_setglobal( L, "luadiant" );
	lua_pop( L, 3 );
	return 0;
}

/// Turns the value returned by a script chunk into a process exit code.
int lua_exitCodeFromResult( lua_State* L, int index ){
	switch ( lua_type( L, index ) )
	{
	case LUA_TNUMBER:
		return static_cast<int>( lua_tointeger( L, index ) );
	case LUA_TBOOLEAN:
		return lua_toboolean( L, index ) ? 0 : 1;
	default:
		return 0;
	}
}
}

std::vector<std::string> LuaEngine_scriptDirectories(){
	return {
		std::string( AppPath_get() ) + "lua/",
		std::string( SettingsPath_get() ) + "lua/",
	};
}

bool LuaEngine_create( bool headless ){
	LuaEngine_destroy();

	g_luaHeadless = headless;

	lua_State* L = luaL_newstate();
	if ( L == nullptr ) {
		globalErrorStream() << "Lua: failed to create the Lua state\n";
		return false;
	}
	lua_atpanic( L, lua_panicHandler );
	luaL_openlibs( L );

	lua_prependSearchPath( L, "path", { "?.lua", "?/init.lua" } );
	lua_prependSearchPath( L, "cpath", { c_nativeModuleSuffix } );

	g_luaState = L;

	lua_pushcfunction( L, lua_setupModules );
	if ( !LuaEngine_pcall( L, 0, 0, "Lua: initialising the luadiant API" ) ) {
		LuaEngine_destroy();
		return false;
	}

	globalOutputStream() << "Lua: " << LUAJIT_VERSION << " initialised\n";
	return true;
}

void LuaEngine_destroy(){
	if ( g_luaState != nullptr ) {
		lua_close( g_luaState );
		g_luaState = nullptr;
	}
}

lua_State* LuaEngine_state(){
	return g_luaState;
}

bool LuaEngine_isHeadless(){
	return g_luaHeadless;
}

int LuaEngine_messageHandler( lua_State* L ){
	const char* message = lua_tostring( L, 1 );
	if ( message == nullptr ) {
		if ( luaL_callmeta( L, 1, "__tostring" ) && lua_type( L, -1 ) == LUA_TSTRING ) {
			message = lua_tostring( L, -1 );
		}
		else
		{
			message = lua_pushfstring( L, "(error object is a %s value)", luaL_typename( L, 1 ) );
		}
	}

	if ( std::strstr( message, "\nstack traceback:" ) != nullptr ) {
		lua_pushstring( L, message );
	}
	else
	{
		luaL_traceback( L, L, message, 1 );
	}
	return 1;
}

bool LuaEngine_pcall( lua_State* L, int nargs, int nresults, const char* context ){
	// the message handler goes below the function and its arguments
	const int handlerIndex = lua_gettop( L ) - nargs;
	lua_pushcfunction( L, LuaEngine_messageHandler );
	lua_insert( L, handlerIndex );

	const int status = lua_pcall( L, nargs, nresults, handlerIndex );
	lua_remove( L, handlerIndex );

	if ( status != 0 ) {
		const char* message = lua_tostring( L, -1 );
		globalErrorStream() << context << ": " << ( message != nullptr ? message : "(error object is not a string)" ) << '\n';
		lua_pop( L, 1 );
		return false;
	}
	return true;
}

bool LuaEngine_runFile( const char* path, const std::vector<std::string>& args, int* exitCode ){
	if ( exitCode != nullptr ) {
		*exitCode = 1;
	}

	lua_State* L = g_luaState;
	if ( L == nullptr ) {
		globalErrorStream() << "Lua: cannot run " << path << ": the Lua engine is not initialised\n";
		return false;
	}
	const int top = lua_gettop( L );

	if ( luaL_loadfile( L, path ) != 0 ) {
		globalErrorStream() << "Lua: " << lua_tostring( L, -1 ) << '\n';
		lua_settop( L, top );
		return false;
	}

	// the global `arg` table follows the convention of the lua interpreter
	lua_createtable( L, static_cast<int>( args.size() ), 1 );
	lua_pushstring( L, path );
	lua_rawseti( L, -2, 0 );
	for ( std::size_t i = 0; i < args.size(); ++i ){
		lua_pushlstring( L, args[i].data(), args[i].size() );
		lua_rawseti( L, -2, static_cast<int>( i + 1 ) );
	}
	lua_setglobal( L, "arg" );

	if ( !lua_checkstack( L, static_cast<int>( args.size() ) + 1 ) ) {
		globalErrorStream() << "Lua: too many arguments for " << path << '\n';
		lua_settop( L, top );
		return false;
	}
	for ( const std::string& arg : args ){
		lua_pushlstring( L, arg.data(), arg.size() );
	}

	// the script's changes are one undo operation, rolled back on errors
	LuaUndo_enter( StringStream( "Lua: ", path_get_filename_start( path ) ) );
	const bool success = LuaEngine_pcall( L, static_cast<int>( args.size() ), 1, "Lua" );
	LuaUndo_leave( success );
	if ( !success ) {
		lua_settop( L, top );
		return false;
	}

	if ( exitCode != nullptr ) {
		*exitCode = lua_exitCodeFromResult( L, -1 );
	}
	lua_settop( L, top );
	return true;
}

bool LuaEngine_runString( const char* code, const char* chunkname ){
	lua_State* L = g_luaState;
	if ( L == nullptr ) {
		globalErrorStream() << "Lua: cannot run " << chunkname << ": the Lua engine is not initialised\n";
		return false;
	}
	const int top = lua_gettop( L );

	if ( luaL_loadbuffer( L, code, std::strlen( code ), chunkname ) != 0 ) {
		globalErrorStream() << "Lua: " << lua_tostring( L, -1 ) << '\n';
		lua_settop( L, top );
		return false;
	}
	LuaUndo_enter( StringStream( "Lua: ", chunkname ) );
	const bool success = LuaEngine_pcall( L, 0, 0, "Lua" );
	LuaUndo_leave( success );
	lua_settop( L, top );
	return success;
}
