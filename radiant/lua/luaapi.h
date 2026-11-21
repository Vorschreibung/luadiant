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

// Internal interface between the Lua engine and the C++ parts of the
// luadiant Lua API (the "luadiant.core" module).
//
// Each LuaApi_open* function adds its functions to the module table which
// is on top of the stack when called, and must leave the stack balanced.

#pragma once

#include "lua.hpp"
#include "math/vectorfwd.h"

namespace scene
{
class Node;
}

/// General functions: logging, paths, version and environment info.
void LuaApi_openCore( lua_State* L );

/// Node and face handles, object creation and selection; also creates
/// the metatable of vectors.
void LuaApi_openScene( lua_State* L );

/// The luadiant.map table: loading, saving and the map's entities.
void LuaApi_openMap( lua_State* L );

/// Pushes a vector {x =, y =, z =} with the luadiant.vec3 metatable.
void LuaApi_pushVector3( lua_State* L, double x, double y, double z );

template<typename T>
inline void LuaApi_pushVector( lua_State* L, const BasicVector3<T>& vector ){
	LuaApi_pushVector3( L, vector.x(), vector.y(), vector.z() );
}

/// Pushes the handle of a node, the same handle as long as one exists.
void LuaApi_pushNode( lua_State* L, scene::Node& node );
