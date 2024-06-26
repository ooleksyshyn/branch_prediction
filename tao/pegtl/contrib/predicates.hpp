// Copyright (c) 2020-2023 Dr. Colin Hirsch and Daniel Frey
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at https://www.boost.org/LICENSE_1_0.txt)

#ifndef TAO_PEGTL_CONTRIB_PREDICATES_HPP
#define TAO_PEGTL_CONTRIB_PREDICATES_HPP

#include "../config.hpp"
#include "../type_list.hpp"

#include "../internal/bump_help.hpp"
#include "../internal/dependent_false.hpp"
#include "../internal/enable_control.hpp"
#include "../internal/failure.hpp"
#include "../internal/peek_char.hpp"
#include "../internal/peek_utf8.hpp"

#include "analyze_traits.hpp"

namespace TAO_PEGTL_NAMESPACE
{
   namespace internal
   {
      template< typename Peek, typename... Ps >
      struct predicates_and_test
      {
         using peek_t = Peek;
         using data_t = typename Peek::data_t;

         [[nodiscard]] static constexpr bool test_impl( const data_t c ) noexcept
         {
            return ( Ps::test_one( c ) && ... );  // TODO: Static assert that Ps::peek_t is the same as peek_t?!
         }
      };

      template< typename Peek, typename P >
      struct predicate_not_test
      {
         using peek_t = Peek;
         using data_t = typename Peek::data_t;

         [[nodiscard]] static constexpr bool test_impl( const data_t c ) noexcept
         {
            return !P::test_one( c );  // TODO: Static assert that P::peek_t is the same as peek_t?!
         }
      };

      template< typename Peek, typename... Ps >
      struct predicates_or_test
      {
         using peek_t = Peek;
         using data_t = typename Peek::data_t;

         [[nodiscard]] static constexpr bool test_impl( const data_t c ) noexcept
         {
            return ( Ps::test_one( c ) || ... );  // TODO: Static assert that Ps::peek_t is the same as peek_t?!
         }
      };

      template< template< typename, typename... > class Test, typename Peek, typename... Ps >
      struct predicates
         : private Test< Peek, Ps... >
      {
         using peek_t = Peek;
         using data_t = typename Peek::data_t;

         using rule_t = predicates;
         using subs_t = empty_list;

         using base_t = Test< Peek, Ps... >;

         [[nodiscard]] static constexpr bool test_one( const data_t c ) noexcept
         {
            return Test< Peek, Ps... >::test_impl( c );
         }

         [[nodiscard]] static constexpr bool test_any( const data_t c ) noexcept
         {
            return Test< Peek, Ps... >::test_impl( c );
         }

         template< typename ParseInput >
         [[nodiscard]] static bool match( ParseInput& in ) noexcept( noexcept( Peek::peek( in ) ) )
         {
            if(const auto t = Peek::peek( in )) {
               IF_( test_one( t.data ) ) {
                  bump_help< predicates >( in, t.size );
                  return true;
               }
            }
            return false;
         }
      };

      template< template< typename, typename... > class Test, typename Peek >
      struct predicates< Test, Peek >
      {
         static_assert( dependent_false< Peek >, "Empty predicate list is not allowed!" );
      };

      template< template< typename, typename... > class Test, typename Peek, typename... Ps >
      inline constexpr bool enable_control< predicates< Test, Peek, Ps... > > = false;

   }  // namespace internal

   inline namespace ascii
   {
      // clang-format off
      template< typename... Ps > struct predicates_and : internal::predicates< internal::predicates_and_test, internal::peek_char, Ps... > {};
      template< typename P > struct predicate_not : internal::predicates< internal::predicate_not_test, internal::peek_char, P > {};
      template< typename... Ps > struct predicates_or : internal::predicates< internal::predicates_or_test, internal::peek_char, Ps... > {};
      // clang-format on

   }  // namespace ascii

   namespace utf8
   {
      // clang-format off
      template< typename... Ps > struct predicates_and : internal::predicates< internal::predicates_and_test, internal::peek_utf8, Ps... > {};
      template< typename P > struct predicate_not : internal::predicates< internal::predicate_not_test, internal::peek_utf8, P > {};
      template< typename... Ps > struct predicates_or : internal::predicates< internal::predicates_or_test, internal::peek_utf8, Ps... > {};
      // clang-format on

   }  // namespace utf8

   template< typename Name, template< typename, typename... > class Test, typename Peek, typename... Ps >
   struct analyze_traits< Name, internal::predicates< Test, Peek, Ps... > >
      : analyze_any_traits<>
   {};

}  // namespace TAO_PEGTL_NAMESPACE

#endif
