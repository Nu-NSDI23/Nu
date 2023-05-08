/* -*-mode:c++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */

/* Copyright 2013-2018 the Alfalfa authors
                       and the Massachusetts Institute of Technology

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions are
   met:

      1. Redistributions of source code must retain the above copyright
         notice, this list of conditions and the following disclaimer.

      2. Redistributions in binary form must reproduce the above copyright
         notice, this list of conditions and the following disclaimer in the
         documentation and/or other materials provided with the distribution.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
   HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. */

#include <stdexcept>
#include <fstream>
#include <iterator>

#include "ivf.hh"
#include "file.hh"

using namespace std;

static void memcpy_le32( uint8_t * dest, const uint32_t val )
{
  uint32_t swizzled = htole32( val );
  memcpy( dest, &swizzled, sizeof( swizzled ) );
}

IVF::IVF( const string & filename )
try :
  file_( filename ),
    header_( file_( 0, supported_header_len ) ),
    fourcc_( header_( 8, 4 ).to_string() ),
    width_( header_( 12, 2 ).le16() ),
    height_( header_( 14, 2 ).le16() ),
    frame_rate_( header_( 16, 4 ).le32() ),
    time_scale_( header_( 20, 4 ).le32() ),
    frame_count_( header_( 24, 4 ).le32() ),
    expected_decoder_minihash_( header_( 28, 4 ).le32() ),
    frame_index_()
      {
        if ( header_( 0, 4 ).to_string() != "DKIF" ) {
          throw Invalid( "missing IVF file header" );
        }

        if ( header_( 4, 2 ).le16() != 0 ) {
          throw Unsupported( "not an IVF version 0 file" );
        }

        if ( header_( 6, 2 ).le16() != supported_header_len ) {
          throw Unsupported( "unsupported IVF header length" );
        }

        /* build the index */
        frame_index_.reserve( frame_count_ );

        uint64_t position = supported_header_len;
        for ( uint32_t i = 0; i < frame_count_; i++ ) {
          Chunk frame_header = file_( position, frame_header_len );
          const uint32_t frame_len = frame_header.le32();

          frame_index_.emplace_back( position + frame_header_len, frame_len );
          position += frame_header_len + frame_len;
        }
      }
catch ( const out_of_range & e )
  {
    throw Invalid( "IVF file truncated" );
  }

Chunk IVF::frame( const uint32_t & index ) const
{
  const auto & entry = frame_index_.at( index );
  return file_( entry.first, entry.second );
}

IVF_MEM::IVF_MEM( const string & filename )
try :
  buffer_( load(filename) ),
  fourcc_( Chunk( reinterpret_cast<const uint8_t *>(buffer_.data()), supported_header_len )( 8, 4 ).to_string() ),
  width_( Chunk( reinterpret_cast<const uint8_t *>(buffer_.data()), supported_header_len )( 12, 2 ).le16() ),
  height_( Chunk( reinterpret_cast<const uint8_t *>(buffer_.data()), supported_header_len )( 14, 2 ).le16() ),
  frame_rate_( Chunk( reinterpret_cast<const uint8_t *>(buffer_.data()), supported_header_len )( 16, 4 ).le32() ),
  time_scale_( Chunk( reinterpret_cast<const uint8_t *>(buffer_.data()), supported_header_len )( 20, 4 ).le32() ),
  frame_count_( Chunk( reinterpret_cast<const uint8_t *>(buffer_.data()), supported_header_len )( 24, 4 ).le32() ),
  expected_decoder_minihash_( Chunk( reinterpret_cast<const uint8_t *>(buffer_.data()), supported_header_len )( 28, 4 ).le32() ),
  frame_index_()
  {
    auto header = Chunk( reinterpret_cast<const uint8_t *>(buffer_.data()), supported_header_len );
    if ( header( 0, 4 ).to_string() != "DKIF" ) {
      throw Invalid( "missing IVF file header" );
    }

    if ( header( 4, 2 ).le16() != 0 ) {
      throw Unsupported( "not an IVF version 0 file" );
    }

    if ( header( 6, 2 ).le16() != supported_header_len ) {
      throw Unsupported( "unsupported IVF header length" );
    }

    /* build the index */
    frame_index_.reserve( frame_count_ );

    uint64_t position = supported_header_len;
    for ( uint32_t i = 0; i < frame_count_; i++ ) {
      Chunk frame_header = Chunk( reinterpret_cast<const uint8_t *>(buffer_.data()) + position, frame_header_len );
      const uint32_t frame_len = frame_header.le32();

      frame_index_.emplace_back( position + frame_header_len, frame_len );
      position += frame_header_len + frame_len;
    }
  }
catch ( const out_of_range & e )
  {
    throw Invalid( "IVF file truncated" );
  }

IVF_MEM::IVF_MEM( Chunk header ) :
  buffer_( vector<char>(supported_header_len) ),
  fourcc_( header( 8, 4 ).to_string() ),
  width_( header( 12, 2 ).le16() ),
  height_( header( 14, 2 ).le16() ),
  frame_rate_( header( 16, 4 ).le32() ),
  time_scale_( header( 20, 4 ).le32() ),
  frame_count_( header( 24, 4 ).le32() ),
  expected_decoder_minihash_( header( 28, 4 ).le32() ),
  frame_index_()
{
  if ( header( 0, 4 ).to_string() != "DKIF" ) {
    throw Invalid( "missing IVF file header" );
  }

  if ( header( 4, 2 ).le16() != 0 ) {
    throw Unsupported( "not an IVF version 0 file" );
  }

  if ( header( 6, 2 ).le16() != supported_header_len ) {
    throw Unsupported( "unsupported IVF header length" );
  }

  std::string header_str = header.to_string();
  buffer_.assign(header_str.begin(), header_str.end());
}

vector<char> IVF_MEM::load(const string &filename)
{
  // open the file:
  ifstream file(filename, ios::binary);

  // Stop eating new lines in binary mode!!!
  file.unsetf(ios::skipws);

  // get its size:
  streampos fileSize;

  file.seekg(0, ios::end);
  fileSize = file.tellg();
  file.seekg(0, ios::beg);

  vector<char> vec;
  vec.reserve(fileSize);

  // read the data
  vec.insert(vec.begin(),
             istream_iterator<char>(file),
             istream_iterator<char>());

  return vec;
}

void IVF_MEM::append_frame( const Chunk & header, const Chunk & frame )
{
  /* update the index */
  uint64_t position = buffer_.size();
  uint32_t frame_len = header.le32();
  frame_index_.emplace_back( position + frame_header_len, frame_len );

  auto header_str = header.to_string();
  buffer_.insert(buffer_.end(), header_str.begin(), header_str.end());
  
  auto frame_str = frame.to_string();
  buffer_.insert(buffer_.end(), frame_str.begin(), frame_str.end());
  frame_count_++;
  memcpy_le32( reinterpret_cast<uint8_t *>(buffer_.data()) + 24, frame_count_ );
}

void IVF_MEM::set_expected_decoder_minihash( const uint32_t minihash )
{
  expected_decoder_minihash_ = minihash;
  memcpy_le32( reinterpret_cast<uint8_t *>(buffer_.data()) + 28, frame_count_ );
}

Chunk IVF_MEM::frame( const uint32_t & index ) const
{
  const auto & entry = frame_index_.at( index );
  return Chunk(reinterpret_cast<const uint8_t *>(buffer_.data()) + entry.first, entry.second);
}

void IVF_MEM::write( const string & filename )
{
  auto fd = FileDescriptor( SystemCall( filename, open( filename.c_str(), O_RDWR | O_CREAT | O_TRUNC,
                                 S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH ) ) );
  fd.write( Chunk( reinterpret_cast<uint8_t *>(buffer_.data()), buffer_.size() ) );
}
