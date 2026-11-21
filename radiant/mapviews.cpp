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

#include "mapviews.h"

#include <algorithm>
#include <vector>

#include <QByteArray>
#include <QFile>
#include <QFileInfo>
#include <QList>
#include <QSaveFile>
#include <QString>

#include "math/vector.h"
#include "qerplugin.h"
#include "mainframe.h"
#include "camwindow.h"
#include "xywindow.h"

bool g_bRestoreMapViews = true;

namespace
{

// How many maps are remembered; the least recently used ones are dropped.
const std::size_t c_maxEntries = 100;

// The file in the settings directory. One map per line, most recently
// used first, fields separated by tabs:
//   <path> \t <camera: ox oy oz pitch yaw roll> [\t <2d view: type ox oy oz scale>]...
// The 2D views are matched by view type on restore, so layouts with a
// different set of 2D windows still get the views they have in common.
const char* const c_fileName = "mapviews.txt";

struct XYView
{
	int viewType;
	Vector3 origin;
	float scale;
};

struct MapView
{
	QByteArray path;
	Vector3 cameraOrigin;
	Vector3 cameraAngles;
	std::vector<XYView> xyViews;
};

QString MapViews_filePath(){
	return QString::fromUtf8( SettingsPath_get() ) + c_fileName;
}

// Maps are keyed by their canonical absolute path, so a map opened via a
// relative command line path or a symlink shares its entry. A map that no
// longer exists has no canonical path, the absolute one is used then.
QByteArray MapViews_key( const char* mapname ){
	const QFileInfo info( QString::fromUtf8( mapname ) );
	const QString canonical = info.canonicalFilePath();
	return ( canonical.isEmpty() ? info.absoluteFilePath() : canonical ).toUtf8();
}

// Parses `count` space separated floats of `field` into `out`.
bool MapViews_parseFloats( const QByteArray& field, float* out, int count ){
	const QList<QByteArray> parts = field.simplified().split( ' ' );
	if ( parts.size() != count ) {
		return false;
	}
	for ( int i = 0; i < count; ++i )
	{
		bool ok;
		out[i] = parts[i].toFloat( &ok );
		if ( !ok ) {
			return false;
		}
	}
	return true;
}

// Parses one line of the file; malformed lines are skipped by the caller.
bool MapViews_parseLine( const QByteArray& line, MapView& view ){
	const QList<QByteArray> fields = line.split( '\t' );
	if ( fields.size() < 2 || fields[0].isEmpty() ) {
		return false;
	}
	view.path = fields[0];

	float camera[6];
	if ( !MapViews_parseFloats( fields[1], camera, 6 ) ) {
		return false;
	}
	view.cameraOrigin = Vector3( camera[0], camera[1], camera[2] );
	view.cameraAngles = Vector3( camera[3], camera[4], camera[5] );

	for ( int i = 2; i < fields.size(); ++i )
	{
		float xy[5];
		if ( !MapViews_parseFloats( fields[i], xy, 5 ) ) {
			return false;
		}
		view.xyViews.push_back( { static_cast<int>( xy[0] ), Vector3( xy[1], xy[2], xy[3] ), xy[4] } );
	}
	return true;
}

std::vector<MapView> MapViews_load(){
	std::vector<MapView> views;
	QFile file( MapViews_filePath() );
	if ( !file.open( QIODevice::ReadOnly ) ) {
		return views; // nothing remembered yet
	}
	while ( !file.atEnd() )
	{
		MapView view;
		if ( MapViews_parseLine( file.readLine().trimmed(), view ) ) {
			views.push_back( std::move( view ) );
		}
	}
	return views;
}

QByteArray MapViews_formatFloats( std::initializer_list<float> values ){
	QByteArray out;
	for ( const float value : values )
	{
		if ( !out.isEmpty() ) {
			out += ' ';
		}
		// 9 significant digits round-trip any float exactly
		out += QByteArray::number( value, 'g', 9 );
	}
	return out;
}

// Writes the whole file atomically, so a crash while saving never loses
// the views of the other maps.
void MapViews_save( const std::vector<MapView>& views ){
	QSaveFile file( MapViews_filePath() );
	if ( !file.open( QIODevice::WriteOnly ) ) {
		globalWarningStream() << "Failed to write " << file.fileName().toUtf8().constData() << '\n';
		return;
	}
	for ( const MapView& view : views )
	{
		QByteArray line = view.path;
		line += '\t';
		line += MapViews_formatFloats( { view.cameraOrigin[0], view.cameraOrigin[1], view.cameraOrigin[2],
		                                 view.cameraAngles[0], view.cameraAngles[1], view.cameraAngles[2] } );
		for ( const XYView& xy : view.xyViews )
		{
			line += '\t';
			line += MapViews_formatFloats( { static_cast<float>( xy.viewType ), xy.origin[0], xy.origin[1], xy.origin[2], xy.scale } );
		}
		line += '\n';
		file.write( line );
	}
	if ( !file.commit() ) {
		globalWarningStream() << "Failed to write " << file.fileName().toUtf8().constData() << '\n';
	}
}

// The camera window, or null when headless (no main window) or disabled.
CamWnd* MapViews_camWnd(){
	if ( !g_bRestoreMapViews || g_pParentWnd == nullptr ) {
		return nullptr;
	}
	return g_pParentWnd->GetCamWnd();
}

} // namespace

void MapViews_store( const char* mapname ){
	CamWnd* camwnd = MapViews_camWnd();
	if ( camwnd == nullptr ) {
		return;
	}

	MapView view;
	view.path = MapViews_key( mapname );
	if ( view.path.contains( '\t' ) || view.path.contains( '\n' ) || view.path.contains( '\r' ) ) {
		return; // can't be represented in the line based file
	}
	view.cameraOrigin = Camera_getOrigin( *camwnd );
	view.cameraAngles = Camera_getAngles( *camwnd );
	g_pParentWnd->forEachXYWnd( [&view]( XYWnd* xywnd ){
		view.xyViews.push_back( { xywnd->GetViewType(), xywnd->GetOrigin(), xywnd->Scale() } );
	} );

	// move this map to the front, dropping its old entry and the oldest
	// entries beyond the limit
	std::vector<MapView> views = MapViews_load();
	views.erase( std::remove_if( views.begin(), views.end(), [&view]( const MapView& other ){
		return other.path == view.path;
	} ), views.end() );
	views.insert( views.begin(), std::move( view ) );
	if ( views.size() > c_maxEntries ) {
		views.resize( c_maxEntries );
	}
	MapViews_save( views );
}

bool MapViews_restore( const char* mapname ){
	CamWnd* camwnd = MapViews_camWnd();
	if ( camwnd == nullptr ) {
		return false;
	}

	const QByteArray key = MapViews_key( mapname );
	const std::vector<MapView> views = MapViews_load();
	const auto found = std::find_if( views.begin(), views.end(), [&key]( const MapView& view ){
		return view.path == key;
	} );
	if ( found == views.end() ) {
		return false;
	}
	const MapView& view = *found;

	Camera_setOrigin( *camwnd, view.cameraOrigin );
	Camera_setAngles( *camwnd, view.cameraAngles );

	g_pParentWnd->forEachXYWnd( [&view]( XYWnd* xywnd ){
		const auto xy = std::find_if( view.xyViews.begin(), view.xyViews.end(), [xywnd]( const XYView& xy ){
			return xy.viewType == xywnd->GetViewType();
		} );
		if ( xy != view.xyViews.end() ) {
			xywnd->SetScale( xy->scale );
			xywnd->SetOrigin( xy->origin );
		}
		else
		{
			// no remembered state for this view type, center it on the camera
			xywnd->SetOrigin( view.cameraOrigin );
		}
	} );
	return true;
}
