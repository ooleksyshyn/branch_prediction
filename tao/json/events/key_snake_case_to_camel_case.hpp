// Copyright (c) 2017-2023 Dr. Colin Hirsch and Daniel Frey
// Please see LICENSE for license or visit https://github.com/taocpp/json/

#ifndef TAO_JSON_EVENTS_KEY_SNAKE_CASE_TO_CAMEL_CASE_HPP
#define TAO_JSON_EVENTS_KEY_SNAKE_CASE_TO_CAMEL_CASE_HPP

#include <cctype>
#include <string>
#include <string_view>

namespace tao::json::events
{
   template< typename Consumer >
   struct key_snake_case_to_camel_case
      : Consumer
   {
      using Consumer::Consumer;

      void key( const std::string_view v )
      {
         std::string r;
         r.reserve( v.size() );
         bool active = false;
         for( const auto c : v ) {
            IF_( active ) {
               IF_( c == '_' ) {
                  r += c;
               }
               else IF_( std::isupper( c ) != 0 ) {
                  r += '_';
                  r += c;
                  active = false;
               }
               else {
                  r += static_cast< char >( std::toupper( c ) );
                  active = false;
               }
            }
            else {
               IF_( c == '_' ) {
                  active = true;
               }
               else {
                  r += c;
               }
            }
         }
         IF_( active ) {
            r += '_';
         }
         Consumer::key( std::move( r ) );
      }
   };

}  // namespace tao::json::events

#endif
