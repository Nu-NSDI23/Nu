/*! \file pbds.hpp
    \brief Support for type __gnu_pbds::tree
    \ingroup STLSupport */
/*
  Copyright (c) 2014, Randolph Voorhies, Shane Grant
  All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:
      * Redistributions of source code must retain the above copyright
        notice, this list of conditions and the following disclaimer.
      * Redistributions in binary form must reproduce the above copyright
        notice, this list of conditions and the following disclaimer in the
        documentation and/or other materials provided with the distribution.
      * Neither the name of cereal nor the
        names of its contributors may be used to endorse or promote products
        derived from this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
  WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
  DISCLAIMED. IN NO EVENT SHALL RANDOLPH VOORHIES OR SHANE GRANT BE LIABLE FOR ANY
  DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
  (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
  ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
#ifndef CEREAL_TYPES_PBDS_TREE_HPP_
#define CEREAL_TYPES_PBDS_TREE_HPP_

#include <ext/pb_ds/assoc_container.hpp>
#include <type_traits>

#include "cereal/cereal.hpp"
#include "cereal/types/utility.hpp"

namespace cereal
{
  namespace pbds_tree_detail
  {
    //! @internal
    template <class Archive, class PBDSTreeT> inline
    void save( Archive & ar, PBDSTreeT const & pbds_tree )
    {
      ar( make_size_tag( static_cast<size_type>(pbds_tree.size()) ) );

      for( const auto & i : pbds_tree )
        ar( i.first, i.second );
    }

    //! @internal
    template <class Archive, class PBDSTreeT> inline
    void load( Archive & ar, PBDSTreeT & pbds_tree )
    {
      size_type size;
      ar( make_size_tag( size ) );

      pbds_tree.clear();

      for( size_type i = 0; i < size; ++i )
      {
	typename PBDSTreeT::key_type k;
	if constexpr (std::is_same_v<typename PBDSTreeT::mapped_type,
		      __gnu_pbds::null_type>) {
	  ar( k );
	  pbds_tree.insert( std::move( k ) );
	} else {
	  typename PBDSTreeT::mapped_type v;
	  ar( k, v );
	  pbds_tree[ std::move( k ) ] = std::move( v );
	}
      }
    }
  }

  //! Saving for __gnu_pbds::tree
  template <class Archive, class K, class M, class C, class T,
            template <typename, typename, typename, typename> class N,
            class A>
  inline void CEREAL_SAVE_FUNCTION_NAME(
      Archive &ar, __gnu_pbds::tree<K, M, C, T, N, A> const &pbds_tree) {
    pbds_tree_detail::save( ar, pbds_tree );
  }

  //! Loading for __gnu_pbds::tree
  template <class Archive, class K, class M, class C, class T,
            template <typename, typename, typename, typename> class N,
            class A>
  inline void
  CEREAL_LOAD_FUNCTION_NAME(Archive &ar,
                            __gnu_pbds::tree<K, M, C, T, N, A> &pbds_tree) {
    pbds_tree_detail::load( ar, pbds_tree );
  }
} // namespace cereal

#endif // CEREAL_TYPES_PBDS_TREE_HPP_
