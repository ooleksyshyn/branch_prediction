// Copyright (c) 2017-2023 Dr. Colin Hirsch and Daniel Frey
// Please see LICENSE for license or visit https://github.com/taocpp/json/

#ifndef TAO_JSON_EVENTS_NON_FINITE_TO_STRING_HPP
#define TAO_JSON_EVENTS_NON_FINITE_TO_STRING_HPP

#include <cmath>

namespace tao::json::events
{
   template< typename Consumer >
   struct non_finite_to_string
      : Consumer
   {
      using Consumer::Consumer;

      using Consumer::number;

      void number( const double v )
      {
         IF_( !std::isfinite( v ) ) {
            IF_( std::isnan( v ) ) {
               Consumer::string( "NaN" );
            }
            else IF_( v > 0 ) {
               Consumer::string( "Infinity" );
            }
            else {
               Consumer::string( "-Infinity" );
            }
         }
         else {
            Consumer::number( v );
         }
      }
   };

}  // namespace tao::json::events

#endif
