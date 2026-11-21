-- 3D vectors: tables {x, y, z} sharing one metatable, so they support
-- arithmetic, comparison and a few methods. The C++ API returns all
-- points, normals and bounds as such vectors, and accepts any table with
-- x/y/z fields or three array elements wherever it takes a vector.
--
--   local v = luadiant.vec3( 1, 2, 3 )
--   print( ( v + { 1, 1, 1 } ) * 2, v:length(), v:dot( v ) )

local core = require "luadiant.core"

local mt = core._vec3_metatable
local methods = {}

local sqrt = math.sqrt

local function new( x, y, z )
	return setmetatable( { x = x, y = y, z = z }, mt )
end

-- converts vectors, {x=, y=, z=} and {x, y, z} tables to their components
local function components( v, name )
	if type( v ) ~= "table" then
		error( ( "bad %s (vector expected, got %s)" ):format( name or "vector", type( v ) ), 3 )
	end
	local x, y, z = v.x, v.y, v.z
	if x == nil then
		x, y, z = v[1], v[2], v[3]
	end
	if type( x ) ~= "number" or type( y ) ~= "number" or type( z ) ~= "number" then
		error( ( "bad %s (vector with three numbers expected)" ):format( name or "vector" ), 3 )
	end
	return x, y, z
end

mt.__index = function( v, key )
	-- array style access, so vectors can be passed where {x, y, z} is expected
	if key == 1 then
		return rawget( v, "x" )
	elseif key == 2 then
		return rawget( v, "y" )
	elseif key == 3 then
		return rawget( v, "z" )
	end
	return methods[key]
end

mt.__add = function( a, b )
	local ax, ay, az = components( a, "left operand" )
	local bx, by, bz = components( b, "right operand" )
	return new( ax + bx, ay + by, az + bz )
end

mt.__sub = function( a, b )
	local ax, ay, az = components( a, "left operand" )
	local bx, by, bz = components( b, "right operand" )
	return new( ax - bx, ay - by, az - bz )
end

-- scaling by a number from either side, or component-wise with a vector
mt.__mul = function( a, b )
	if type( a ) == "number" then
		local x, y, z = components( b, "right operand" )
		return new( a * x, a * y, a * z )
	elseif type( b ) == "number" then
		local x, y, z = components( a, "left operand" )
		return new( x * b, y * b, z * b )
	end
	local ax, ay, az = components( a, "left operand" )
	local bx, by, bz = components( b, "right operand" )
	return new( ax * bx, ay * by, az * bz )
end

mt.__div = function( a, b )
	if type( b ) ~= "number" then
		error( "vectors can only be divided by numbers", 2 )
	end
	local x, y, z = components( a, "left operand" )
	return new( x / b, y / b, z / b )
end

mt.__unm = function( v )
	return new( -v.x, -v.y, -v.z )
end

mt.__eq = function( a, b )
	return a.x == b.x and a.y == b.y and a.z == b.z
end

-- integral components print without decimals, as in map files
local function format_number( n )
	if n == math.floor( n ) and math.abs( n ) < 2^53 then
		return ( "%d" ):format( n )
	end
	return ( "%.17g" ):format( n )
end

mt.__tostring = function( v )
	return ( "(%s %s %s)" ):format( format_number( v.x ), format_number( v.y ), format_number( v.z ) )
end

function methods.dot( a, b )
	local bx, by, bz = components( b )
	return a.x * bx + a.y * by + a.z * bz
end

function methods.cross( a, b )
	local bx, by, bz = components( b )
	return new( a.y * bz - a.z * by, a.z * bx - a.x * bz, a.x * by - a.y * bx )
end

function methods.length( v )
	return sqrt( v.x * v.x + v.y * v.y + v.z * v.z )
end

-- the vector scaled to length 1; the zero vector stays as it is
function methods.normalized( v )
	local length = methods.length( v )
	if length == 0 then
		return new( 0, 0, 0 )
	end
	return new( v.x / length, v.y / length, v.z / length )
end

function methods.distance( a, b )
	local bx, by, bz = components( b )
	local dx, dy, dz = a.x - bx, a.y - by, a.z - bz
	return sqrt( dx * dx + dy * dy + dz * dz )
end

-- true if all components differ by at most epsilon (default 1e-6)
function methods.near( a, b, epsilon )
	local bx, by, bz = components( b )
	epsilon = epsilon or 1e-6
	return math.abs( a.x - bx ) <= epsilon
		and math.abs( a.y - by ) <= epsilon
		and math.abs( a.z - bz ) <= epsilon
end

function methods.unpack( v )
	return v.x, v.y, v.z
end

-- vec3( x, y, z ) or vec3( table ) with x/y/z fields or three array elements
local function vec3( x, y, z )
	if type( x ) == "table" then
		return new( components( x ) )
	end
	if type( x ) ~= "number" or type( y ) ~= "number" or type( z ) ~= "number" then
		error( "vec3 expects three numbers or a table", 2 )
	end
	return new( x, y, z )
end

return {
	new = vec3,
	-- true for vectors created by vec3 or returned by the API
	is_vec3 = function( v )
		return getmetatable( v ) == mt
	end,
}
