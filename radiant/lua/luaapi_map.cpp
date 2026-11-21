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

// luadiant.map: the map as a whole, i.e. loading and saving it and access
// to its entities.

#include "luaapi.h"
#include "luaundo.h"

#include <vector>

#include <QDir>
#include <QFileInfo>

#include "iscenegraph.h"
#include "imap.h"
#include "mapfile.h"
#include "scenelib.h"
#include "os/file.h"
#include "os/path.h"
#include "string/string.h"

#include "../map.h"
#include "../filetypes.h"

namespace
{
/// Pushes the absolute, cleaned up version of the path argument at
/// \p index and returns it.
const char* lua_pushAbsolutePath( lua_State* L, int index ){
	const char* path = luaL_checkstring( L, index );
	const QByteArray absolute = QDir::cleanPath( QFileInfo( QString::fromUtf8( path ) ).absoluteFilePath() ).toUtf8();
	lua_pushlstring( L, absolute.constData(), absolute.size() );
	return lua_tostring( L, -1 );
}

/// Raises an error if no map format handles files like \p path.
void lua_checkMapFormat( lua_State* L, const char* path ){
	const char* extension = path_get_extension( path );
	if ( string_empty( findModuleName( GetFileTypeRegistry(), MapFormat::Name, extension ) ) ) {
		luaL_error( L, "unsupported map file type '%s': %s", extension, path );
	}
}

/// luadiant.map.new(): replaces the map with an empty, unnamed one;
/// unsaved changes are lost.
int l_map_new( lua_State* L ){
	LuaUndo_checkpoint( L, "map.new" );
	Map_Free();
	Map_New();
	return 0;
}

/// luadiant.map.load( path ): replaces the map with the given map file;
/// unsaved changes are lost. Clears the undo history.
int l_map_load( lua_State* L ){
	const char* path = lua_pushAbsolutePath( L, 1 );
	if ( !file_exists( path ) ) {
		luaL_error( L, "map file not found: %s", path );
	}
	lua_checkMapFormat( L, path );
	LuaUndo_checkpoint( L, "map.load" );
	Map_Free();
	Map_LoadFile( path );
	return 0;
}

/// luadiant.map.save( [path] ): writes the map to its file, or to path;
/// the map keeps its name. Writing it to its own file marks it as saved.
/// Unlike saving in the editor this does not create a .bak backup.
int l_map_save( lua_State* L ){
	const char* path;
	if ( lua_isnoneornil( L, 1 ) ) {
		if ( Map_Unnamed( g_map ) ) {
			luaL_error( L, "map.save: the map has no file name yet, pass a path" );
		}
		path = Map_Name( g_map );
	}
	else
	{
		path = lua_pushAbsolutePath( L, 1 );
	}
	lua_checkMapFormat( L, path );
	// later changes must start a new operation to update the modified state
	LuaUndo_checkpoint( L, "map.save" );

	if ( !Map_SaveFile( path ) ) {
		luaL_error( L, "map.save: failed to write %s", path );
	}
	if ( path_equal( path, Map_Name( g_map ) ) ) {
		if ( MapFile* mapFile = Node_getMapFile( GlobalSceneGraph().root() ) ) {
			mapFile->save();
		}
	}
	return 0;
}

/// luadiant.map.name(): the absolute path of the map file, "unnamed.map"
/// for a new map.
int l_map_name( lua_State* L ){
	lua_pushstring( L, Map_Name( g_map ) );
	return 1;
}

/// luadiant.map.unnamed(): whether the map was never saved.
int l_map_unnamed( lua_State* L ){
	lua_pushboolean( L, Map_Unnamed( g_map ) );
	return 1;
}

/// luadiant.map.modified(): whether the map has unsaved changes.
int l_map_modified( lua_State* L ){
	lua_pushboolean( L, Map_Modified( g_map ) );
	return 1;
}

/// luadiant.map.worldspawn(): the worldspawn entity, created if missing.
int l_map_worldspawn( lua_State* L ){
	scene::Node* worldspawn = Map_FindWorldspawn( g_map );
	if ( worldspawn == nullptr ) {
		LuaUndo_change();
		worldspawn = &Map_FindOrInsertWorldspawn( g_map );
	}
	LuaApi_pushNode( L, *worldspawn );
	return 1;
}

/// luadiant.map.entities(): all entities, in map file order.
int l_map_entities( lua_State* L ){
	std::vector<scene::Node*> entities;
	class Collect : public scene::Traversable::Walker
	{
		std::vector<scene::Node*>& m_nodes;
	public:
		Collect( std::vector<scene::Node*>& nodes ) : m_nodes( nodes ){
		}
		bool pre( scene::Node& node ) const override {
			m_nodes.push_back( &node );
			return false;
		}
	};
	Node_getTraversable( GlobalSceneGraph().root() )->traverse( Collect( entities ) );

	lua_createtable( L, static_cast<int>( entities.size() ), 0 );
	for ( std::size_t i = 0; i < entities.size(); ++i ){
		LuaApi_pushNode( L, *entities[i] );
		lua_rawseti( L, -2, static_cast<int>( i + 1 ) );
	}
	return 1;
}

const luaL_Reg c_mapFunctions[] = {
	{ "new", l_map_new },
	{ "load", l_map_load },
	{ "save", l_map_save },
	{ "name", l_map_name },
	{ "unnamed", l_map_unnamed },
	{ "modified", l_map_modified },
	{ "worldspawn", l_map_worldspawn },
	{ "entities", l_map_entities },
	{ nullptr, nullptr }
};
}

void LuaApi_openMap( lua_State* L ){
	lua_newtable( L );
	luaL_register( L, nullptr, c_mapFunctions );
	lua_setfield( L, -2, "map" );
}
