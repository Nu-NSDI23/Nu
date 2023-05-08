#ifndef BUFFER_READER_HH
#define BUFFER_READER_HH

#include <string>
#include <unistd.h>
#include <sys/stat.h>
#include <algorithm>
#include <cstdio>
#include <cassert>
#include <vector>
#include <fstream>

#include "exception.hh"

class BufferReader
{
private:
  bool eof_ { false };
  size_t read_idx_, read_count_;
  std::vector<char> buffer_;

protected:
  void register_read( void ) { read_count_++; }

public:
  BufferReader() : read_idx_( 0 ), read_count_( 0 ), buffer_() {}
  BufferReader( const std::string path ) : read_idx_( 0 ), read_count_( 0 ), buffer_() {
    std::ifstream file(path, std::ios::binary);

    file.seekg(0, std::ios::end);
    size_t fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    buffer_.resize(fileSize);
    file.read(buffer_.data(), fileSize);
  }

  template <class Archive>
  void save(Archive &ar) const {
    ar(eof_, read_idx_, read_count_, buffer_);
  }

  template <class Archive>
  void load(Archive &ar) {
    ar(eof_, read_idx_, read_count_, buffer_);
  }

  uint64_t size( void ) const
  {
    return buffer_.size();
  }

  bool eof() { return eof_; }

  unsigned int read_count( void ) const { return read_count_; }

  std::string getline()
  {
    std::string ret;

    while ( true ) {
      std::string char_read = read( 1 );

      if ( eof() or char_read == "\n" ) {
        break;
      }

      ret.append( char_read );
    }

    return ret;
  }

  std::string read( const size_t limit = BUFFER_SIZE )
  {
    if ( eof() ) {
      throw std::runtime_error( "read() called after eof was set" );
    }

    int bytes_read = std::min(std::min(BUFFER_SIZE, limit), size() - read_idx_);

    if ( bytes_read <= 0 ) {
      eof_ = true;
    }

    register_read();

    std::string ret( buffer_.cbegin() + read_idx_, buffer_.cbegin() + read_idx_ + bytes_read );
    read_idx_ += bytes_read;
    return ret;
  }

  std::string read_exactly( const size_t length )
  {
    std::string ret;
    while ( ret.size() < length ) {
      ret.append( read( length - ret.size() ) );
      if ( eof() ) {
        throw std::runtime_error( "read_exactly: FileDescriptor reached EOF before reaching target" );
      }
    }

    assert( ret.size() == length );
    return ret;
  }

  void reset()
  {
    eof_ = false;
    read_idx_ = 0;
    read_count_ = 0;
  }
};

#endif /* BUFFER_READER_HH */
