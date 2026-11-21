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

#include "luaplugins.h"
#include "luaengine.h"
#include "luaundo.h"

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include <QMenu>
#include <QMenuBar>
#include <QTimer>

#include "itextstream.h"
#include "gtkutil/accelerator.h"
#include "gtkutil/filechooser.h"
#include "os/path.h"
#include "stream/stringstream.h"
#include "../commands.h"
#include "../gtkmisc.h"
#include "../mainframe.h"
#include "../preferences.h"

namespace
{
/// An action registered by a plugin, see luaplugins.h.
struct LuaAction
{
	std::string command;
	std::string plugin;
	std::string id;
	std::string title;
	/// slash separated path of the submenu below the Lua menu
	std::string menu;
	/// registry reference of the run function in the current Lua state,
	/// LUA_NOREF while the action is inert
	int function = LUA_NOREF;
	/// when it was registered, the menu lists actions in this order
	unsigned order = 0;
};

/// All actions ever registered, by command name; the commands refer to
/// the elements, which std::map never moves.
std::map<std::string, LuaAction> g_luaActions;
unsigned g_luaActionOrder = 0;

QMenu* g_luaMenu = nullptr;
/// the submenus of g_luaMenu, which are rebuilt with it
std::vector<QMenu*> g_luaSubmenus;
bool g_luaMenuUpdateQueued = false;
bool g_luaReloadQueued = false;

/// the last script run from the menu, to start the file dialog there
std::string g_luaLastScript;

/// Plugin names and action ids become parts of command names and thereby
/// keys in shortcuts.ini, so they are restricted to a safe set.
bool lua_validName( const char* name ){
	if ( name == nullptr || *name == '\0' ) {
		return false;
	}
	for ( const char* c = name; *c != '\0'; ++c )
	{
		const bool valid = ( *c >= 'a' && *c <= 'z' ) || ( *c >= 'A' && *c <= 'Z' ) || ( *c >= '0' && *c <= '9' ) || *c == '_' || *c == '-';
		if ( !valid ) {
			return false;
		}
	}
	return true;
}

/// Whether \p shortcut is empty or a key sequence Qt understands, e.g.
/// "Ctrl+Shift+A"; a function of its own so the QKeySequence is gone
/// before a Lua error may be raised.
bool lua_validShortcut( const char* shortcut ){
	return *shortcut == '\0' || QKeySequence_valid( QKeySequence( shortcut ) );
}

/// The callback of an action's command: runs its function as an entry
/// point, all its changes are one undo operation named after the action.
void LuaAction_run( LuaAction& action ){
	lua_State* L = LuaEngine_state();
	if ( L == nullptr || action.function == LUA_NOREF ) {
		globalWarningStream() << "Lua: " << action.command << " is not available, its plugin is not loaded\n";
		return;
	}
	if ( LuaEngine_running() ) {
		// e.g. a shortcut pressed while a script processes events
		globalWarningStream() << "Lua: " << action.command << " ignored, Lua code is already running\n";
		return;
	}

	lua_rawgeti( L, LUA_REGISTRYINDEX, action.function );
	LuaUndo_enter( action.title.c_str() );
	const bool success = LuaEngine_pcall( L, 0, 0, StringStream( "Lua: ", action.command.c_str() ) );
	LuaUndo_leave( success );
}
typedef ReferenceCaller<LuaAction, void(), LuaAction_run> LuaActionRunCaller;

/// The live actions in the order of their registration.
std::vector<const LuaAction*> lua_liveActions(){
	std::vector<const LuaAction*> actions;
	for ( const auto& [command, action] : g_luaActions )
		if ( action.function != LUA_NOREF ) {
			actions.push_back( &action );
		}
	std::sort( actions.begin(), actions.end(), []( const LuaAction* a, const LuaAction* b ){
		return a->order < b->order;
	} );
	return actions;
}

/// Returns the submenu of the Lua menu at the slash separated \p path,
/// creating it and its parents as necessary; \p submenus maps the paths
/// created so far to their menus.
QMenu* lua_submenu( std::map<std::string, QMenu*>& submenus, const std::string& path ){
	QMenu* menu = g_luaMenu;
	std::string current;
	std::size_t start = 0;
	while ( start <= path.size() )
	{
		std::size_t end = path.find( '/', start );
		if ( end == std::string::npos ) {
			end = path.size();
		}
		const std::string name = path.substr( start, end - start );
		start = end + 1;
		if ( name.empty() ) {
			continue;
		}

		current += '/';
		current += name;
		QMenu*& submenu = submenus[current];
		if ( submenu == nullptr ) {
			submenu = menu->addMenu( QString::fromStdString( name ) );
			submenu->setTearOffEnabled( g_Layout_enableDetachableMenus.m_value );
			g_luaSubmenus.push_back( submenu );
		}
		menu = submenu;
	}
	return menu;
}

/// Rebuilds the Lua menu from the registered actions; the shortcuts of
/// inert actions are disconnected, so they do not trigger anything.
void LuaPlugins_updateMenu(){
	g_luaMenuUpdateQueued = false;
	if ( g_luaMenu == nullptr ) {
		return;
	}

	// the actions of commands belong to the main window and survive this;
	// submenus are deleted in reverse order of creation, so nested ones go
	// before their parents, which would delete them along with themselves
	for ( auto submenu = g_luaSubmenus.rbegin(); submenu != g_luaSubmenus.rend(); ++submenu )
		delete *submenu;
	g_luaSubmenus.clear();
	g_luaMenu->clear();

	create_menu_item_with_mnemonic( g_luaMenu, "&Reload Scripts", "LuaReloadScripts" );
	create_menu_item_with_mnemonic( g_luaMenu, "Run &Script...", "LuaRunScript" );

	const std::vector<const LuaAction*> actions = lua_liveActions();
	if ( !actions.empty() ) {
		g_luaMenu->addSeparator();
	}
	std::map<std::string, QMenu*> submenus;
	for ( const LuaAction* action : actions )
		create_menu_item_with_mnemonic( lua_submenu( submenus, action->menu ), action->title.c_str(), action->command.c_str() );

	for ( const auto& [command, action] : g_luaActions )
		if ( action.function == LUA_NOREF ) {
			command_disconnect_accelerator( command.c_str() );
		}
}

/// Rebuilds the menu once control is back in the event loop, so a batch
/// of registrations rebuilds it only once.
void LuaPlugins_queueMenuUpdate(){
	if ( g_luaMenu != nullptr && !g_luaMenuUpdateQueued ) {
		g_luaMenuUpdateQueued = true;
		QTimer::singleShot( 0, LuaPlugins_updateMenu );
	}
}

/// Recreates the Lua state and loads all plugins again.
void LuaPlugins_reloadNow(){
	g_luaReloadQueued = false;
	if ( LuaEngine_running() ) {
		// closing the state under running Lua code would be fatal
		g_luaReloadQueued = true;
		QTimer::singleShot( 0, LuaPlugins_reloadNow );
		return;
	}

	globalOutputStream() << "Lua: reloading scripts\n";
	if ( LuaEngine_create( false ) ) {
		LuaPlugins_loadAll();
	}
	LuaPlugins_updateMenu();
}

/// Reloads the scripts once control is back in the event loop, as this
/// may be requested by Lua code which must finish first.
void LuaPlugins_queueReload(){
	if ( !g_luaReloadQueued ) {
		g_luaReloadQueued = true;
		QTimer::singleShot( 0, LuaPlugins_reloadNow );
	}
}

/// Lua > Run Script...: runs a script file chosen by the user.
void LuaPlugins_runScriptDialog(){
	if ( LuaEngine_running() ) {
		return;
	}
	const std::string start = g_luaLastScript.empty() ? std::string( SettingsPath_get() ) + "lua/" : g_luaLastScript;
	const char* path = file_dialog( MainFrame_getWindow(), true, "Run Lua Script", start.c_str(), "Lua scripts (*.lua)" );
	if ( path == nullptr ) {
		return;
	}
	g_luaLastScript = path;
	LuaEngine_runFile( g_luaLastScript.c_str(), {} );
}

/// Reads an optional string field of an action table; \p index is the
/// stack index of the value. Returns \p fallback for nil and nullptr for
/// any other type than string.
const char* lua_optionalString( lua_State* L, int index, const char* fallback ){
	if ( lua_isnil( L, index ) ) {
		return fallback;
	}
	return lua_type( L, index ) == LUA_TSTRING ? lua_tostring( L, index ) : nullptr;
}

/// plugin:action{ id =, title =, menu =, shortcut =, run = }: registers
/// an action of the plugin and returns its command name, see
/// luaplugins.h. title defaults to the id, menu to the plugin's name.
int l_plugin_action( lua_State* L ){
	luaL_checktype( L, 1, LUA_TTABLE );
	luaL_checktype( L, 2, LUA_TTABLE );
	lua_settop( L, 2 );

	lua_getfield( L, 1, "name" ); // 3
	const char* plugin = lua_type( L, 3 ) == LUA_TSTRING ? lua_tostring( L, 3 ) : nullptr;
	if ( !lua_validName( plugin ) ) {
		return luaL_error( L, "action: must be called on a plugin, as plugin:action{ ... }" );
	}

	lua_getfield( L, 2, "id" );       // 4
	lua_getfield( L, 2, "title" );    // 5
	lua_getfield( L, 2, "menu" );     // 6
	lua_getfield( L, 2, "shortcut" ); // 7
	lua_getfield( L, 2, "run" );      // 8

	const char* id = lua_type( L, 4 ) == LUA_TSTRING ? lua_tostring( L, 4 ) : nullptr;
	if ( !lua_validName( id ) ) {
		return luaL_error( L, "action: id must be a non-empty string of letters, digits, '_' and '-'" );
	}
	const char* title = lua_optionalString( L, 5, id );
	if ( title == nullptr || *title == '\0' ) {
		return luaL_error( L, "action %s: title must be a non-empty string", id );
	}
	const char* menu = lua_optionalString( L, 6, plugin );
	if ( menu == nullptr ) {
		return luaL_error( L, "action %s: menu must be a string", id );
	}
	const char* shortcut = lua_optionalString( L, 7, "" );
	if ( shortcut == nullptr || !lua_validShortcut( shortcut ) ) {
		return luaL_error( L, "action %s: shortcut must be a key sequence like \"Ctrl+Shift+A\"", id );
	}
	if ( !lua_isfunction( L, 8 ) ) {
		return luaL_error( L, "action %s: run must be a function", id );
	}

	lua_pushfstring( L, "Lua.%s.%s", plugin, id ); // 9
	const char* command = lua_tostring( L, 9 );
	const auto found = g_luaActions.find( command );
	if ( found != g_luaActions.end() && found->second.function != LUA_NOREF ) {
		return luaL_error( L, "action %s is already registered", command );
	}

	// no Lua errors from here on, the strings below would leak
	lua_pushvalue( L, 8 );
	const int function = luaL_ref( L, LUA_REGISTRYINDEX );

	LuaAction& action = g_luaActions[command];
	const bool isNew = action.command.empty();
	action.command = command;
	action.plugin = plugin;
	action.id = id;
	action.title = title;
	action.menu = menu;
	action.function = function;
	action.order = ++g_luaActionOrder;
	if ( isNew ) {
		// the default shortcut of a command is fixed once it is inserted,
		// changes to it take effect after restarting the editor
		GlobalCommands_insert( action.command.c_str(), LuaActionRunCaller( action ), QKeySequence( shortcut ) );
	}

	LuaPlugins_queueMenuUpdate();
	lua_settop( L, 9 );
	return 1;
}

/// drop_plugin_actions( name ): makes the actions of a plugin inert,
/// used when the plugin fails to load.
int l_drop_plugin_actions( lua_State* L ){
	const char* plugin = luaL_checkstring( L, 1 );
	for ( auto& [command, action] : g_luaActions )
		if ( action.function != LUA_NOREF && action.plugin == plugin ) {
			luaL_unref( L, LUA_REGISTRYINDEX, action.function );
			action.function = LUA_NOREF;
		}
	LuaPlugins_queueMenuUpdate();
	return 0;
}

/// luadiant.actions(): the registered actions in the order of their
/// registration, as tables with the fields command, plugin, id, title,
/// menu, shortcut (as currently bound, "" for none) and run.
int l_actions( lua_State* L ){
	const std::vector<const LuaAction*> actions = lua_liveActions();
	lua_createtable( L, static_cast<int>( actions.size() ), 0 );
	for ( std::size_t i = 0; i < actions.size(); ++i )
	{
		const LuaAction& action = *actions[i];
		lua_createtable( L, 0, 7 );
		lua_pushstring( L, action.command.c_str() );
		lua_setfield( L, -2, "command" );
		lua_pushstring( L, action.plugin.c_str() );
		lua_setfield( L, -2, "plugin" );
		lua_pushstring( L, action.id.c_str() );
		lua_setfield( L, -2, "id" );
		lua_pushstring( L, action.title.c_str() );
		lua_setfield( L, -2, "title" );
		lua_pushstring( L, action.menu.c_str() );
		lua_setfield( L, -2, "menu" );
		lua_pushstring( L, GlobalCommands_find( action.command.c_str() ).m_accelerator.toString().toUtf8().constData() );
		lua_setfield( L, -2, "shortcut" );
		lua_rawgeti( L, LUA_REGISTRYINDEX, action.function );
		lua_setfield( L, -2, "run" );
		lua_rawseti( L, -2, static_cast<int>( i + 1 ) );
	}
	return 1;
}

/// luadiant.reload_scripts(): recreates the Lua state and reloads all
/// plugins as soon as the running Lua code has finished; in the editor
/// only.
int l_reload_scripts( lua_State* L ){
	if ( LuaEngine_isHeadless() ) {
		return luaL_error( L, "reload_scripts is not available in headless mode" );
	}
	LuaPlugins_queueReload();
	return 0;
}

const luaL_Reg c_pluginFunctions[] = {
	{ "plugin_action", l_plugin_action },
	{ "drop_plugin_actions", l_drop_plugin_actions },
	{ "actions", l_actions },
	{ "reload_scripts", l_reload_scripts },
	{ nullptr, nullptr }
};
}

void LuaPlugins_loadAll(){
	lua_State* L = LuaEngine_state();
	if ( L == nullptr ) {
		return;
	}
	const int top = lua_gettop( L );

	lua_getglobal( L, "require" );
	lua_pushliteral( L, "luadiant.plugin" );
	if ( !LuaEngine_pcall( L, 1, 1, "Lua: loading plugins" ) ) {
		return;
	}
	if ( !lua_istable( L, -1 ) ) {
		globalErrorStream() << "Lua: loading plugins: the luadiant.plugin module is not a table\n";
		lua_settop( L, top );
		return;
	}
	lua_getfield( L, -1, "load_all" );

	// plugins may change the map while loading, treat that like a script
	LuaUndo_enter( "Lua: loading plugins" );
	const bool success = LuaEngine_pcall( L, 0, 0, "Lua: loading plugins" );
	LuaUndo_leave( success );
	lua_settop( L, top );
}

void LuaPlugins_stateClosed(){
	// the references died with the state
	for ( auto& [command, action] : g_luaActions )
		action.function = LUA_NOREF;
	LuaPlugins_queueMenuUpdate();
}

void LuaPlugins_registerCommands(){
	GlobalCommands_insert( "LuaReloadScripts", makeCallbackF( LuaPlugins_queueReload ) );
	GlobalCommands_insert( "LuaRunScript", makeCallbackF( LuaPlugins_runScriptDialog ) );
}

void LuaPlugins_createMenu( QMenuBar* menubar ){
	g_luaMenu = menubar->addMenu( "&Lua" );
	g_luaMenu->setTearOffEnabled( g_Layout_enableDetachableMenus.m_value );
	// the menu goes away with the main window
	QObject::connect( g_luaMenu, &QObject::destroyed, [](){
		g_luaMenu = nullptr;
		g_luaSubmenus.clear();
	} );
	LuaPlugins_updateMenu();
}

void LuaApi_openPlugins( lua_State* L ){
	luaL_register( L, nullptr, c_pluginFunctions );
}
