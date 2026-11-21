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

// Headless mode: run a Lua script against the editor core without any
// user interface, e.g. for batch processing maps or in CI.
//
//   luadiant [editor options] --headless [options] [--] script.lua [args...]
//
// Editor options in front of --headless are handled as usual (e.g.
// -global-gamefile q3.game); everything from --headless on is consumed
// here and hidden from the rest of the editor.

#pragma once

/// True if luadiant was started with --headless.
extern bool g_headless;

/// Looks for --headless in the command line and, if present, parses the
/// headless options and removes them and the script arguments from
/// argc/argv. Returns false if the program should exit right away with
/// `exitCode` (usage errors, --help).
bool Headless_parseArguments( int& argc, char* argv[], int& exitCode );

/// The game requested with --game (a .game file name, the extension may be
/// omitted), or an empty string to use the configured game.
const char* Headless_game();

/// The engine path given with --engine-path, or an empty string.
const char* Headless_enginePath();

/// The gamepacks directory given with --gamepacks, or an empty string.
const char* Headless_gamepacksPath();

/// Whether informational editor output should be suppressed (--quiet).
bool Headless_quiet();

/// Runs the script on an empty map, after the editor core has been
/// initialised; returns the process exit code.
int Headless_run();
