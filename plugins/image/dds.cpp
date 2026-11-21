/*
   Copyright (C) 2001-2006, William Joseph.
   All Rights Reserved.

   This file is part of GtkRadiant.

   GtkRadiant is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   GtkRadiant is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with GtkRadiant; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "dds.h"

#include <cstdlib>

#include "ifilesystem.h"
#include "iarchive.h"
#include "idatastream.h"

#include "ddslib.h"
#include "imagelib.h"

Image* LoadDDSBuff( const byte* buffer, std::size_t length ){
	int width, height;
	ddsPF_t pixelFormat;
	// DDSGetInfo() does not know the buffer length, so first make sure the
	// header and the top mip level are actually in the buffer
	if ( buffer == nullptr || DDSCheckLength( reinterpret_cast<ddsBuffer_t*>( const_cast<byte*>( buffer ) ), length ) != 0 ) {
		return 0;
	}
	if ( DDSGetInfo( reinterpret_cast<ddsBuffer_t*>( const_cast<byte*>( buffer ) ), &width, &height, &pixelFormat ) == -1 ) {
		return 0;
	}

	auto *image = new RGBAImage( width, height );

	if ( DDSDecompress( reinterpret_cast<ddsBuffer_t*>( const_cast<byte*>( buffer ) ), image->getRGBAPixels() ) == -1 ) {
		image->release();
		return 0;
	}
	return image;
}

Image* LoadDDS( ArchiveFile& file ){
	ScopedArchiveBuffer buffer( file );
	return LoadDDSBuff( buffer.buffer, buffer.length );
}
