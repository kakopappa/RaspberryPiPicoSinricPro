/*===========================================================================*/
/*                                                                           */
/*  JSON Interface for the Raspberry Pi Pico                                 */
/*                                                                           */
/*  This adds utility functions to simplify JSON building and parsing.       */
/*                                                                           */
/*  It includes tiny-json and json-maker by Rafa Garcia -                    */
/*  https://github.com/rafagafe/tiny-json                                    */
/*  https://github.com/rafagafe/json-maker                                   */
/*                                                                           */
/*  Author: Russell Rhodes, https://github.com/RussellRhodes                 */
/*                                                                           */
/*  This is free and unencumbered software released into the public domain.  */
/*  Orignal release date: January 2026                                       */
/*                                                                           */
/*===========================================================================*/

#include "pico/stdlib.h"
#include <string.h>
#include <ctype.h>
#include "json.h"

static bool json_get_field_value( json_t const *field, jsonType_t type, jsonValue_t *value )
{
    static char *textValue = NULL;
    bool result = false;

    if ( textValue ) {
        free( textValue );
        textValue = NULL;
    }

    if ( field != NULL ) {
        if ( type == JSON_REAL && json_getType( field ) == JSON_REAL ) {
            value->real = json_getReal( field );
            result = true;
        } else if ( (type == JSON_INTEGER || type == JSON_REAL) && json_getType( field ) == JSON_INTEGER ) {
            int intValue = json_getInteger( field );
            if ( type == JSON_INTEGER ) {
                value->integer = intValue;
                result = true;
            } else if ( type == JSON_REAL ) {
                value->real = (double)intValue;
                result = true;
            }
        } else if ( type == JSON_BOOLEAN && json_getType( field ) == JSON_BOOLEAN ) {
            bool boolValue = json_getBoolean( field );
            value->boolean = (int)boolValue;
            result = true;
        } else if ( type == JSON_TEXT && json_getType( field ) == JSON_TEXT ) {
            textValue = strdup( json_getValue( field ) );
            value->text = textValue;
            //printf("textValue=[%s]\n",textValue);
            result = true;
        } else if ( type == JSON_OBJ && json_getType( field ) == JSON_OBJ ) {
            //printf("JSON_OBJ[%s]=[%s]\n", field->name, json_getValue( field ) );
            value->json_obj = (json_t *)field;
            result = true;
        } else if ( type == JSON_ARRAY && json_getType( field ) == JSON_ARRAY ) {
            value->json_obj = (json_t *)field;
            result = true;
        }
    } else {
        printf("Invalid JSON field!\n");
    }

    return result;
}

static bool json_find_field( json_t const *root, const char *name, jsonType_t type, jsonValue_t *value )
{
    bool result = false;
    json_t const* item;

    //printf("json_find_field [%s] in [%s]\n", name, root->name );

    for( item = json_getChild( root ); item != NULL && !result ; item = json_getSibling( item ) ) {
        if ( JSON_OBJ == json_getType( item ) ) {
            //printf("obj [%s] in [%s]\n", name, item->name );
            if ( strcmp(item->name, name)==0 && json_getType( item ) == type  ) {
                result = json_get_field_value( item, type, value );
            } else {
                result = json_find_field( item, name, type, value );
            }
        } else {
            //printf("item [%s] in [%s]\n", name, item->name );
            if ( strcmp(item->name, name)==0 && json_getType( item ) == type  ) {
                result = json_get_field_value( item, type, value );
            }
        }
    }

    return result;
}

//===============================================================================================================

static json_t *internal_pool = NULL;
static int max_pool_fields = MAX_POOL_FIELDS;

void json_set_max_pool_fields( int max_fields )
{
    max_pool_fields = max_fields;
}

/*! \brief Finds the named field within the JSON string and returns field value
 *  \ingroup json.c
 *
 * JSON_ARRAY's are not currently implemented.
 * 
 * \param json null terminated JSON string
 * \param name name of field to find
 * \param type type of field (JSON_OBJ, JSON_ARRAY, JSON_TEXT, JSON_BOOLEAN, JSON_INTEGER, JSON_REAL)
 * \param value pointer to jsonValue_t to return value in
 * \return true if field found
 */
bool json_get( const char *json, const char *name, jsonType_t type, jsonValue_t *value ) 
{
    bool result = false;

    if ( internal_pool != NULL ) {
        free( internal_pool );
        internal_pool = NULL;
    }
    
    internal_pool = (json_t *)malloc( sizeof(json_t)*max_pool_fields);

    if ( internal_pool ) {
        // duplicate as string is modified
        if ( json ) {
            json = strdup( json );
            if ( json ) {
                json_t const* root = json_create( (char *)json, internal_pool, max_pool_fields );
                if ( root != NULL ) {
                    //printf("Finding '%s'\n",name);
                    json_t const *field = json_getProperty( root, name );
                    if ( field != NULL ) {
                        result = json_get_field_value( field, type, value );
                    } else {
                        result = json_find_field( root, name, type, value );
                        if ( !result ) {
                            //printf("Couldn't find JSON field '%s'!\n", name);
                        }
                    }
                } else {
                    printf("Couldn't create JSON parent object!, try increasing max_fields\n");
                }
                free( (void *)json );
            } else {
                printf("Couldn't allocate JSON string!\n");
            }
        }
    } else {
        printf("Couldn't allocate internal field pool\n");
    }

    return result;
}

//===============================================================================================================

static char *json_buffer;
static size_t json_buffer_len;
static char *json_pointer;
static size_t remaining_len;

/*! \brief Initialises the start of a new JSON string
 *  \ingroup json.c
 *
 * \param buffer to contain the JSON string
 * \param buffer_len length of buffer
 * \return true if succesful
 */
bool json_put_start( char *buffer, size_t buffer_len )
{
    json_pointer = json_buffer = buffer;
    remaining_len = json_buffer_len = buffer_len;
    json_pointer = json_objOpen( json_pointer, NULL, &remaining_len ); 

    return remaining_len>0;
}

/*! \brief Puts the named field into the JSON string, a NULL name and value with type JSON_OBJ will terminate the last JSON_OBJ
 *  \ingroup json.c
 *
 * JSON_ARRAY's are not currently implemented.
 * 
 * \param name name of field to insert
 * \param value jsonValue_t to insert
 * \param type type of field (JSON_OBJ, JSON_ARRAY, JSON_TEXT, JSON_BOOLEAN, JSON_INTEGER, JSON_REAL)
 * \return true if succesful
 */
bool json_put( char const* name, jsonValue_t value, jsonType_t type ) 
{
    switch( type ) {
        case JSON_OBJ:
            if ( name != NULL ) {
                json_pointer = json_objOpen( json_pointer, name, &remaining_len ); 
            } else {
                json_pointer = json_objClose( json_pointer, &remaining_len ); 
            }
            break;
        case JSON_TEXT:
            json_pointer = json_str( json_pointer, name, value.text, &remaining_len ); 
            break;
        case JSON_BOOLEAN:
            json_pointer = json_bool( json_pointer, name, value.boolean, &remaining_len ); 
            break;
        case JSON_INTEGER:
            json_pointer = json_verylong( json_pointer, name, value.integer, &remaining_len ); 
            break;
        case JSON_REAL:
            json_pointer = json_double( json_pointer, name, value.real, &remaining_len ); 
            break;
        default:
            break;
    }

    return remaining_len>0;
}

/*! \brief Terminates the JSON string for use
 *  \ingroup json.c
 *
 * \param None
 * \return true if succesful
 */
bool json_put_end( void )
{
    json_pointer = json_objClose( json_pointer, &remaining_len ); 
    json_pointer = json_end( json_pointer, &remaining_len ); 

    return remaining_len>0;
}

