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

// Scene access for Lua: vectors, node handles (entities, brushes, patches),
// face handles, object creation and the selection.
//
// A node handle is a userdata holding a reference to a scene::Node, so
// the node stays alive as long as Lua can reach it, even after it has
// been deleted from the map (e.g. it may come back through undo). Each
// node gets at most one handle at a time, so handles compare with ==.
// Whether a node is part of the map is decided by it having a scene
// instance; changes are only allowed then, so they are recorded by undo.
//
// A face handle holds a reference to its brush node and the Face pointer,
// which is only used after checking it still belongs to the brush.
//
// Lua errors must not skip C++ destructors (no -fexceptions), so the
// functions below check their arguments first and collect scene data in
// plain containers before they push anything to Lua.

#include "luaapi.h"
#include "luaundo.h"

#include <cstring>
#include <vector>

#include "ientity.h"
#include "ieclass.h"
#include "ibrush.h"
#include "iselection.h"
#include "iscenegraph.h"
#include "scenelib.h"
#include "shaderlib.h"
#include "math/aabb.h"

#include "../brush.h"
#include "../brushnode.h"
#include "../brushmanip.h"
#include "../brush_primit.h"
#include "../patch.h"
#include "../map.h"

namespace
{
const char* const c_vec3Metatable = "luadiant.vec3";
const char* const c_nodeMetatable = "luadiant.node";
const char* const c_faceMetatable = "luadiant.face";
/// Registry field of the table mapping nodes (light userdata) to their
/// handles; it has weak values, so it does not keep the handles alive.
const char* const c_nodeHandles = "luadiant.node_handles";

struct LuaNode
{
	scene::Node* node;
};

struct LuaFace
{
	scene::Node* brushNode;
	Face* face;
};

// ---------------------------------------------------------------------------
// vectors

/// Reads a vector given as {x=, y=, z=} or {x, y, z}.
DoubleVector3 lua_checkVector( lua_State* L, int index ){
	if ( index < 0 ) {
		index = lua_gettop( L ) + index + 1;
	}
	luaL_checktype( L, index, LUA_TTABLE );
	DoubleVector3 vector;
	const char* const names[] = { "x", "y", "z" };
	for ( int i = 0; i < 3; ++i ){
		lua_getfield( L, index, names[i] );
		if ( lua_isnil( L, -1 ) ) {
			lua_pop( L, 1 );
			lua_rawgeti( L, index, i + 1 );
		}
		if ( !lua_isnumber( L, -1 ) ) {
			luaL_argerror( L, index, "expected a vector {x, y, z}" );
		}
		vector[i] = lua_tonumber( L, -1 );
		lua_pop( L, 1 );
	}
	return vector;
}

// ---------------------------------------------------------------------------
// node handles

/// The first scene instance of the node, or nullptr if it is not part of
/// the map; nodes in the map have exactly one instance.
scene::Instance* Node_firstInstance( scene::Node& node ){
	scene::Instantiable* instantiable = Node_getInstantiable( node );
	if ( instantiable == nullptr ) {
		return nullptr;
	}
	class FirstInstance : public scene::Instantiable::Visitor
	{
	public:
		mutable scene::Instance* m_instance = nullptr;
		void visit( scene::Instance& instance ) const override {
			if ( m_instance == nullptr ) {
				m_instance = &instance;
			}
		}
	} visitor;
	instantiable->forEachInstance( visitor );
	return visitor.m_instance;
}

const char* Node_kind( scene::Node& node ){
	if ( Node_getEntity( node ) != nullptr ) {
		return "entity";
	}
	if ( Node_isBrush( node ) ) {
		return "brush";
	}
	if ( Node_isPatch( node ) ) {
		return "patch";
	}
	return "node";
}

int l_node_gc( lua_State* L ){
	LuaNode* handle = static_cast<LuaNode*>( luaL_checkudata( L, 1, c_nodeMetatable ) );
	if ( handle->node != nullptr ) {
		// forget the handle, unless the node got a new one in the meantime
		lua_getfield( L, LUA_REGISTRYINDEX, c_nodeHandles );
		lua_pushlightuserdata( L, handle->node );
		lua_rawget( L, -2 );
		if ( lua_rawequal( L, -1, 1 ) ) {
			lua_pushlightuserdata( L, handle->node );
			lua_pushnil( L );
			lua_rawset( L, -4 );
		}
		lua_pop( L, 2 );

		scene::Node* node = handle->node;
		handle->node = nullptr;
		node->DecRef();
	}
	return 0;
}

LuaNode& lua_checkNodeHandle( lua_State* L, int index ){
	LuaNode* handle = static_cast<LuaNode*>( luaL_checkudata( L, index, c_nodeMetatable ) );
	if ( handle->node == nullptr ) {
		luaL_argerror( L, index, "node handle has been released" );
	}
	return *handle;
}

scene::Node& lua_checkNode( lua_State* L, int index ){
	return *lua_checkNodeHandle( L, index ).node;
}

/// The instance of a node argument which must be part of the map.
scene::Instance& lua_checkInstance( lua_State* L, int index ){
	scene::Instance* instance = Node_firstInstance( lua_checkNode( L, index ) );
	if ( instance == nullptr ) {
		luaL_argerror( L, index, "the node is not part of the map (deleted?)" );
	}
	return *instance;
}

Entity& lua_checkEntity( lua_State* L, int index ){
	Entity* entity = Node_getEntity( lua_checkNode( L, index ) );
	if ( entity == nullptr ) {
		luaL_argerror( L, index, "expected an entity" );
	}
	return *entity;
}

Brush& lua_checkBrush( lua_State* L, int index ){
	Brush* brush = Node_getBrush( lua_checkNode( L, index ) );
	if ( brush == nullptr ) {
		luaL_argerror( L, index, "expected a brush" );
	}
	return *brush;
}

Patch& lua_checkPatch( lua_State* L, int index ){
	Patch* patch = Node_getPatch( lua_checkNode( L, index ) );
	if ( patch == nullptr ) {
		luaL_argerror( L, index, "expected a patch" );
	}
	return *patch;
}

/// Checks a node which is about to be changed: it must be part of the
/// map, and an undo operation is opened for the change.
scene::Instance& lua_beginNodeChange( lua_State* L, int index ){
	scene::Instance& instance = lua_checkInstance( L, index );
	LuaUndo_change();
	return instance;
}

void lua_pushNodeList( lua_State* L, const std::vector<scene::Node*>& nodes ){
	lua_createtable( L, static_cast<int>( nodes.size() ), 0 );
	for ( std::size_t i = 0; i < nodes.size(); ++i ){
		LuaApi_pushNode( L, *nodes[i] );
		lua_rawseti( L, -2, static_cast<int>( i + 1 ) );
	}
}

/// The direct children of a node, e.g. the entities of the map root or
/// the primitives of an entity.
std::vector<scene::Node*> Node_children( scene::Node& node ){
	std::vector<scene::Node*> children;
	if ( scene::Traversable* traversable = Node_getTraversable( node ) ) {
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
		traversable->traverse( Collect( children ) );
	}
	return children;
}

/// node:kind(): "entity", "brush" or "patch".
int l_node_kind( lua_State* L ){
	lua_pushstring( L, Node_kind( lua_checkNode( L, 1 ) ) );
	return 1;
}

/// node:valid(): whether the node is part of the map.
int l_node_valid( lua_State* L ){
	lua_pushboolean( L, Node_firstInstance( lua_checkNode( L, 1 ) ) != nullptr );
	return 1;
}

/// node:parent(): the entity of a brush or patch, nil for entities.
int l_node_parent( lua_State* L ){
	scene::Instance& instance = lua_checkInstance( L, 1 );
	const scene::Path& path = instance.path();
	if ( path.size() < 3 ) { // root, entity
		lua_pushnil( L );
		return 1;
	}
	LuaApi_pushNode( L, path.parent() );
	return 1;
}

/// node:bounds(): the world space bounding box as min, max vectors.
int l_node_bounds( lua_State* L ){
	const AABB& bounds = lua_checkInstance( L, 1 ).worldAABB();
	LuaApi_pushVector( L, vector3_subtracted( bounds.origin, bounds.extents ) );
	LuaApi_pushVector( L, vector3_added( bounds.origin, bounds.extents ) );
	return 2;
}

/// node:selected(): whether the node is selected.
int l_node_selected( lua_State* L ){
	lua_pushboolean( L, Instance_isSelected( lua_checkInstance( L, 1 ) ) );
	return 1;
}

/// node:select( [selected = true] ): selects or deselects the node;
/// selection changes are not recorded by undo.
int l_node_select( lua_State* L ){
	scene::Instance& instance = lua_checkInstance( L, 1 );
	const bool selected = lua_isnoneornil( L, 2 ) || lua_toboolean( L, 2 );
	if ( Selectable* selectable = Instance_getSelectable( instance ) ) {
		selectable->setSelected( selected );
	}
	return 0;
}

/// node:delete(): removes the node (with its primitives) from the map.
int l_node_delete( lua_State* L ){
	scene::Instance& instance = lua_beginNodeChange( L, 1 );
	if ( Selectable* selectable = Instance_getSelectable( instance ) ) {
		selectable->setSelected( false );
	}
	Path_deleteTop( instance.path() );
	return 0;
}

int l_node_tostring( lua_State* L ){
	scene::Node& node = lua_checkNode( L, 1 );
	if ( Entity* entity = Node_getEntity( node ) ) {
		lua_pushfstring( L, "entity %s: %p", entity->getClassName(), static_cast<void*>( &node ) );
	}
	else
	{
		lua_pushfstring( L, "%s: %p", Node_kind( node ), static_cast<void*>( &node ) );
	}
	return 1;
}

// ---------------------------------------------------------------------------
// entities

/// entity:classname()
int l_entity_classname( lua_State* L ){
	lua_pushstring( L, lua_checkEntity( L, 1 ).getClassName() );
	return 1;
}

/// entity:get( key ): the value, or nil if the key is not set.
int l_entity_get( lua_State* L ){
	Entity& entity = lua_checkEntity( L, 1 );
	const char* key = luaL_checkstring( L, 2 );
	if ( entity.hasKeyValue( key ) ) {
		lua_pushstring( L, entity.getKeyValue( key ) );
	}
	else
	{
		lua_pushnil( L );
	}
	return 1;
}

/// entity:set( key, value ): sets a key, removes it if value is nil or "";
/// numbers are converted to strings.
int l_entity_set( lua_State* L ){
	Entity& entity = lua_checkEntity( L, 1 );
	const char* key = luaL_checkstring( L, 2 );
	const char* value = lua_isnoneornil( L, 3 ) ? "" : luaL_checkstring( L, 3 );
	if ( string_empty( key ) ) {
		luaL_argerror( L, 2, "empty key" );
	}
	if ( string_equal( key, "classname" ) ) {
		// changing it means replacing the entity, which is not supported yet
		luaL_argerror( L, 2, "the classname cannot be changed" );
	}
	lua_beginNodeChange( L, 1 );
	entity.setKeyValue( key, value );
	return 0;
}

/// entity:keys(): a table with all keys and values.
int l_entity_keys( lua_State* L ){
	Entity& entity = lua_checkEntity( L, 1 );
	class Collect : public Entity::Visitor
	{
	public:
		std::vector<std::pair<CopiedString, CopiedString>> m_keyValues;
		void visit( const char* key, const char* value ) override {
			m_keyValues.emplace_back( key, value );
		}
	} visitor;
	entity.forEachKeyValue( visitor );

	lua_createtable( L, 0, static_cast<int>( visitor.m_keyValues.size() ) );
	for ( const auto& [key, value] : visitor.m_keyValues ){
		lua_pushstring( L, value.c_str() );
		lua_setfield( L, -2, key.c_str() );
	}
	return 1;
}

/// entity:is_group(): whether the entity can hold brushes and patches.
int l_entity_is_group( lua_State* L ){
	lua_pushboolean( L, lua_checkEntity( L, 1 ).isContainer() );
	return 1;
}

/// entity:primitives(): the brushes and patches of the entity.
int l_entity_primitives( lua_State* L ){
	scene::Node& node = lua_checkNode( L, 1 );
	lua_checkEntity( L, 1 );
	lua_pushNodeList( L, Node_children( node ) );
	return 1;
}

// ---------------------------------------------------------------------------
// brushes and faces

void lua_pushFace( lua_State* L, scene::Node& brushNode, Face* face ){
	LuaFace* handle = static_cast<LuaFace*>( lua_newuserdata( L, sizeof( LuaFace ) ) );
	handle->brushNode = nullptr;
	handle->face = face;
	luaL_getmetatable( L, c_faceMetatable );
	lua_setmetatable( L, -2 );
	handle->brushNode = &brushNode;
	brushNode.IncRef();
}

int l_face_gc( lua_State* L ){
	LuaFace* handle = static_cast<LuaFace*>( luaL_checkudata( L, 1, c_faceMetatable ) );
	if ( handle->brushNode != nullptr ) {
		scene::Node* node = handle->brushNode;
		handle->brushNode = nullptr;
		node->DecRef();
	}
	return 0;
}

int l_face_eq( lua_State* L ){
	LuaFace* a = static_cast<LuaFace*>( luaL_checkudata( L, 1, c_faceMetatable ) );
	LuaFace* b = static_cast<LuaFace*>( luaL_checkudata( L, 2, c_faceMetatable ) );
	lua_pushboolean( L, a->brushNode == b->brushNode && a->face == b->face );
	return 1;
}

/// The face of a face argument; errors if the face is no longer part of
/// its brush (e.g. after the brush was changed by CSG or undo), or if the
/// brush is not part of the map, like node handles do.
Face& lua_checkFace( lua_State* L, int index ){
	LuaFace* handle = static_cast<LuaFace*>( luaL_checkudata( L, index, c_faceMetatable ) );
	if ( handle->brushNode != nullptr ) {
		if ( Brush* brush = Node_getBrush( *handle->brushNode ) ) {
			for ( const FaceSmartPointer& face : *brush ){
				if ( face.get() == handle->face ) {
					if ( Node_firstInstance( *handle->brushNode ) == nullptr ) {
						luaL_argerror( L, index, "the brush of the face is not part of the map (deleted?)" );
					}
					return *handle->face;
				}
			}
		}
	}
	luaL_argerror( L, index, "the face no longer exists" );
	return *handle->face; // not reached
}

/// Checks a face which is about to be changed and opens an undo operation
/// for the change.
Face& lua_beginFaceChange( lua_State* L, int index ){
	Face& face = lua_checkFace( L, index );
	LuaUndo_change();
	return face;
}

/// brush:faces(): the faces of the brush.
int l_brush_faces( lua_State* L ){
	scene::Node& node = lua_checkNode( L, 1 );
	Brush& brush = lua_checkBrush( L, 1 );
	std::vector<Face*> faces;
	for ( const FaceSmartPointer& face : brush ){
		faces.push_back( face.get() );
	}
	lua_createtable( L, static_cast<int>( faces.size() ), 0 );
	for ( std::size_t i = 0; i < faces.size(); ++i ){
		lua_pushFace( L, node, faces[i] );
		lua_rawseti( L, -2, static_cast<int>( i + 1 ) );
	}
	return 1;
}

/// brush:is_detail()
int l_brush_is_detail( lua_State* L ){
	lua_pushboolean( L, lua_checkBrush( L, 1 ).isDetail() );
	return 1;
}

/// brush:set_detail( detail ): makes the brush detail or structural.
int l_brush_set_detail( lua_State* L ){
	Brush& brush = lua_checkBrush( L, 1 );
	const bool detail = lua_toboolean( L, 2 );
	lua_beginNodeChange( L, 1 );
	for ( const FaceSmartPointer& face : brush )
		face->setDetail( detail );
	return 0;
}

/// face:brush(): the brush the face belongs to.
int l_face_brush( lua_State* L ){
	lua_checkFace( L, 1 );
	LuaApi_pushNode( L, *static_cast<LuaFace*>( lua_touserdata( L, 1 ) )->brushNode );
	return 1;
}

/// face:shader()
int l_face_shader( lua_State* L ){
	lua_pushstring( L, lua_checkFace( L, 1 ).GetShader() );
	return 1;
}

/// face:set_shader( name ), e.g. "textures/common/caulk"
int l_face_set_shader( lua_State* L ){
	const char* shader = luaL_checkstring( L, 2 );
	lua_beginFaceChange( L, 1 ).SetShader( shader );
	return 0;
}

/// face:plane(): the outward pointing normal and the distance from the
/// origin, so that normal:dot( point ) == dist for points on the plane.
int l_face_plane( lua_State* L ){
	const Plane3& plane = lua_checkFace( L, 1 ).plane3();
	LuaApi_pushVector( L, plane.normal() );
	lua_pushnumber( L, plane.dist() );
	return 2;
}

/// face:points(): a list of the three points defining the plane, as in
/// map files and create_brush.
int l_face_points( lua_State* L ){
	const PlanePoints& points = lua_checkFace( L, 1 ).getPlane().planePoints();
	lua_createtable( L, 3, 0 );
	for ( int i = 0; i < 3; ++i ){
		LuaApi_pushVector( L, points[i] );
		lua_rawseti( L, -2, i + 1 );
	}
	return 1;
}

/// face:winding(): the corners of the face polygon, an empty list if the
/// face does not contribute to the brush.
int l_face_winding( lua_State* L ){
	const Winding& winding = lua_checkFace( L, 1 ).getWinding();
	const std::vector<DoubleVector3> points( [&winding]{
		std::vector<DoubleVector3> points;
		for ( const WindingVertex& vertex : winding )
			points.push_back( vertex.vertex );
		return points;
	}() );
	lua_createtable( L, static_cast<int>( points.size() ), 0 );
	for ( std::size_t i = 0; i < points.size(); ++i ){
		LuaApi_pushVector( L, points[i] );
		lua_rawseti( L, -2, static_cast<int>( i + 1 ) );
	}
	return 1;
}

/// face:texdef(): the texture alignment as {shift = {s, t}, scale = {s, t},
/// rotate = degrees}, like the surface inspector shows it.
int l_face_texdef( lua_State* L ){
	TextureProjection projection;
	lua_checkFace( L, 1 ).GetTexdef( projection );
	texdef_t texdef;
	ShiftScaleRotate_fromFace( texdef, projection );

	lua_createtable( L, 0, 3 );
	const auto pushPair = [L]( const char* name, const float* values ){
		lua_createtable( L, 2, 0 );
		lua_pushnumber( L, values[0] );
		lua_rawseti( L, -2, 1 );
		lua_pushnumber( L, values[1] );
		lua_rawseti( L, -2, 2 );
		lua_setfield( L, -2, name );
	};
	pushPair( "shift", texdef.shift );
	pushPair( "scale", texdef.scale );
	lua_pushnumber( L, texdef.rotate );
	lua_setfield( L, -2, "rotate" );
	return 1;
}

/// Reads the optional number pair field \p name of the table at \p index.
bool lua_optPair( lua_State* L, int index, const char* name, float values[2] ){
	lua_getfield( L, index, name );
	if ( lua_isnil( L, -1 ) ) {
		lua_pop( L, 1 );
		return false;
	}
	if ( !lua_istable( L, -1 ) ) {
		luaL_error( L, "texdef field '%s' must be a pair {s, t}", name );
	}
	for ( int i = 0; i < 2; ++i ){
		lua_rawgeti( L, -1, i + 1 );
		if ( !lua_isnumber( L, -1 ) ) {
			luaL_error( L, "texdef field '%s' must be a pair {s, t}", name );
		}
		values[i] = static_cast<float>( lua_tonumber( L, -1 ) );
		lua_pop( L, 1 );
	}
	lua_pop( L, 1 );
	return true;
}

/// face:set_texdef{ shift = {s, t}, scale = {s, t}, rotate = degrees }:
/// changes the given parts of the texture alignment, see face:texdef().
int l_face_set_texdef( lua_State* L ){
	lua_checkFace( L, 1 );
	luaL_checktype( L, 2, LUA_TTABLE );
	float shift[2], scale[2], rotate;
	const bool hasShift = lua_optPair( L, 2, "shift", shift );
	const bool hasScale = lua_optPair( L, 2, "scale", scale );
	lua_getfield( L, 2, "rotate" );
	const bool hasRotate = !lua_isnil( L, -1 );
	if ( hasRotate ) {
		if ( !lua_isnumber( L, -1 ) ) {
			luaL_error( L, "texdef field 'rotate' must be a number" );
		}
		rotate = static_cast<float>( lua_tonumber( L, -1 ) );
	}
	lua_pop( L, 1 );

	lua_beginFaceChange( L, 1 ).SetTexdef( hasShift ? &shift[0] : nullptr, hasShift ? &shift[1] : nullptr,
	                                       hasScale ? &scale[0] : nullptr, hasScale ? &scale[1] : nullptr,
	                                       hasRotate ? &rotate : nullptr );
	return 0;
}

/// face:fit( [s_repeat = 1, t_repeat = 1] ): fits the texture to the face.
int l_face_fit( lua_State* L ){
	const float s = static_cast<float>( luaL_optnumber( L, 2, 1 ) );
	const float t = static_cast<float>( luaL_optnumber( L, 3, 1 ) );
	lua_beginFaceChange( L, 1 ).FitTexture( s, t, false );
	return 0;
}

int l_face_tostring( lua_State* L ){
	LuaFace* handle = static_cast<LuaFace*>( luaL_checkudata( L, 1, c_faceMetatable ) );
	lua_pushfstring( L, "face: %p", static_cast<void*>( handle->face ) );
	return 1;
}

// ---------------------------------------------------------------------------
// patches

/// patch:shader(), brush:shader(): the shader of the patch, or the one
/// all faces of the brush have (nil if they differ).
int l_primitive_shader( lua_State* L ){
	scene::Node& node = lua_checkNode( L, 1 );
	if ( Brush* brush = Node_getBrush( node ) ) {
		const char* shader = nullptr;
		for ( const FaceSmartPointer& face : *brush ){
			if ( shader == nullptr ) {
				shader = face->GetShader();
			}
			else if ( !shader_equal( shader, face->GetShader() ) ) {
				shader = nullptr;
				break;
			}
		}
		lua_pushstring( L, shader ); // nil for nullptr
		return 1;
	}
	lua_pushstring( L, lua_checkPatch( L, 1 ).GetShader() );
	return 1;
}

/// patch:set_shader( name ), brush:set_shader( name ): sets the shader of
/// the patch or of all faces of the brush.
int l_primitive_set_shader( lua_State* L ){
	scene::Node& node = lua_checkNode( L, 1 );
	const char* shader = luaL_checkstring( L, 2 );
	if ( Brush* brush = Node_getBrush( node ) ) {
		lua_beginNodeChange( L, 1 );
		for ( const FaceSmartPointer& face : *brush )
			face->SetShader( shader );
		return 0;
	}
	Patch& patch = lua_checkPatch( L, 1 );
	lua_beginNodeChange( L, 1 );
	patch.SetShader( shader );
	return 0;
}

/// patch:size(): the number of control point columns and rows.
int l_patch_size( lua_State* L ){
	Patch& patch = lua_checkPatch( L, 1 );
	lua_pushinteger( L, static_cast<lua_Integer>( patch.getWidth() ) );
	lua_pushinteger( L, static_cast<lua_Integer>( patch.getHeight() ) );
	return 2;
}

/// patch:control_point( column, row ): position and texture coordinates
/// s, t of a control point; indices start at 1.
int l_patch_control_point( lua_State* L ){
	Patch& patch = lua_checkPatch( L, 1 );
	const lua_Integer column = luaL_checkinteger( L, 2 );
	const lua_Integer row = luaL_checkinteger( L, 3 );
	luaL_argcheck( L, column >= 1 && column <= static_cast<lua_Integer>( patch.getWidth() ), 2, "column out of range" );
	luaL_argcheck( L, row >= 1 && row <= static_cast<lua_Integer>( patch.getHeight() ), 3, "row out of range" );
	const PatchControl& control = patch.ctrlAt( row - 1, column - 1 );
	LuaApi_pushVector( L, control.m_vertex );
	lua_pushnumber( L, control.m_texcoord[0] );
	lua_pushnumber( L, control.m_texcoord[1] );
	return 3;
}

const luaL_Reg c_nodeMethods[] = {
	{ "kind", l_node_kind },
	{ "valid", l_node_valid },
	{ "parent", l_node_parent },
	{ "bounds", l_node_bounds },
	{ "selected", l_node_selected },
	{ "select", l_node_select },
	{ "delete", l_node_delete },
	// entities
	{ "classname", l_entity_classname },
	{ "get", l_entity_get },
	{ "set", l_entity_set },
	{ "keys", l_entity_keys },
	{ "is_group", l_entity_is_group },
	{ "primitives", l_entity_primitives },
	// brushes
	{ "faces", l_brush_faces },
	{ "is_detail", l_brush_is_detail },
	{ "set_detail", l_brush_set_detail },
	// brushes and patches
	{ "shader", l_primitive_shader },
	{ "set_shader", l_primitive_set_shader },
	// patches
	{ "size", l_patch_size },
	{ "control_point", l_patch_control_point },
	{ nullptr, nullptr }
};

const luaL_Reg c_faceMethods[] = {
	{ "brush", l_face_brush },
	{ "shader", l_face_shader },
	{ "set_shader", l_face_set_shader },
	{ "plane", l_face_plane },
	{ "points", l_face_points },
	{ "winding", l_face_winding },
	{ "texdef", l_face_texdef },
	{ "set_texdef", l_face_set_texdef },
	{ "fit", l_face_fit },
	{ nullptr, nullptr }
};

// ---------------------------------------------------------------------------
// creation

/// Checks the optional entity argument to put new primitives into: it
/// must be a group entity in the map. Returns nullptr for the default,
/// the worldspawn.
scene::Node* lua_checkPrimitiveParent( lua_State* L, int index ){
	if ( lua_isnoneornil( L, index ) ) {
		return nullptr;
	}
	scene::Node& node = lua_checkNode( L, index );
	Entity* entity = Node_getEntity( node );
	if ( entity == nullptr || !entity->isContainer() ) {
		luaL_argerror( L, index, "expected a group entity, which can hold brushes" );
	}
	lua_checkInstance( L, index );
	return &node;
}

/// Inserts a newly built brush into \p parent (nullptr: the worldspawn,
/// created if missing), as a change recorded by undo. Returns false and
/// changes nothing if the brush has no volume.
bool Brush_insertIfValid( scene::Node* parent, scene::Node& node ){
	Brush& brush = *Node_getBrush( node );
	brush.evaluateBRep();
	std::size_t contributing = 0;
	for ( const FaceSmartPointer& face : brush ){
		if ( face->contributes() ) {
			++contributing;
		}
	}
	if ( contributing < 4 ) {
		return false;
	}
	LuaUndo_change();
	if ( parent == nullptr ) {
		parent = &Map_FindOrInsertWorldspawn( g_map );
	}
	Node_getTraversable( *parent )->insert( node );
	return true;
}

/// luadiant.create_entity( classname [, keys] ): creates an entity with
/// the given key/value table; returns it. Brushes can be added to it if
/// its entity class is a group entity (e.g. func_group).
int l_create_entity( lua_State* L ){
	const char* classname = luaL_checkstring( L, 1 );
	if ( string_empty( classname ) ) {
		luaL_argerror( L, 1, "empty classname" );
	}
	const int keys = 2;
	if ( !lua_isnoneornil( L, keys ) ) {
		luaL_checktype( L, keys, LUA_TTABLE );
		// check the table before creating anything
		lua_pushnil( L );
		while ( lua_next( L, keys ) != 0 ){
			if ( lua_type( L, -2 ) != LUA_TSTRING || !lua_isstring( L, -1 ) ) {
				luaL_argerror( L, keys, "keys and values must be strings (or numbers as values)" );
			}
			lua_pop( L, 1 );
		}
	}

	EntityClass* entityClass = GlobalEntityClassManager().findOrInsert( classname, false );
	scene::Node& node = GlobalEntityCreator().createEntity( entityClass );
	LuaApi_pushNode( L, node ); // holds a reference from now on
	const int handle = lua_gettop( L );

	// set the keys before inserting, so they are not recorded one by one
	if ( !lua_isnoneornil( L, keys ) ) {
		Entity* entity = Node_getEntity( node );
		lua_pushnil( L );
		while ( lua_next( L, keys ) != 0 ){
			const char* key = lua_tostring( L, -2 );
			if ( !string_equal( key, "classname" ) ) {
				entity->setKeyValue( key, lua_tostring( L, -1 ) );
			}
			lua_pop( L, 1 );
		}
	}

	LuaUndo_change();
	Node_getTraversable( GlobalSceneGraph().root() )->insert( node );
	lua_settop( L, handle );
	return 1;
}

/// luadiant.create_box( min, max [, shader [, entity]] ): creates an axis
/// aligned box brush in the entity (default: worldspawn); returns it.
int l_create_box( lua_State* L ){
	const DoubleVector3 min = lua_checkVector( L, 1 );
	const DoubleVector3 max = lua_checkVector( L, 2 );
	const char* shader = luaL_optstring( L, 3, texdef_name_default() );
	for ( int i = 0; i < 3; ++i ){
		if ( !( min[i] < max[i] ) ) {
			luaL_error( L, "create_box: min must be smaller than max on every axis" );
		}
	}
	scene::Node* parent = lua_checkPrimitiveParent( L, 4 );

	scene::Node& node = GlobalBrushCreator().createBrush();
	LuaApi_pushNode( L, node );
	const AABB bounds = aabb_for_minmax( Vector3( min ), Vector3( max ) );
	Brush_ConstructCuboid( *Node_getBrush( node ), bounds, shader, TextureTransform_getDefault() );
	if ( !Brush_insertIfValid( parent, node ) ) {
		luaL_error( L, "create_box: the box is outside of the world bounds" );
	}
	return 1;
}

/// luadiant.create_brush( planes [, entity] ): creates a brush from a list
/// of planes, each given by three points {p1, p2, p3, shader = name} like
/// in map files (clockwise, seen from outside of the brush); returns it.
/// Raises an error if the planes do not enclose a volume.
int l_create_brush( lua_State* L ){
	luaL_checktype( L, 1, LUA_TTABLE );
	const int count = static_cast<int>( lua_objlen( L, 1 ) );
	if ( count < 4 ) {
		luaL_argerror( L, 1, "a brush needs at least 4 planes" );
	}
	scene::Node* parent = lua_checkPrimitiveParent( L, 2 );

	// the brush is only inserted at the end, an error before just leaves
	// it to the garbage collector
	scene::Node& node = GlobalBrushCreator().createBrush();
	LuaApi_pushNode( L, node );
	Brush& brush = *Node_getBrush( node );
	for ( int i = 1; i <= count; ++i ){
		lua_rawgeti( L, 1, i );
		if ( !lua_istable( L, -1 ) ) {
			luaL_error( L, "create_brush: plane %d is not a table", i );
		}
		DoubleVector3 points[3];
		for ( int p = 0; p < 3; ++p ){
			lua_rawgeti( L, -1, p + 1 );
			points[p] = lua_checkVector( L, -1 );
			lua_pop( L, 1 );
		}
		lua_getfield( L, -1, "shader" );
		const char* shader = lua_isnil( L, -1 ) ? texdef_name_default() : lua_tostring( L, -1 );
		if ( shader == nullptr ) {
			luaL_error( L, "create_brush: the shader of plane %d is not a string", i );
		}
		if ( brush.addPlane( points[0], points[1], points[2], shader, TextureTransform_getDefault() ) == nullptr ) {
			luaL_error( L, "create_brush: too many planes" );
		}
		lua_pop( L, 2 );
	}
	if ( !Brush_insertIfValid( parent, node ) ) {
		luaL_error( L, "create_brush: the planes do not enclose a volume" );
	}
	return 1;
}

// ---------------------------------------------------------------------------
// selection

/// luadiant.selection(): the selected entities, brushes and patches.
int l_selection( lua_State* L ){
	std::vector<scene::Node*> nodes;
	class Collect : public SelectionSystem::Visitor
	{
		std::vector<scene::Node*>& m_nodes;
	public:
		Collect( std::vector<scene::Node*>& nodes ) : m_nodes( nodes ){
		}
		void visit( scene::Instance& instance ) const override {
			m_nodes.push_back( &instance.path().top().get() );
		}
	};
	GlobalSelectionSystem().foreachSelected( Collect( nodes ) );
	lua_pushNodeList( L, nodes );
	return 1;
}

/// luadiant.select_none(): deselects everything.
int l_select_none( lua_State* L ){
	GlobalSelectionSystem().setSelectedAll( false );
	return 0;
}

const luaL_Reg c_sceneFunctions[] = {
	{ "create_entity", l_create_entity },
	{ "create_box", l_create_box },
	{ "create_brush", l_create_brush },
	{ "selection", l_selection },
	{ "select_none", l_select_none },
	{ nullptr, nullptr }
};

/// Creates the metatable \p name with the given __gc and __tostring
/// and a method table as __index; leaves the method table on the stack.
void lua_newHandleMetatable( lua_State* L, const char* name, lua_CFunction gc, lua_CFunction tostring, const luaL_Reg* methods ){
	luaL_newmetatable( L, name );
	lua_pushcfunction( L, gc );
	lua_setfield( L, -2, "__gc" );
	lua_pushcfunction( L, tostring );
	lua_setfield( L, -2, "__tostring" );
	lua_newtable( L );
	luaL_register( L, nullptr, methods );
	lua_pushvalue( L, -1 );
	lua_setfield( L, -3, "__index" );
	lua_remove( L, -2 );
}
}

void LuaApi_pushVector3( lua_State* L, double x, double y, double z ){
	lua_createtable( L, 0, 3 );
	lua_pushnumber( L, x );
	lua_setfield( L, -2, "x" );
	lua_pushnumber( L, y );
	lua_setfield( L, -2, "y" );
	lua_pushnumber( L, z );
	lua_setfield( L, -2, "z" );
	luaL_getmetatable( L, c_vec3Metatable );
	lua_setmetatable( L, -2 );
}

void LuaApi_pushNode( lua_State* L, scene::Node& node ){
	lua_getfield( L, LUA_REGISTRYINDEX, c_nodeHandles );
	lua_pushlightuserdata( L, &node );
	lua_rawget( L, -2 );
	// a handle whose __gc already ran may still be in the weak table
	if ( LuaNode* existing = static_cast<LuaNode*>( lua_touserdata( L, -1 ) ); existing != nullptr && existing->node == &node ) {
		lua_remove( L, -2 );
		return;
	}
	lua_pop( L, 1 );

	LuaNode* handle = static_cast<LuaNode*>( lua_newuserdata( L, sizeof( LuaNode ) ) );
	handle->node = nullptr;
	luaL_getmetatable( L, c_nodeMetatable );
	lua_setmetatable( L, -2 );
	handle->node = &node;
	node.IncRef();

	lua_pushlightuserdata( L, &node );
	lua_pushvalue( L, -2 );
	lua_rawset( L, -4 );
	lua_remove( L, -2 );
}

void LuaApi_openScene( lua_State* L ){
	// the vector metatable gets its methods from lua/luadiant/vec3.lua
	luaL_newmetatable( L, c_vec3Metatable );
	lua_setfield( L, -2, "_vec3_metatable" );

	lua_newtable( L );
	lua_createtable( L, 0, 1 );
	lua_pushliteral( L, "v" );
	lua_setfield( L, -2, "__mode" );
	lua_setmetatable( L, -2 );
	lua_setfield( L, LUA_REGISTRYINDEX, c_nodeHandles );

	// the method tables are exposed, so lua/luadiant/ can add more methods
	lua_newHandleMetatable( L, c_nodeMetatable, l_node_gc, l_node_tostring, c_nodeMethods );
	lua_setfield( L, -2, "_node_methods" );
	lua_newHandleMetatable( L, c_faceMetatable, l_face_gc, l_face_tostring, c_faceMethods );
	luaL_getmetatable( L, c_faceMetatable );
	lua_pushcfunction( L, l_face_eq );
	lua_setfield( L, -2, "__eq" );
	lua_pop( L, 1 );
	lua_setfield( L, -2, "_face_methods" );

	luaL_register( L, nullptr, c_sceneFunctions );
}
