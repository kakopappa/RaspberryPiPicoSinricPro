#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "tiny-json.h"
#include "json-maker.h"

#define MAX_POOL_FIELDS     50

typedef union jsonValue_u {
    void *void_ptr;
    char *text;
    int boolean;    
    long long integer;
    double real;
    json_t *json_obj;
} jsonValue_t;

void json_set_max_pool_fields( int max_fields );
bool json_get( const char *json, const char *name, jsonType_t type, jsonValue_t *value );
bool json_put_start( char *buffer, size_t buffer_len );
bool json_put( char const* name, jsonValue_t value, jsonType_t type );
bool json_put_end( void );

#ifdef __cplusplus
}
#endif
