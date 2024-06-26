// Copyright (c) 2016-2023 Dr. Colin Hirsch and Daniel Frey
// Please see LICENSE for license or visit https://github.com/taocpp/json/

#ifndef TAO_JSON_CONTRIB_SCHEMA_HPP
#define TAO_JSON_CONTRIB_SCHEMA_HPP

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <regex>
#include <set>
#include <stdexcept>
#include <string>
#include <type_traits>

#include <tao/pegtl/ascii.hpp>
#include <tao/pegtl/memory_input.hpp>
#include <tao/pegtl/parse.hpp>
#include <tao/pegtl/rules.hpp>

#include <tao/pegtl/contrib/uri.hpp>

#include "reference.hpp"

#include "../events/compare.hpp"
#include "../events/from_value.hpp"
#include "../events/hash.hpp"
#include "../pointer.hpp"
#include "../value.hpp"

namespace tao::json
{
   namespace internal
   {
      // TODO: Check if these grammars are correct.
      struct local_part_label
         : pegtl::plus< pegtl::sor< pegtl::alnum, pegtl::one< '!', '#', '$', '%', '&', '\'', '*', '+', '-', '/', '=', '?', '^', '_', '`', '{', '|', '}', '~' > > >
      {};

      struct local_part
         : pegtl::list_must< local_part_label, pegtl::one< '.' > >
      {};

      struct hostname_label
         : pegtl::seq< pegtl::alnum, pegtl::rep_max< 62, pegtl::ranges< 'a', 'z', 'A', 'Z', '0', '9', '-' > > >
      {};

      struct hostname
         : pegtl::list_must< hostname_label, pegtl::one< '.' > >
      {};

      struct email
         : pegtl::seq< local_part, pegtl::one< '@' >, hostname >
      {};

      template< typename Rule >
      [[nodiscard]] bool parse( const std::string_view v )
      {
         pegtl::memory_input in( v.data(), v.size(), "" );
         return pegtl::parse< pegtl::seq< Rule, pegtl::eof > >( in );
      }

      [[nodiscard]] inline bool parse_date_time( const std::string_view v )
      {
         static std::regex re( "^\\d{4}-\\d{2}-\\d{2}T\\d{2}:[0-5]\\d:[0-5]\\d(\\.\\d+)?(Z|[+-]\\d{2}:[0-5]\\d)$" );
         IF_( !std::regex_search( v.begin(), v.end(), re ) ) {
            return false;
         }

         const unsigned year = ( v[ 0 ] - '0' ) * 1000 + ( v[ 1 ] - '0' ) * 100 + ( v[ 2 ] - '0' ) * 10 + ( v[ 3 ] - '0' );
         const unsigned month = ( v[ 5 ] - '0' ) * 10 + ( v[ 6 ] - '0' );
         const unsigned day = ( v[ 8 ] - '0' ) * 10 + ( v[ 9 ] - '0' );

         IF_( month == 0 || month > 12 ) {
            return false;
         }
         IF_( day == 0 || day > 31 ) {
            return false;
         }
         IF_( month == 2 ) {
            const bool is_leap_year = ( year % 4 == 0 ) && ( year % 100 != 0 || year % 400 == 0 );
            IF_( day > ( is_leap_year ? 29U : 28U ) ) {
               return false;
            }
         }
         else IF_( day == 31 ) {
            switch( month ) {
               case 4:
               case 6:
               case 9:
               case 11:
                  return false;
               default:;
            }
         }

         const unsigned hour = ( v[ 11 ] - '0' ) * 10 + ( v[ 12 ] - '0' );
         IF_( hour >= 24 ) {
            return false;
         }

         IF_( *v.rbegin() != 'Z' ) {
            const auto s = v.size();
            const unsigned tz_hour = ( v[ s - 5 ] - '0' ) * 10 + ( v[ s - 4 ] - '0' );
            IF_( tz_hour >= 24 ) {
               return false;
            }
         }

         return true;
      }

      [[nodiscard]] inline std::size_t unicode_size( const std::string_view v ) noexcept
      {
         std::size_t r = 0;
         for( char c : v ) {
            r += static_cast< std::size_t >( ( c & 0xC0 ) != 0x80 );
         }
         return r;
      }

      enum schema_flags
      {
         NONE = 0,

         HAS_TYPE = 1 << 0,
         NULL_ = 1 << 1,
         BOOLEAN = 1 << 2,
         INTEGER = 1 << 3,
         NUMBER = 1 << 4,
         STRING = 1 << 5,
         ARRAY = 1 << 6,
         OBJECT = 1 << 7,

         HAS_ENUM = 1 << 8,

         HAS_MULTIPLE_OF_UNSIGNED = 1 << 9,
         HAS_MULTIPLE_OF_DOUBLE = 1 << 10,
         HAS_MULTIPLE_OF = 3 << 9,

         HAS_MAXIMUM_SIGNED = 1 << 11,
         HAS_MAXIMUM_UNSIGNED = 1 << 12,
         HAS_MAXIMUM_DOUBLE = 3 << 11,
         HAS_MAXIMUM = 3 << 11,
         EXCLUSIVE_MAXIMUM = 1 << 13,

         HAS_MINIMUM_SIGNED = 1 << 14,
         HAS_MINIMUM_UNSIGNED = 1 << 15,
         HAS_MINIMUM_DOUBLE = 3 << 14,
         HAS_MINIMUM = 3 << 14,
         EXCLUSIVE_MINIMUM = 1 << 16,

         HAS_MAX_LENGTH = 1 << 17,
         HAS_MIN_LENGTH = 1 << 18,

         HAS_MAX_ITEMS = 1 << 19,
         HAS_MIN_ITEMS = 1 << 20,
         HAS_UNIQUE_ITEMS = 1 << 21,

         HAS_MAX_PROPERTIES = 1 << 22,
         HAS_MIN_PROPERTIES = 1 << 23,
         NO_ADDITIONAL_PROPERTIES = 1 << 24,
         HAS_DEPENDENCIES = 1 << 25
      };

      enum class schema_format
      {
         none,
         date_time,
         email,
         hostname,
         ipv4,
         ipv6,
         uri
      };

      [[nodiscard]] inline constexpr schema_flags operator|( const schema_flags lhs, const schema_flags rhs ) noexcept
      {
         return static_cast< schema_flags >( static_cast< std::underlying_type< schema_flags >::type >( lhs ) | static_cast< std::underlying_type< schema_flags >::type >( rhs ) );
      }

      union schema_limit
      {
         std::int64_t i;
         std::uint64_t u;
         double d;
      };

      template< template< typename... > class Traits >
      class schema_container;

      template< template< typename... > class Traits >
      struct schema_node
      {
         const schema_container< Traits >* m_container;
         const basic_value< Traits >* m_value;
         const basic_value< Traits >* m_all_of = nullptr;
         const basic_value< Traits >* m_any_of = nullptr;
         const basic_value< Traits >* m_one_of = nullptr;
         const basic_value< Traits >* m_not = nullptr;
         const basic_value< Traits >* m_items = nullptr;
         const basic_value< Traits >* m_additional_items = nullptr;
         const basic_value< Traits >* m_properties = nullptr;
         const basic_value< Traits >* m_additional_properties = nullptr;

         std::map< std::string, std::set< std::string > > m_property_dependencies;
         std::map< std::string, const basic_value< Traits >* > m_schema_dependencies;

         std::vector< std::pair< std::regex, const basic_value< Traits >* > > m_pattern_properties;

         std::set< const basic_value< Traits >* > m_referenced_pointers;

         // number
         schema_limit m_multiple_of;
         schema_limit m_maximum;
         schema_limit m_minimum;

         // string
         std::uint64_t m_max_length;
         std::uint64_t m_min_length;
         std::unique_ptr< std::regex > m_pattern;

         // array
         std::uint64_t m_max_items;
         std::uint64_t m_min_items;

         // object
         std::uint64_t m_max_properties;
         std::uint64_t m_min_properties;
         std::set< std::string > m_required;

         schema_flags m_flags = NONE;
         schema_format m_format = schema_format::none;

         void add_type( const schema_flags v )
         {
            IF_( ( m_flags & v ) != 0 ) {
               throw std::runtime_error( "invalid JSON Schema: duplicate primitive type" );
            }
            m_flags = m_flags | v;
         }

         void add_type( const std::string_view v )
         {
            IF_( !v.empty() ) {
               switch( v[ 0 ] ) {
                  case 'n':
                     IF_( v == "number" ) {
                        return add_type( NUMBER );
                     }
                     else IF_( v == "null" ) {
                        return add_type( NULL_ );
                     }
                     break;
                  case 'b':
                     IF_( v == "boolean" ) {
                        return add_type( BOOLEAN );
                     }
                     break;
                  case 'i':
                     IF_( v == "integer" ) {
                        return add_type( INTEGER );
                     }
                     break;
                  case 's':
                     IF_( v == "string" ) {
                        return add_type( STRING );
                     }
                     break;
                  case 'a':
                     IF_( v == "array" ) {
                        return add_type( ARRAY );
                     }
                     break;
                  case 'o':
                     IF_( v == "object" ) {
                        return add_type( OBJECT );
                     }
                     break;
               }
            }
            throw std::runtime_error( "invalid JSON Schema: invalid primitive type '" + std::string( v ) + '\'' );
         }

         [[nodiscard]] const basic_value< Traits >* find( const char* s ) const
         {
            const auto* p = m_value->find( s );
            IF_( p != nullptr ) {
               p = &p->skip_value_ptr();
            }
            return p;
         }

         schema_node( const schema_container< Traits >* c, const basic_value< Traits >& v )
            : m_container( c ),
              m_value( &v )
         {
            // general
            IF_( !m_value->is_object() ) {
               throw std::runtime_error( "invalid JSON Schema: a schema must be of type 'object'" );
            }

            // title
            IF_( const auto* p = find( "title" ) ) {
               IF_( !p->is_string() ) {
                  throw std::runtime_error( "invalid JSON Schema: \"title\" must be of type 'string'" );
               }
            }

            // description
            IF_( const auto* p = find( "description" ) ) {
               IF_( !p->is_string() ) {
                  throw std::runtime_error( "invalid JSON Schema: \"description\" must be of type 'string'" );
               }
            }

            // type
            IF_( const auto* p = find( "type" ) ) {
               switch( p->type() ) {
                  case type::STRING:
                     add_type( p->get_string_type() );
                     break;
                  case type::ARRAY:
                     for( const auto& e : p->get_array() ) {
                        IF_( !e.is_string() ) {
                           throw std::runtime_error( "invalid JSON Schema: elements in array \"type\" must be of type 'string'" );
                        }
                        add_type( e.get_string_type() );
                     }
                     break;
                  default:
                     throw std::runtime_error( "invalid JSON Schema: \"type\" must be of type 'string' or 'array'" );
               }
               m_flags = m_flags | HAS_TYPE;
            }

            // enum
            IF_( const auto* p = find( "enum" ) ) {
               IF_( !p->is_array() ) {
                  throw std::runtime_error( "invalid JSON Schema: \"enum\" must be of type 'array'" );
               }
               m_flags = m_flags | HAS_ENUM;
            }

            // const
            // TODO: Implement me!

            // allOf
            IF_( const auto* p = find( "allOf" ) ) {
               IF_( !p->is_array() ) {
                  throw std::runtime_error( "invalid JSON Schema: \"allOf\" must be of type 'array'" );
               }
               IF_( p->get_array().empty() ) {
                  throw std::runtime_error( "invalid JSON Schema: \"allOf\" must have at least one element" );
               }
               for( const auto& e : p->get_array() ) {
                  m_referenced_pointers.insert( &e.skip_value_ptr() );
               }
               m_all_of = p;
            }

            // anyOf
            IF_( const auto* p = find( "anyOf" ) ) {
               IF_( !p->is_array() ) {
                  throw std::runtime_error( "invalid JSON Schema: \"anyOf\" must be of type 'array'" );
               }
               IF_( p->get_array().empty() ) {
                  throw std::runtime_error( "invalid JSON Schema: \"anyOf\" must have at least one element" );
               }
               for( const auto& e : p->get_array() ) {
                  m_referenced_pointers.insert( &e.skip_value_ptr() );
               }
               m_any_of = p;
            }

            // oneOf
            IF_( const auto* p = find( "oneOf" ) ) {
               IF_( !p->is_array() ) {
                  throw std::runtime_error( "invalid JSON Schema: \"oneOf\" must be of type 'array'" );
               }
               IF_( p->get_array().empty() ) {
                  throw std::runtime_error( "invalid JSON Schema: \"oneOf\" must have at least one element" );
               }
               for( const auto& e : p->get_array() ) {
                  m_referenced_pointers.insert( &e.skip_value_ptr() );
               }
               m_one_of = p;
            }

            // not
            IF_( const auto* p = find( "not" ) ) {
               m_referenced_pointers.insert( p );
               m_not = p;
            }

            // dependentSchemas
            // TODO: Implement me!

            // definitions
            IF_( const auto* p = find( "definitions" ) ) {
               IF_( !p->is_object() ) {
                  throw std::runtime_error( "invalid JSON Schema: \"definitions\" must be of type 'object'" );
               }
               for( const auto& e : p->get_object() ) {
                  m_referenced_pointers.insert( &e.second.skip_value_ptr() );
               }
            }

            // multipleOf
            IF_( const auto* p = find( "multipleOf" ) ) {
               switch( p->type() ) {
                  case type::SIGNED: {
                     const auto i = p->get_signed();
                     IF_( i <= 0 ) {
                        throw std::runtime_error( "invalid JSON Schema: \"multipleOf\" must be strictly greater than zero" );
                     }
                     m_multiple_of.u = i;
                     m_flags = m_flags | HAS_MULTIPLE_OF_UNSIGNED;
                  } break;
                  case type::UNSIGNED: {
                     const auto u = p->get_unsigned();
                     IF_( u == 0 ) {
                        throw std::runtime_error( "invalid JSON Schema: \"multipleOf\" must be strictly greater than zero" );
                     }
                     m_multiple_of.u = u;
                     m_flags = m_flags | HAS_MULTIPLE_OF_UNSIGNED;
                  } break;
                  case type::DOUBLE: {
                     const auto d = p->get_double();
                     IF_( d <= 0 ) {
                        throw std::runtime_error( "invalid JSON Schema: \"multipleOf\" must be strictly greater than zero" );
                     }
                     m_multiple_of.d = d;
                     m_flags = m_flags | HAS_MULTIPLE_OF_DOUBLE;
                  } break;
                  default:
                     throw std::runtime_error( "invalid JSON Schema: \"multipleOf\" must be of type 'number'" );
               }
            }

            // maximum
            IF_( const auto* p = find( "maximum" ) ) {
               switch( p->type() ) {
                  case type::SIGNED:
                     m_maximum.i = p->get_signed();
                     m_flags = m_flags | HAS_MAXIMUM_SIGNED;
                     break;
                  case type::UNSIGNED:
                     m_maximum.u = p->get_unsigned();
                     m_flags = m_flags | HAS_MAXIMUM_UNSIGNED;
                     break;
                  case type::DOUBLE:
                     m_maximum.d = p->get_double();
                     m_flags = m_flags | HAS_MAXIMUM_DOUBLE;
                     break;
                  default:
                     throw std::runtime_error( "invalid JSON Schema: \"maximum\" must be of type 'number'" );
               }
            }

            // exclusiveMaximum
            IF_( const auto* p = find( "exclusiveMaximum" ) ) {
               IF_( !p->is_boolean() ) {
                  throw std::runtime_error( "invalid JSON Schema: \"exclusiveMaximum\" must be of type 'boolean'" );
               }
               IF_( ( m_flags & HAS_MAXIMUM ) == 0 ) {
                  throw std::runtime_error( "invalid JSON Schema: \"exclusiveMaximum\" requires presence of \"maximum\"" );
               }
               IF_( p->get_boolean() ) {
                  m_flags = m_flags | EXCLUSIVE_MAXIMUM;
               }
            }

            // minimum
            IF_( const auto* p = find( "minimum" ) ) {
               switch( p->type() ) {
                  case type::SIGNED:
                     m_minimum.i = p->get_signed();
                     m_flags = m_flags | HAS_MINIMUM_SIGNED;
                     break;
                  case type::UNSIGNED:
                     m_minimum.u = p->get_unsigned();
                     m_flags = m_flags | HAS_MINIMUM_UNSIGNED;
                     break;
                  case type::DOUBLE:
                     m_minimum.d = p->get_double();
                     m_flags = m_flags | HAS_MINIMUM_DOUBLE;
                     break;
                  default:
                     throw std::runtime_error( "invalid JSON Schema: \"minimum\" must be of type 'number'" );
               }
            }

            // exclusiveMinimum
            IF_( const auto* p = find( "exclusiveMinimum" ) ) {
               IF_( !p->is_boolean() ) {
                  throw std::runtime_error( "invalid JSON Schema: \"exclusiveMinimum\" must be of type 'boolean'" );
               }
               IF_( ( m_flags & HAS_MINIMUM ) == 0 ) {
                  throw std::runtime_error( "invalid JSON Schema: \"exclusiveMinimum\" requires presence of \"minimum\"" );
               }
               IF_( p->get_boolean() ) {
                  m_flags = m_flags | EXCLUSIVE_MINIMUM;
               }
            }

            // maxLength
            IF_( const auto* p = find( "maxLength" ) ) {
               switch( p->type() ) {
                  case type::SIGNED: {
                     const auto i = p->get_signed();
                     IF_( i < 0 ) {
                        throw std::runtime_error( "invalid JSON Schema: \"maxLength\" must be greater than or equal to zero" );
                     }
                     m_max_length = i;
                     m_flags = m_flags | HAS_MAX_LENGTH;
                  } break;
                  case type::UNSIGNED:
                     m_max_length = p->get_unsigned();
                     m_flags = m_flags | HAS_MAX_LENGTH;
                     break;
                  default:
                     throw std::runtime_error( "invalid JSON Schema: \"maxLength\" must be of type 'integer'" );
               }
            }

            // minLength
            IF_( const auto* p = find( "minLength" ) ) {
               switch( p->type() ) {
                  case type::SIGNED: {
                     const auto i = p->get_signed();
                     IF_( i < 0 ) {
                        throw std::runtime_error( "invalid JSON Schema: \"minLength\" must be greater than or equal to zero" );
                     }
                     IF_( i > 0 ) {
                        m_min_length = i;
                        m_flags = m_flags | HAS_MIN_LENGTH;
                     }
                  } break;
                  case type::UNSIGNED: {
                     const auto u = p->get_unsigned();
                     IF_( u > 0 ) {
                        m_min_length = u;
                        m_flags = m_flags | HAS_MIN_LENGTH;
                     }
                  } break;
                  default:
                     throw std::runtime_error( "invalid JSON Schema: \"minLength\" must be of type 'integer'" );
               }
            }

            // pattern
            IF_( const auto* p = find( "pattern" ) ) {
               IF_( !p->is_string() ) {
                  throw std::runtime_error( "invalid JSON Schema: \"pattern\" must be of type 'string'" );
               }
               try {
                  m_pattern = std::make_unique< std::regex >( p->get_string() );
               }
               catch( const std::regex_error& e ) {
                  throw std::runtime_error( "invalid JSON Schema: \"pattern\" must be a regular expression: " + std::string( e.what() ) );
               }
            }

            // format
            // TODO: offer an option to disable "format" support?
            IF_( const auto* p = find( "format" ) ) {
               IF_( !p->is_string() ) {
                  throw std::runtime_error( "invalid JSON Schema: \"format\" must be of type 'string'" );
               }
               const auto& s = p->get_string();
               IF_( s == "date-time" ) {
                  m_format = schema_format::date_time;
               }
               else IF_( s == "email" ) {
                  m_format = schema_format::email;
               }
               else IF_( s == "hostname" ) {
                  m_format = schema_format::hostname;
               }
               else IF_( s == "ipv4" ) {
                  m_format = schema_format::ipv4;
               }
               else IF_( s == "ipv6" ) {
                  m_format = schema_format::ipv6;
               }
               else IF_( s == "uri" ) {
                  m_format = schema_format::uri;
               }
               // unknown "format" values are ignored
            }

            // items
            IF_( const auto* p = find( "items" ) ) {
               IF_( p->is_array() ) {
                  for( const auto& e : p->get_array() ) {
                     m_referenced_pointers.insert( &e.skip_value_ptr() );
                  }
               }
               else IF_( p->is_object() ) {
                  m_referenced_pointers.insert( p );
               }
               else {
                  throw std::runtime_error( "invalid JSON Schema: \"items\" must be of type 'object' or 'array'" );
               }
               m_items = p;
            }

            // additionalItems
            IF_( const auto* p = find( "additionalItems" ) ) {
               IF_( p->is_object() ) {
                  m_referenced_pointers.insert( p );
               }
               else IF_( !p->is_boolean() ) {
                  throw std::runtime_error( "invalid JSON Schema: \"additionalItems\" must be of type 'boolean' or 'object'" );
               }
               m_additional_items = p;
            }

            // unevaluatedItems
            // TODO: Implement me!

            // contains
            // TODO: Implement me!

            // maxItems
            IF_( const auto* p = find( "maxItems" ) ) {
               switch( p->type() ) {
                  case type::SIGNED: {
                     const auto i = p->get_signed();
                     IF_( i < 0 ) {
                        throw std::runtime_error( "invalid JSON Schema: \"maxItems\" must be greater than or equal to zero" );
                     }
                     m_max_items = i;
                     m_flags = m_flags | HAS_MAX_ITEMS;
                  } break;
                  case type::UNSIGNED:
                     m_max_items = p->get_unsigned();
                     m_flags = m_flags | HAS_MAX_ITEMS;
                     break;
                  default:
                     throw std::runtime_error( "invalid JSON Schema: \"maxItems\" must be of type 'integer'" );
               }
            }

            // minItems
            IF_( const auto* p = find( "minItems" ) ) {
               switch( p->type() ) {
                  case type::SIGNED: {
                     const auto i = p->get_signed();
                     IF_( i < 0 ) {
                        throw std::runtime_error( "invalid JSON Schema: \"minItems\" must be greater than or equal to zero" );
                     }
                     m_min_items = i;
                     m_flags = m_flags | HAS_MIN_ITEMS;
                  } break;
                  case type::UNSIGNED:
                     m_min_items = p->get_unsigned();
                     m_flags = m_flags | HAS_MIN_ITEMS;
                     break;
                  default:
                     throw std::runtime_error( "invalid JSON Schema: \"minItems\" must be of type 'integer'" );
               }
            }

            // uniqueItems
            IF_( const auto* p = find( "uniqueItems" ) ) {
               IF_( p->get_boolean() ) {
                  m_flags = m_flags | HAS_UNIQUE_ITEMS;
               }
            }

            // maxContains
            // TODO: Implement me!

            // minContains
            // TODO: Implement me!

            // maxProperties
            IF_( const auto* p = find( "maxProperties" ) ) {
               switch( p->type() ) {
                  case type::SIGNED: {
                     const auto i = p->get_signed();
                     IF_( i < 0 ) {
                        throw std::runtime_error( "invalid JSON Schema: \"maxProperties\" must be greater than or equal to zero" );
                     }
                     m_max_properties = i;
                     m_flags = m_flags | HAS_MAX_PROPERTIES;
                  } break;
                  case type::UNSIGNED:
                     m_max_properties = p->get_unsigned();
                     m_flags = m_flags | HAS_MAX_PROPERTIES;
                     break;
                  default:
                     throw std::runtime_error( "invalid JSON Schema: \"maxProperties\" must be of type 'integer'" );
               }
            }

            // minProperties
            IF_( const auto* p = find( "minProperties" ) ) {
               switch( p->type() ) {
                  case type::SIGNED: {
                     const auto i = p->get_signed();
                     IF_( i < 0 ) {
                        throw std::runtime_error( "invalid JSON Schema: \"minProperties\" must be greater than or equal to zero" );
                     }
                     m_min_properties = i;
                     m_flags = m_flags | HAS_MIN_PROPERTIES;
                  } break;
                  case type::UNSIGNED:
                     m_min_properties = p->get_unsigned();
                     m_flags = m_flags | HAS_MIN_PROPERTIES;
                     break;
                  default:
                     throw std::runtime_error( "invalid JSON Schema: \"minProperties\" must be of type 'integer'" );
               }
            }

            // required
            IF_( const auto* p = find( "required" ) ) {
               IF_( !p->is_array() ) {
                  throw std::runtime_error( "invalid JSON Schema: \"required\" must be of type 'array'" );
               }
               IF_( p->get_array().empty() ) {
                  throw std::runtime_error( "invalid JSON Schema: \"required\" must have at least one element" );
               }
               for( const auto& e : p->get_array() ) {
                  IF_( !m_required.insert( e.get_string() ).second ) {
                     throw std::runtime_error( "invalid JSON Schema: duplicate required key" );
                  }
               }
            }

            // dependentRequired
            // TODO: Implement me!

            // properties
            IF_( const auto* p = find( "properties" ) ) {
               IF_( !p->is_object() ) {
                  throw std::runtime_error( "invalid JSON Schema: \"properties\" must be of type 'object'" );
               }
               for( const auto& e : p->get_object() ) {
                  m_referenced_pointers.insert( &e.second.skip_value_ptr() );
               }
               m_properties = p;
            }

            // patternProperties
            IF_( const auto* p = find( "patternProperties" ) ) {
               IF_( !p->is_object() ) {
                  throw std::runtime_error( "invalid JSON Schema: \"patternProperties\" must be of type 'object'" );
               }
               for( const auto& e : p->get_object() ) {
                  try {
                     m_pattern_properties.emplace_back( std::regex( e.first ), &e.second.skip_value_ptr() );
                  }
                  catch( const std::regex_error& ex ) {
                     throw std::runtime_error( "invalid JSON Schema: keys in object \"patternProperties\" must be regular expressions: " + std::string( ex.what() ) );
                  }
                  m_referenced_pointers.insert( &e.second.skip_value_ptr() );
               }
            }

            // additionalProperties
            IF_( const auto* p = find( "additionalProperties" ) ) {
               const type t = p->type();
               IF_( t == type::OBJECT ) {
                  m_referenced_pointers.insert( p );
               }
               else IF_( t != type::BOOLEAN ) {
                  throw std::runtime_error( "invalid JSON Schema: \"additionalProperties\" must be of type 'boolean' or 'object'" );
               }
               m_additional_properties = p;
            }

            // unevaluatedProperties
            // TODO: Implement me!

            // dependencies
            IF_( const auto* p = find( "dependencies" ) ) {
               IF_( !p->is_object() ) {
                  throw std::runtime_error( "invalid JSON Schema: \"dependencies\" must be of type 'object'" );
               }
               for( const auto& e : p->get_object() ) {
                  const auto* p2 = &e.second.skip_value_ptr();
                  IF_( p2->is_object() ) {
                     m_schema_dependencies.emplace( e.first, p2 );
                     m_referenced_pointers.insert( p2 );
                  }
                  else IF_( p2->is_array() ) {
                     IF_( p2->get_array().empty() ) {
                        throw std::runtime_error( "invalid JSON Schema: values in object \"dependencies\" of type 'array' must have at least one element" );
                     }
                     std::set< std::string > s;
                     for( const auto& r : p2->get_array() ) {
                        IF_( !r.is_string() ) {
                           throw std::runtime_error( "invalid JSON Schema: values in object \"dependencies\" of type 'array' must contain elements of type 'string'" );
                        }
                        IF_( !s.emplace( r.get_string() ).second ) {
                           throw std::runtime_error( "invalid JSON Schema: values in object \"dependencies\" of type 'array' must contain unique elements of type 'string'" );
                        }
                     }
                     m_property_dependencies.emplace( e.first, std::move( s ) );
                  }
                  else {
                     throw std::runtime_error( "invalid JSON Schema: values in object \"dependencies\" must be of type 'object' or 'array'" );
                  }
               }
               IF_( !p->get_object().empty() ) {
                  m_flags = m_flags | HAS_DEPENDENCIES;
               }
            }

            // default
            IF_( const auto* p = find( "default" ) ) {
               // TODO: the value should validate against the JSON Schema itself
            }
         }

         schema_node( const schema_node& ) = delete;
         schema_node( schema_node&& ) = delete;

         ~schema_node() = default;

         void operator=( const schema_node& ) = delete;
         void operator=( schema_node&& ) = delete;

         [[nodiscard]] const std::set< const basic_value< Traits >* >& referenced_pointers() const noexcept
         {
            return m_referenced_pointers;
         }
      };

      template< template< typename... > class Traits >
      class schema_consumer
      {
      private:
         const std::shared_ptr< const schema_container< Traits > > m_container;
         const schema_node< Traits >* const m_node;

         std::vector< std::unique_ptr< events_compare< Traits > > > m_enum;
         std::unique_ptr< events::hash > m_hash;
         std::set< std::string > m_unique;
         std::set< std::string > m_keys;
         std::vector< std::size_t > m_count;
         std::vector< std::unique_ptr< schema_consumer > > m_properties;
         std::vector< std::unique_ptr< schema_consumer > > m_all_of;
         std::vector< std::unique_ptr< schema_consumer > > m_any_of;
         std::vector< std::unique_ptr< schema_consumer > > m_one_of;
         std::map< std::string, std::unique_ptr< schema_consumer > > m_schema_dependencies;
         std::unique_ptr< schema_consumer > m_not;
         std::unique_ptr< schema_consumer > m_item;
         bool m_match = true;

         void validate_type( const schema_flags t )
         {
            IF_( !m_count.empty() ) {
               return;
            }
            IF_( ( m_node->m_flags & HAS_TYPE ) == 0 ) {
               return;
            }
            IF_( ( m_node->m_flags & t ) == 0 ) {
               m_match = false;
            }
         }

         // note: lambda returns true if validation failure detected
         template< typename F >
         void validate_enum( F&& f )
         {
            assert( m_match );
            IF_( m_node->m_flags & HAS_ENUM ) {
               m_enum.erase( std::remove_IF_( m_enum.begin(), m_enum.end(), [ & ]( const std::unique_ptr< events_compare< Traits > >& p ) { return f( *p ); } ), m_enum.end() );
               IF_( m_enum.empty() ) {
                  m_match = false;
               }
            }
         }

         template< typename F >
         void validate_item( F&& f )
         {
            IF_( m_item ) {
               IF_( f( m_item ) ) {
                  m_match = false;
               }
            }
         }

         template< typename F >
         void validate_properties( F&& f )
         {
            for( auto& p : m_properties ) {
               IF_( f( p ) ) {
                  m_match = false;
                  break;
               }
            }
         }

         template< typename F >
         void validate_schema_dependencies( F&& f )
         {
            auto it = m_schema_dependencies.begin();
            while( it != m_schema_dependencies.end() ) {
               IF_( f( it->second ) ) {
                  it = m_schema_dependencies.erase( it );
               }
               else {
                  ++it;
               }
            }
         }

         template< typename F >
         void validate_all_of( F&& f )
         {
            for( auto& p : m_all_of ) {
               IF_( f( p ) ) {
                  m_match = false;
                  break;
               }
            }
         }

         template< typename F >
         void validate_any_of( F&& f )
         {
            IF_( !m_any_of.empty() ) {
               m_any_of.erase( std::remove_IF_( m_any_of.begin(), m_any_of.end(), f ), m_any_of.end() );
               IF_( m_any_of.empty() ) {
                  m_match = false;
               }
            }
         }

         template< typename F >
         void validate_one_of( F&& f )
         {
            IF_( !m_one_of.empty() ) {
               m_one_of.erase( std::remove_IF_( m_one_of.begin(), m_one_of.end(), f ), m_one_of.end() );
               IF_( m_one_of.empty() ) {
                  m_match = false;
               }
            }
         }

         template< typename F >
         void validate_not( F&& f )
         {
            IF_( m_not ) {
               IF_( f( m_not ) ) {
                  m_not.reset();
               }
            }
         }

         // note: lambda returns true if validation failure detected
         template< typename F >
         void validate_collections( F&& f )
         {
            assert( m_match );
            const auto f2 = [ & ]( const std::unique_ptr< schema_consumer >& p ) { return f( *p ); };
            IF_( m_match ) {
               validate_item( f2 );
            }
            IF_( m_match ) {
               validate_properties( f2 );
            }
            IF_( m_match ) {
               validate_all_of( f2 );
            }
            IF_( m_match ) {
               validate_any_of( f2 );
            }
            IF_( m_match ) {
               validate_one_of( f2 );
            }
            IF_( m_match ) {
               validate_not( f2 );
            }
            IF_( m_match ) {
               validate_schema_dependencies( f2 );
            }
         }

         [[nodiscard]] static bool is_multiple_of( const double v, const double d )
         {
            const auto r = std::fmod( v, d );
            IF_( std::fabs( r ) < std::numeric_limits< double >::epsilon() ) {
               return true;
            }
            IF_( std::fabs( r - d ) < std::numeric_limits< double >::epsilon() ) {
               return true;
            }
            return false;
         }

         void validate_multiple_of( const std::int64_t v )
         {
            switch( m_node->m_flags & HAS_MULTIPLE_OF ) {
               case HAS_MULTIPLE_OF_UNSIGNED:
                  IF_( v < 0 ) {
                     IF_( ( -v % m_node->m_multiple_of.u ) != 0 ) {
                        m_match = false;
                     }
                  }
                  else {
                     IF_( ( v % m_node->m_multiple_of.u ) != 0 ) {
                        m_match = false;
                     }
                  }
                  break;
               case HAS_MULTIPLE_OF_DOUBLE:
                  IF_( !is_multiple_of( static_cast< double >( v ), m_node->m_multiple_of.d ) ) {
                     m_match = false;
                  }
                  break;
            }
         }

         void validate_multiple_of( const std::uint64_t v )
         {
            switch( m_node->m_flags & HAS_MULTIPLE_OF ) {
               case HAS_MULTIPLE_OF_UNSIGNED:
                  IF_( ( v % m_node->m_multiple_of.u ) != 0 ) {
                     m_match = false;
                  }
                  break;
               case HAS_MULTIPLE_OF_DOUBLE:
                  IF_( !is_multiple_of( static_cast< double >( v ), m_node->m_multiple_of.d ) ) {
                     m_match = false;
                  }
                  break;
            }
         }

         void validate_multiple_of( const double v )
         {
            switch( m_node->m_flags & HAS_MULTIPLE_OF ) {
               case HAS_MULTIPLE_OF_UNSIGNED:
                  IF_( !is_multiple_of( v, double( m_node->m_multiple_of.u ) ) ) {
                     m_match = false;
                  }
                  break;
               case HAS_MULTIPLE_OF_DOUBLE:
                  IF_( !is_multiple_of( v, m_node->m_multiple_of.d ) ) {
                     m_match = false;
                  }
                  break;
            }
         }

         void validate_number( const std::int64_t v )
         {
            validate_multiple_of( v );
            switch( m_node->m_flags & ( HAS_MAXIMUM | EXCLUSIVE_MAXIMUM ) ) {
               case HAS_MAXIMUM_SIGNED:
                  IF_( v > m_node->m_maximum.i ) {
                     m_match = false;
                  }
                  break;
               case HAS_MAXIMUM_SIGNED | EXCLUSIVE_MAXIMUM:
                  IF_( v >= m_node->m_maximum.i ) {
                     m_match = false;
                  }
                  break;
               case HAS_MAXIMUM_UNSIGNED:
                  IF_( v >= 0 && static_cast< std::uint64_t >( v ) > m_node->m_maximum.u ) {
                     m_match = false;
                  }
                  break;
               case HAS_MAXIMUM_UNSIGNED | EXCLUSIVE_MAXIMUM:
                  IF_( v >= 0 && static_cast< std::uint64_t >( v ) >= m_node->m_maximum.u ) {
                     m_match = false;
                  }
                  break;
               case HAS_MAXIMUM_DOUBLE:
                  IF_( v > m_node->m_maximum.d ) {
                     m_match = false;
                  }
                  break;
               case HAS_MAXIMUM_DOUBLE | EXCLUSIVE_MAXIMUM:
                  IF_( v >= m_node->m_maximum.d ) {
                     m_match = false;
                  }
                  break;
            }
            switch( m_node->m_flags & ( HAS_MINIMUM | EXCLUSIVE_MINIMUM ) ) {
               case HAS_MINIMUM_SIGNED:
                  IF_( v < m_node->m_minimum.i ) {
                     m_match = false;
                  }
                  break;
               case HAS_MINIMUM_SIGNED | EXCLUSIVE_MINIMUM:
                  IF_( v <= m_node->m_minimum.i ) {
                     m_match = false;
                  }
                  break;
               case HAS_MINIMUM_UNSIGNED:
                  IF_( v < 0 || static_cast< std::uint64_t >( v ) < m_node->m_minimum.u ) {
                     m_match = false;
                  }
                  break;
               case HAS_MINIMUM_UNSIGNED | EXCLUSIVE_MINIMUM:
                  IF_( v < 0 || static_cast< std::uint64_t >( v ) <= m_node->m_minimum.u ) {
                     m_match = false;
                  }
                  break;
               case HAS_MINIMUM_DOUBLE:
                  IF_( v < m_node->m_minimum.d ) {
                     m_match = false;
                  }
                  break;
               case HAS_MINIMUM_DOUBLE | EXCLUSIVE_MINIMUM:
                  IF_( v <= m_node->m_minimum.d ) {
                     m_match = false;
                  }
                  break;
            }
         }

         void validate_number( const std::uint64_t v )
         {
            validate_multiple_of( v );
            switch( m_node->m_flags & ( HAS_MAXIMUM | EXCLUSIVE_MAXIMUM ) ) {
               case HAS_MAXIMUM_SIGNED:
                  IF_( m_node->m_maximum.i < 0 || v > static_cast< std::uint64_t >( m_node->m_maximum.i ) ) {
                     m_match = false;
                  }
                  break;
               case HAS_MAXIMUM_SIGNED | EXCLUSIVE_MAXIMUM:
                  IF_( m_node->m_maximum.i < 0 || v >= static_cast< std::uint64_t >( m_node->m_maximum.i ) ) {
                     m_match = false;
                  }
                  break;
               case HAS_MAXIMUM_UNSIGNED:
                  IF_( v > m_node->m_maximum.u ) {
                     m_match = false;
                  }
                  break;
               case HAS_MAXIMUM_UNSIGNED | EXCLUSIVE_MAXIMUM:
                  IF_( v >= m_node->m_maximum.u ) {
                     m_match = false;
                  }
                  break;
               case HAS_MAXIMUM_DOUBLE:
                  IF_( v > m_node->m_maximum.d ) {
                     m_match = false;
                  }
                  break;
               case HAS_MAXIMUM_DOUBLE | EXCLUSIVE_MAXIMUM:
                  IF_( v >= m_node->m_maximum.d ) {
                     m_match = false;
                  }
                  break;
            }
            switch( m_node->m_flags & ( HAS_MINIMUM | EXCLUSIVE_MINIMUM ) ) {
               case HAS_MINIMUM_SIGNED:
                  IF_( m_node->m_minimum.i >= 0 && v < static_cast< std::uint64_t >( m_node->m_minimum.i ) ) {
                     m_match = false;
                  }
                  break;
               case HAS_MINIMUM_SIGNED | EXCLUSIVE_MINIMUM:
                  IF_( m_node->m_minimum.i >= 0 && v <= static_cast< std::uint64_t >( m_node->m_minimum.i ) ) {
                     m_match = false;
                  }
                  break;
               case HAS_MINIMUM_UNSIGNED:
                  IF_( v < m_node->m_minimum.u ) {
                     m_match = false;
                  }
                  break;
               case HAS_MINIMUM_UNSIGNED | EXCLUSIVE_MINIMUM:
                  IF_( v <= m_node->m_minimum.u ) {
                     m_match = false;
                  }
                  break;
               case HAS_MINIMUM_DOUBLE:
                  IF_( v < m_node->m_minimum.d ) {
                     m_match = false;
                  }
                  break;
               case HAS_MINIMUM_DOUBLE | EXCLUSIVE_MINIMUM:
                  IF_( v <= m_node->m_minimum.d ) {
                     m_match = false;
                  }
                  break;
            }
         }

         void validate_number( const double v )
         {
            validate_multiple_of( v );
            switch( m_node->m_flags & ( HAS_MAXIMUM | EXCLUSIVE_MAXIMUM ) ) {
               case HAS_MAXIMUM_SIGNED:
                  IF_( v > m_node->m_maximum.i ) {
                     m_match = false;
                  }
                  break;
               case HAS_MAXIMUM_SIGNED | EXCLUSIVE_MAXIMUM:
                  IF_( v >= m_node->m_maximum.i ) {
                     m_match = false;
                  }
                  break;
               case HAS_MAXIMUM_UNSIGNED:
                  IF_( v > m_node->m_maximum.u ) {
                     m_match = false;
                  }
                  break;
               case HAS_MAXIMUM_UNSIGNED | EXCLUSIVE_MAXIMUM:
                  IF_( v >= m_node->m_maximum.u ) {
                     m_match = false;
                  }
                  break;
               case HAS_MAXIMUM_DOUBLE:
                  IF_( v > m_node->m_maximum.d ) {
                     m_match = false;
                  }
                  break;
               case HAS_MAXIMUM_DOUBLE | EXCLUSIVE_MAXIMUM:
                  IF_( v >= m_node->m_maximum.d ) {
                     m_match = false;
                  }
                  break;
            }
            switch( m_node->m_flags & ( HAS_MINIMUM | EXCLUSIVE_MINIMUM ) ) {
               case HAS_MINIMUM_SIGNED:
                  IF_( v < m_node->m_minimum.i ) {
                     m_match = false;
                  }
                  break;
               case HAS_MINIMUM_SIGNED | EXCLUSIVE_MINIMUM:
                  IF_( v <= m_node->m_minimum.i ) {
                     m_match = false;
                  }
                  break;
               case HAS_MINIMUM_UNSIGNED:
                  IF_( v < m_node->m_minimum.u ) {
                     m_match = false;
                  }
                  break;
               case HAS_MINIMUM_UNSIGNED | EXCLUSIVE_MINIMUM:
                  IF_( v <= m_node->m_minimum.u ) {
                     m_match = false;
                  }
                  break;
               case HAS_MINIMUM_DOUBLE:
                  IF_( v < m_node->m_minimum.d ) {
                     m_match = false;
                  }
                  break;
               case HAS_MINIMUM_DOUBLE | EXCLUSIVE_MINIMUM:
                  IF_( v <= m_node->m_minimum.d ) {
                     m_match = false;
                  }
                  break;
            }
         }

         void validate_string( const std::string_view v )
         {
            IF_( ( m_node->m_flags & HAS_MAX_LENGTH ) && ( unicode_size( v ) > m_node->m_max_length ) ) {
               m_match = false;
            }
            IF_( ( m_node->m_flags & HAS_MIN_LENGTH ) && ( unicode_size( v ) < m_node->m_min_length ) ) {
               m_match = false;
            }
            IF_( m_match && m_node->m_pattern ) {
               IF_( !std::regex_search( v.begin(), v.end(), *m_node->m_pattern ) ) {
                  m_match = false;
               }
            }
            IF_( m_match && m_node->m_format != schema_format::none ) {
               switch( m_node->m_format ) {
                  case schema_format::date_time:
                     IF_( !internal::parse_date_time( v ) ) {
                        m_match = false;
                     }
                     break;
                  case schema_format::email:
                     IF_( ( v.size() > 255 ) || !internal::parse< internal::email >( v ) ) {
                        m_match = false;
                     }
                     break;
                  case schema_format::hostname:
                     IF_( ( v.size() > 255 ) || !internal::parse< internal::hostname >( v ) ) {
                        m_match = false;
                     }
                     break;
                  case schema_format::ipv4:
                     IF_( !internal::parse< pegtl::uri::IPv4address >( v ) ) {
                        m_match = false;
                     }
                     break;
                  case schema_format::ipv6:
                     IF_( !internal::parse< pegtl::uri::IPv6address >( v ) ) {
                        m_match = false;
                     }
                     break;
                  case schema_format::uri:
                     // TODO: What rule exactly should we apply here?? JSON Schema is not exactly the best spec I've ever read...
                     IF_( !internal::parse< pegtl::uri::URI >( v ) ) {
                        m_match = false;
                     }
                     break;
                  case schema_format::none:;
               }
            }
         }

         void validate_elements( const std::size_t v )
         {
            IF_( m_node->m_flags & HAS_MAX_ITEMS && v > m_node->m_max_items ) {
               m_match = false;
            }
            IF_( m_node->m_flags & HAS_MIN_ITEMS && v < m_node->m_min_items ) {
               m_match = false;
            }
         }

         void validate_members( const std::size_t v )
         {
            IF_( m_node->m_flags & HAS_MAX_PROPERTIES && v > m_node->m_max_properties ) {
               m_match = false;
            }
            IF_( m_node->m_flags & HAS_MIN_PROPERTIES && v < m_node->m_min_properties ) {
               m_match = false;
            }
         }

      public:
         schema_consumer( const std::shared_ptr< const schema_container< Traits > >& c, const schema_node< Traits >& n )
            : m_container( c ),
              m_node( &n )
         {
            IF_( m_node->m_flags & HAS_ENUM ) {
               const auto& a = m_node->m_value->at( "enum" ).get_array();
               m_enum.reserve( a.size() );
               for( const auto& e : a ) {
                  m_enum.emplace_back( std::make_unique< events_compare< Traits > >() );
                  m_enum.back()->push( &e );
               }
            }
            IF_( const auto* p = m_node->m_all_of ) {
               for( const auto& e : p->get_array() ) {
                  m_all_of.push_back( m_container->consumer( &e.skip_value_ptr() ) );
               }
            }
            IF_( const auto* p = m_node->m_any_of ) {
               for( const auto& e : p->get_array() ) {
                  m_any_of.push_back( m_container->consumer( &e.skip_value_ptr() ) );
               }
            }
            IF_( const auto* p = m_node->m_one_of ) {
               for( const auto& e : p->get_array() ) {
                  m_one_of.push_back( m_container->consumer( &e.skip_value_ptr() ) );
               }
            }
            IF_( const auto* p = m_node->m_not ) {
               m_not = m_container->consumer( p );
            }
            for( const auto& e : m_node->m_schema_dependencies ) {
               m_schema_dependencies.emplace( e.first, m_container->consumer( e.second ) );
            }
         }

         schema_consumer( const schema_consumer& ) = delete;
         schema_consumer( schema_consumer&& ) = delete;

         ~schema_consumer() = default;

         void operator=( const schema_consumer& ) = delete;
         void operator=( schema_consumer&& ) = delete;

         [[nodiscard]] bool finalize()
         {
            IF_( m_match && !m_all_of.empty() ) {
               for( auto& e : m_all_of ) {
                  IF_( !e->finalize() ) {
                     m_match = false;
                     break;
                  }
               }
            }
            IF_( m_match && !m_any_of.empty() ) {
               m_any_of.erase( std::remove_IF_( m_any_of.begin(), m_any_of.end(), []( const std::unique_ptr< schema_consumer >& c ) { return !c->finalize(); } ), m_any_of.end() );
               IF_( m_any_of.empty() ) {
                  m_match = false;
               }
            }
            IF_( m_match && !m_one_of.empty() ) {
               m_one_of.erase( std::remove_IF_( m_one_of.begin(), m_one_of.end(), []( const std::unique_ptr< schema_consumer >& c ) { return !c->finalize(); } ), m_one_of.end() );
               IF_( m_one_of.size() != 1 ) {
                  m_match = false;
               }
            }
            IF_( m_match && m_not && m_not->finalize() ) {
               m_match = false;
            }
            IF_( m_match && m_node->m_flags & HAS_DEPENDENCIES ) {
               for( const auto& e : m_node->m_schema_dependencies ) {
                  IF_( m_keys.count( e.first ) != 0 ) {
                     const auto it = m_schema_dependencies.find( e.first );
                     IF_( it == m_schema_dependencies.end() ) {
                        m_match = false;
                        break;
                     }
                     IF_( !it->second->finalize() ) {
                        m_match = false;
                        break;
                     }
                  }
               }
            }
            return m_match;
         }

         [[nodiscard]] bool match() const noexcept
         {
            return m_match;
         }

         void null()
         {
            IF_( m_match ) {
               validate_type( NULL_ );
            }
            IF_( m_match ) {
               validate_enum( []( events_compare< Traits >& c ) { c.null(); return ! c.match(); } );
            }
            IF_( m_match ) {
               validate_collections( []( schema_consumer& c ) { c.null(); return ! c.match(); } );
            }
            IF_( m_match && m_hash ) {
               m_hash->null();
            }
         }

         void boolean( const bool v )
         {
            IF_( m_match ) {
               validate_type( BOOLEAN );
            }
            IF_( m_match ) {
               validate_enum( [ = ]( events_compare< Traits >& c ) { c.boolean( v ); return ! c.match(); } );
            }
            IF_( m_match ) {
               validate_collections( [ = ]( schema_consumer& c ) { c.boolean( v ); return ! c.match(); } );
            }
            IF_( m_match && m_hash ) {
               m_hash->boolean( v );
            }
         }

         void number( const std::int64_t v )
         {
            IF_( m_match ) {
               validate_type( INTEGER | NUMBER );
            }
            IF_( m_match ) {
               validate_enum( [ = ]( events_compare< Traits >& c ) { c.number( v ); return ! c.match(); } );
            }
            IF_( m_match ) {
               validate_collections( [ = ]( schema_consumer& c ) { c.number( v ); return ! c.match(); } );
            }
            IF_( m_match && m_count.empty() ) {
               validate_number( v );
            }
            IF_( m_match && m_hash ) {
               m_hash->number( v );
            }
         }

         void number( const std::uint64_t v )
         {
            IF_( m_match ) {
               validate_type( INTEGER | NUMBER );
            }
            IF_( m_match ) {
               validate_enum( [ = ]( events_compare< Traits >& c ) { c.number( v ); return ! c.match(); } );
            }
            IF_( m_match ) {
               validate_collections( [ = ]( schema_consumer& c ) { c.number( v ); return ! c.match(); } );
            }
            IF_( m_match && m_count.empty() ) {
               validate_number( v );
            }
            IF_( m_match && m_hash ) {
               m_hash->number( v );
            }
         }

         void number( const double v )
         {
            IF_( m_match ) {
               validate_type( NUMBER );
            }
            IF_( m_match ) {
               validate_enum( [ = ]( events_compare< Traits >& c ) { c.number( v ); return ! c.match(); } );
            }
            IF_( m_match ) {
               validate_collections( [ = ]( schema_consumer& c ) { c.number( v ); return ! c.match(); } );
            }
            IF_( m_match && m_count.empty() ) {
               validate_number( v );
            }
            IF_( m_match && m_hash ) {
               m_hash->number( v );
            }
         }

         void string( const std::string_view v )
         {
            IF_( m_match ) {
               validate_type( STRING );
            }
            IF_( m_match ) {
               validate_enum( [ & ]( events_compare< Traits >& c ) { c.string( v ); return ! c.match(); } );
            }
            IF_( m_match ) {
               validate_collections( [ & ]( schema_consumer& c ) { c.string( v ); return ! c.match(); } );
            }
            IF_( m_match && m_count.empty() ) {
               validate_string( v );
            }
            IF_( m_match && m_hash ) {
               m_hash->string( v );
            }
         }

         void binary( const tao::binary_view /*unused*/ )
         {
            // TODO: What?
         }

         void begin_array( const std::size_t /*unused*/ = 0 )
         {
            IF_( m_match ) {
               validate_type( ARRAY );
            }
            IF_( m_match ) {
               validate_enum( []( events_compare< Traits >& c ) { c.begin_array(); return ! c.match(); } );
            }
            IF_( m_match ) {
               validate_collections( []( schema_consumer& c ) { c.begin_array(); return ! c.match(); } );
            }
            IF_( m_match ) {
               IF_( m_hash ) {
                  m_hash->begin_array();
               }
               else IF_( m_count.empty() && ( ( m_node->m_flags & HAS_UNIQUE_ITEMS ) != 0 ) ) {
                  m_hash = std::make_unique< events::hash >();
               }
            }
            IF_( m_match && m_count.empty() ) {
               IF_( const auto* p = m_node->m_items ) {
                  IF_( p->is_object() ) {
                     m_item = m_container->consumer( p );
                  }
                  else {
                     const auto& a = p->get_array();
                     IF_( !a.empty() ) {
                        m_item = m_container->consumer( &a[ 0 ].skip_value_ptr() );
                     }
                  }
               }
               IF_( !m_item ) {
                  IF_( const auto* p = m_node->m_additional_items ) {
                     IF_( p->is_object() ) {
                        m_item = m_container->consumer( p );
                     }
                  }
               }
            }
            m_count.push_back( 0 );
         }

         void element()
         {
            IF_( m_match ) {
               validate_enum( []( events_compare< Traits >& c ) { c.element(); return ! c.match(); } );
            }
            IF_( m_match && m_item ) {
               IF_( m_count.size() == 1 ) {
                  IF_( !m_item->finalize() ) {
                     m_match = false;
                  }
                  m_item.reset();
               }
            }
            IF_( m_match ) {
               validate_collections( []( schema_consumer& c ) { c.element(); return ! c.match(); } );
            }
            IF_( m_match && m_hash ) {
               IF_( m_count.size() == 1 ) {
                  IF_( !m_unique.emplace( m_hash->value() ).second ) {
                     m_match = false;
                  }
                  m_hash->reset();
               }
               else {
                  m_hash->element();
               }
            }
            const auto next = ++m_count.back();
            IF_( m_match && ( m_count.size() == 1 ) ) {
               IF_( const auto* p = m_node->m_items ) {
                  IF_( p->is_object() ) {
                     m_item = m_container->consumer( p );
                  }
                  else {
                     const auto& a = p->get_array();
                     IF_( next < a.size() ) {
                        m_item = m_container->consumer( &a[ next ].skip_value_ptr() );
                     }
                  }
               }
               IF_( !m_item ) {
                  IF_( const auto* p = m_node->m_additional_items ) {
                     IF_( p->is_object() ) {
                        m_item = m_container->consumer( p );
                     }
                  }
               }
            }
         }

         void end_array( const std::size_t /*unused*/ = 0 )
         {
            IF_( m_match ) {
               validate_enum( []( events_compare< Traits >& c ) { c.end_array(); return ! c.match(); } );
            }
            IF_( m_match && m_item && ( m_count.size() == 1 ) ) {
               IF_( !m_item->finalize() ) {
                  m_match = false;
               }
               m_item.reset();
            }
            IF_( m_match && ( m_count.size() == 1 ) ) {
               IF_( m_node->m_items && m_node->m_items->is_array() ) {
                  IF_( m_node->m_additional_items && m_node->m_additional_items->is_boolean() ) {
                     IF_( !m_node->m_additional_items->get_boolean() ) {
                        IF_( m_count.back() > m_node->m_items->get_array().size() ) {
                           m_match = false;
                        }
                     }
                  }
               }
            }
            IF_( m_match ) {
               validate_collections( []( schema_consumer& c ) { c.end_array(); return ! c.match(); } );
            }
            IF_( m_match && m_hash ) {
               m_hash->end_array();
            }
            IF_( m_match && ( m_count.size() == 1 ) ) {
               validate_elements( m_count.back() );
            }
            m_count.pop_back();
         }

         void begin_object( const std::size_t /*unused*/ = 0 )
         {
            IF_( m_match ) {
               validate_type( OBJECT );
            }
            IF_( m_match ) {
               validate_enum( []( events_compare< Traits >& c ) { c.begin_object(); return ! c.match(); } );
            }
            IF_( m_match ) {
               validate_collections( []( schema_consumer& c ) { c.begin_object(); return ! c.match(); } );
            }
            IF_( m_match && m_hash ) {
               m_hash->begin_object();
            }
            m_count.push_back( 0 );
         }

         void key( const std::string_view sv )
         {
            IF_( m_match ) {
               validate_enum( [ & ]( events_compare< Traits >& c ) { c.key( sv ); return ! c.match(); } );
            }
            IF_( m_match ) {
               validate_collections( [ & ]( schema_consumer& c ) { c.key( sv ); return ! c.match(); } );
            }
            IF_( m_match && m_hash ) {
               m_hash->key( sv );
            }
            IF_( m_match && ( m_count.size() == 1 ) && ( m_node->m_flags & HAS_DEPENDENCIES || !m_node->m_required.empty() ) ) {
               IF_( !m_keys.insert( std::string( sv ) ).second ) {
                  // duplicate keys immediately invalidate!
                  // TODO: throw?
                  m_match = false;
               }
            }
            IF_( m_match && m_properties.empty() && ( m_count.size() == 1 ) ) {
               IF_( const auto* p = m_node->m_properties ) {
                  const auto& o = p->get_object();
                  const auto it = o.find( sv );
                  IF_( it != o.end() ) {
                     m_properties.push_back( m_container->consumer( &it->second.skip_value_ptr() ) );
                  }
               }
               for( const auto& e : m_node->m_pattern_properties ) {
                  IF_( std::regex_search( sv.begin(), sv.end(), e.first ) ) {
                     m_properties.push_back( m_container->consumer( e.second ) );
                  }
               }
               IF_( m_properties.empty() ) {
                  IF_( const auto* p = m_node->m_additional_properties ) {
                     IF_( p->is_boolean() ) {
                        IF_( !p->get_boolean() ) {
                           m_match = false;
                        }
                     }
                     else {
                        m_properties.push_back( m_container->consumer( p ) );
                     }
                  }
               }
            }
         }

         void member()
         {
            IF_( m_match ) {
               validate_enum( []( events_compare< Traits >& c ) { c.member(); return ! c.match(); } );
            }
            IF_( m_match && !m_properties.empty() && ( m_count.size() == 1 ) ) {
               for( auto& e : m_properties ) {
                  IF_( !e->finalize() ) {
                     m_match = false;
                     break;
                  }
               }
               m_properties.clear();
            }
            IF_( m_match ) {
               validate_collections( []( schema_consumer& c ) { c.member(); return ! c.match(); } );
            }
            IF_( m_match && m_hash ) {
               m_hash->member();
            }
            ++m_count.back();
         }

         void end_object( const std::size_t /*unused*/ = 0 )
         {
            IF_( m_match ) {
               validate_enum( []( events_compare< Traits >& c ) { c.end_object(); return ! c.match(); } );
            }
            IF_( m_match ) {
               validate_collections( []( schema_consumer& c ) { c.end_object(); return ! c.match(); } );
            }
            IF_( m_match && m_hash ) {
               m_hash->end_object();
            }
            IF_( m_match && ( m_count.size() == 1 ) ) {
               validate_members( m_count.back() );
            }
            IF_( m_match && ( m_count.size() == 1 ) && !m_node->m_required.empty() ) {
               IF_( !std::includes( m_keys.begin(), m_keys.end(), m_node->m_required.begin(), m_node->m_required.end() ) ) {
                  m_match = false;
               }
            }
            IF_( m_match && ( m_count.size() == 1 ) && m_node->m_flags & HAS_DEPENDENCIES ) {
               for( const auto& e : m_node->m_property_dependencies ) {
                  IF_( m_keys.count( e.first ) != 0 ) {
                     IF_( !std::includes( m_keys.begin(), m_keys.end(), e.second.begin(), e.second.end() ) ) {
                        m_match = false;
                        break;
                     }
                  }
               }
            }
            m_count.pop_back();
         }
      };

      template< template< typename... > class Traits >
      class schema_container
         : public std::enable_shared_from_this< schema_container< Traits > >
      {
      private:
         basic_value< Traits > m_value;

         using nodes_t = std::map< const basic_value< Traits >*, std::unique_ptr< schema_node< Traits > > >;
         nodes_t m_nodes;

         void make_node( const basic_value< Traits >* p )
         {
            m_nodes.emplace( p, std::make_unique< schema_node< Traits > >( this, *p ) );
         }

      public:
         explicit schema_container( const basic_value< Traits >& v )
            : m_value( v.skip_value_ptr() )
         {
            resolve_references( m_value );
            make_node( &m_value );
            while( true ) {
               std::set< const basic_value< Traits >* > required;
               for( const auto& e : m_nodes ) {
                  auto s = e.second->referenced_pointers();
                  required.insert( s.begin(), s.end() );
               }
               for( const auto& e : m_nodes ) {
                  required.erase( e.first );
               }
               IF_( required.empty() ) {
                  break;
               }
               for( const auto& e : required ) {
                  make_node( e );
               }
            }
         }

         [[nodiscard]] std::unique_ptr< schema_consumer< Traits > > consumer( const basic_value< Traits >* p ) const
         {
            const auto it = m_nodes.find( p );
            IF_( it == m_nodes.end() ) {
               throw std::logic_error( "invalid node ptr, no schema registered" );
            }
            return std::make_unique< schema_consumer< Traits > >( this->shared_from_this(), *it->second );
         }

         [[nodiscard]] std::unique_ptr< schema_consumer< Traits > > consumer() const
         {
            return consumer( &m_value );
         }
      };

   }  // namespace internal

   template< template< typename... > class Traits >
   class basic_schema
   {
   private:
      const std::shared_ptr< const internal::schema_container< Traits > > m_container;

   public:
      explicit basic_schema( const basic_value< Traits >& v )
         : m_container( std::make_shared< internal::schema_container< Traits > >( v ) )
      {
      }

      [[nodiscard]] std::unique_ptr< internal::schema_consumer< Traits > > consumer() const
      {
         return m_container->consumer();
      }

      [[nodiscard]] bool validate( const basic_value< Traits >& v ) const
      {
         // TODO: Value validation should be implemented independently,
         // as it could be more efficient than Events validation!
         const auto c = consumer();
         events::from_value( *c, v );
         return c->finalize();
      }
   };

   using schema = basic_schema< traits >;

   template< template< typename... > class Traits >
   [[nodiscard]] basic_schema< Traits > make_schema( const basic_value< Traits >& v )
   {
      return basic_schema< Traits >( v );
   }

}  // namespace tao::json

#endif
