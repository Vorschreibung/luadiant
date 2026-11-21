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

#include "headless.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "ientity.h"
#include "irender.h"
#include "map.h"
#include "textures.h"
#include "lua/luaengine.h"

bool g_headless = false;

namespace
{
std::string g_headless_game;
std::string g_headless_enginePath;
std::string g_headless_gamepacksPath;
bool g_headless_quiet = false;
std::string g_headless_script;
std::vector<std::string> g_headless_scriptArgs;

void Headless_printUsage( FILE* stream, const char* program ){
	std::fprintf( stream,
		"usage: %s [editor options] --headless [options] [--] script.lua [args...]\n"
		"\n"
		"Runs a Lua script without user interface, starting with an empty map.\n"
		"The script receives its arguments as ... and in the global arg table;\n"
		"a number returned by it becomes the exit code, false or an error give 1.\n"
		"\n"
		"options:\n"
		"  --game NAME         game to use, e.g. q3 (default: the configured game)\n"
		"  --engine-path PATH  game engine directory (default: the configured one)\n"
		"  --gamepacks PATH    directory with games/*.game and the game data\n"
		"                      (default: the installed gamepacks)\n"
		"  -q, --quiet         only print warnings and errors of the editor\n"
		"  -h, --help          show this help\n",
		program );
}
}

bool Headless_parseArguments( int& argc, char* argv[], int& exitCode ){
	int start = 1;
	while ( start < argc && std::strcmp( argv[start], "--headless" ) != 0 )
		++start;
	if ( start == argc ) {
		return true;
	}
	g_headless = true;

	// fetches the value of an option, reporting a missing one
	auto optionValue = [&]( int& i, std::string& value ){
		if ( i + 1 >= argc ) {
			std::fprintf( stderr, "%s: option %s requires a value\n", argv[0], argv[i] );
			return false;
		}
		value = argv[++i];
		return true;
	};

	int i = start + 1;
	for ( ; i < argc; ++i )
	{
		const char* arg = argv[i];
		if ( std::strcmp( arg, "--" ) == 0 ) {
			++i;
			break;
		}
		else if ( std::strcmp( arg, "-h" ) == 0 || std::strcmp( arg, "--help" ) == 0 ) {
			Headless_printUsage( stdout, argv[0] );
			exitCode = 0;
			return false;
		}
		else if ( std::strcmp( arg, "-q" ) == 0 || std::strcmp( arg, "--quiet" ) == 0 ) {
			g_headless_quiet = true;
		}
		else if ( std::strcmp( arg, "--game" ) == 0 ) {
			if ( !optionValue( i, g_headless_game ) ) {
				exitCode = 2;
				return false;
			}
		}
		else if ( std::strcmp( arg, "--engine-path" ) == 0 ) {
			if ( !optionValue( i, g_headless_enginePath ) ) {
				exitCode = 2;
				return false;
			}
		}
		else if ( std::strcmp( arg, "--gamepacks" ) == 0 ) {
			if ( !optionValue( i, g_headless_gamepacksPath ) ) {
				exitCode = 2;
				return false;
			}
		}
		else if ( arg[0] == '-' && arg[1] != '\0' ) {
			std::fprintf( stderr, "%s: unknown headless option %s\n", argv[0], arg );
			Headless_printUsage( stderr, argv[0] );
			exitCode = 2;
			return false;
		}
		else
		{
			break;
		}
	}

	if ( i >= argc ) {
		std::fprintf( stderr, "%s: no script given\n", argv[0] );
		Headless_printUsage( stderr, argv[0] );
		exitCode = 2;
		return false;
	}

	g_headless_script = argv[i];
	g_headless_scriptArgs.assign( argv + i + 1, argv + argc );

	// the rest of the editor only gets to see the options in front of --headless
	argc = start;
	argv[argc] = nullptr;
	return true;
}

const char* Headless_game(){
	return g_headless_game.c_str();
}

const char* Headless_enginePath(){
	return g_headless_enginePath.c_str();
}

const char* Headless_gamepacksPath(){
	return g_headless_gamepacksPath.c_str();
}

bool Headless_quiet(){
	return g_headless_quiet;
}

int Headless_run(){
	// the entity module notifies about key changes through a callback that
	// only the entity inspector window sets, which does not exist headless
	GlobalEntityCreator().setKeyValueChangedFunc( [](){} );

	// the editor realises shaders and textures once OpenGL is available;
	// brushes need them for their texture sizes, so it happens without
	// OpenGL here, only loading the images
	GlobalShaderCache().realise();
	Textures_Realise();

	if ( !LuaEngine_create( true ) ) {
		Textures_Unrealise();
		GlobalShaderCache().unrealise();
		return 1;
	}

	Map_New();

	int exitCode = 1;
	LuaEngine_runFile( g_headless_script.c_str(), g_headless_scriptArgs, &exitCode );

	// handles held by Lua reference scene nodes, release them first
	LuaEngine_destroy();
	Map_Free();
	Textures_Unrealise();
	GlobalShaderCache().unrealise();

	std::fflush( stdout );
	return exitCode;
}
