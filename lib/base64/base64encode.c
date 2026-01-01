#include <string.h>
#include <math.h>

#include "pico/stdlib.h"

static char *BASE64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

void base64_encode(const char *in, const unsigned long in_len, char *out) {
  int in_index = 0;
  int out_index = 0;

  while (in_index < in_len) {
    // process group of 24 bit

    // first 6-bit
    out[out_index++] = BASE64[ (in[in_index] & 0xFC) >> 2 ];

    if ((in_index + 1) == in_len) {
      // padding case n.1
      //
      // Remaining bits to process are the right-most 2 bit of on the
      // last byte of input. we also need to add two bytes of padding
      out[out_index++] = BASE64[ ((in[in_index] & 0x3) << 4) ];
      out[out_index++] = '=';
      out[out_index++] = '=';
      break;
    }

    // second 6-bit
    out[out_index++] = BASE64[ ((in[in_index] & 0x3) << 4) | ((in[in_index+1] & 0xF0) >> 4) ];

    if ((in_index + 2) == in_len) {
      // padding case n.2
      //
      // Remaining bits to process are the right most 4 bit on the
      // last byte of input. We also need to add a single byte of
      // padding.
      out[out_index++] = BASE64[ ((in[in_index + 1] & 0xF) << 2) ];
      out[out_index++] = '=';
      break;
    }

    // third 6-bit
    out[out_index++] = BASE64[ ((in[in_index + 1] & 0xF) << 2) | ((in[in_index + 2] & 0xC0) >> 6) ];

    // fourth 6-bit
    out[out_index++] = BASE64[ in[in_index + 2] & 0x3F ];

    in_index += 3;
  }

  out[out_index] = '\0';
  return;
}
