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

/// General functions: logging, paths, version and environment info.
void LuaApi_openCore( lua_State* L );
