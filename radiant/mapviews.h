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

#pragma once

// Per-map memory of the camera and 2D view positions, kept in
// "mapviews.txt" in the settings directory so reopening a map (e.g. via
// "Load last map at startup") continues where the user left it.

// Preference toggle: remember and restore the views of each map.
extern bool g_bRestoreMapViews;

// Records the current camera and 2D views for the map at `mapname`.
// No-op when headless or with the preference disabled.
void MapViews_store( const char* mapname );

// Moves the camera and 2D views to the position recorded for `mapname`.
// Returns false when nothing was restored, so the caller can fall back
// to the default start position.
bool MapViews_restore( const char* mapname );
