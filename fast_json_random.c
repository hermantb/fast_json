#include <stdio.h>
#include <math.h>
#include "fast_json.h"

#define RAND_IA         __UINT64_C(0x5851F42D4C957F2D)
#define RAND_IC         __UINT64_C(0x14057B7EF767814F)

#define	SIZE		(28)

static FAST_JSON_DATA_TYPE add_null (FAST_JSON_TYPE json);
static FAST_JSON_DATA_TYPE add_boolean (FAST_JSON_TYPE json, uint64_t * r);
static FAST_JSON_DATA_TYPE add_integer (FAST_JSON_TYPE json, uint64_t * r);
static FAST_JSON_DATA_TYPE add_double (FAST_JSON_TYPE json, uint64_t * r);
static FAST_JSON_DATA_TYPE add_string (FAST_JSON_TYPE json, uint64_t * r);
static FAST_JSON_DATA_TYPE add_object (FAST_JSON_TYPE json, uint64_t * r, int depth);
static FAST_JSON_DATA_TYPE add_array (FAST_JSON_TYPE json, uint64_t * r,
				      int depth);


static FAST_JSON_DATA_TYPE
add_null (FAST_JSON_TYPE json)
{
  return fast_json_create_null (json);
}

static FAST_JSON_DATA_TYPE
add_boolean (FAST_JSON_TYPE json, uint64_t * r)
{
  *r = *r * RAND_IA + RAND_IC;
  return fast_json_create_boolean_value (json, (*r >> 10) & 1);
}

static FAST_JSON_DATA_TYPE
add_integer (FAST_JSON_TYPE json, uint64_t * r)
{
  *r = *r * RAND_IA + RAND_IC;
  return fast_json_create_integer_value (json, *r);
}

static FAST_JSON_DATA_TYPE
add_double (FAST_JSON_TYPE json, uint64_t * r)
{
  unsigned int exp;
  *r = *r * RAND_IA + RAND_IC;
  exp = (*r >> 10) % 100;
  *r = *r * RAND_IA + RAND_IC;
  return fast_json_create_double_value (json, ldexp ((double) ((int64_t) *r), exp));
}

static FAST_JSON_DATA_TYPE
add_string (FAST_JSON_TYPE json, uint64_t * r)
{
  unsigned int i;
  unsigned int n;
  char str[32];

  n = ((*r >> 10) % (sizeof (str) - 5)) + 4;
  for (i = 0; i < n; i++) {
    *r = *r * RAND_IA + RAND_IC;
    if (i < (sizeof (str) - 5) && ((*r >> 10) % 10) == 0) {
      unsigned int uc;

      do {
        *r = *r * RAND_IA + RAND_IC;
        uc = ((*r >> 10) % 0x10FFFFu) + 1;
      } while (uc == '\\' || (uc >= 0xd800u && uc <= 0xdfffu));
      if (uc < 0x80u) {
	str[i+0] = uc;
      }
      else if (uc < 0x800u) {
        str[i+0] = ((uc >> 6) & 0x1Fu) | 0xC0u;
        str[i+1] = ((uc >> 0) & 0x3Fu) | 0x80u;
	i += 1;
      }
      else if (uc < 0x10000u) {
        str[i+0] = ((uc >> 12) & 0x0Fu) | 0xE0u;
        str[i+1] = ((uc >> 6) & 0x3Fu) | 0x80u;
        str[i+2] = ((uc >> 0) & 0x3Fu) | 0x80u;
	i += 2;
      }
      else if (uc <= 0x10FFFFu) {
        str[i+0] = ((uc >> 18) & 0x07u) | 0xF0u;
        str[i+1] = ((uc >> 12) & 0x3Fu) | 0x80u;
        str[i+2] = ((uc >> 6) & 0x3Fu) | 0x80u;
        str[i+3] = ((uc >> 0) & 0x3Fu) | 0x80u;
	i += 3;
      }
    }
    else {
      do {
        *r = *r * RAND_IA + RAND_IC;
        str[i] = ((*r >> 10) % 96) + 32;
     } while (str[i] == '\\');
    }
  }
  str[i] = '\0';
  return fast_json_create_string (json, str);
}

static FAST_JSON_DATA_TYPE
add_object (FAST_JSON_TYPE json, uint64_t * r, int depth)
{
  if (depth < 10) {
    unsigned int i;
    unsigned int j;
    unsigned int n;
    char name[16];
    FAST_JSON_DATA_TYPE o;

    o = fast_json_create_object (json);
    *r = *r * RAND_IA + RAND_IC;
    n = (*r >> 10) % SIZE;
    for (i = 0; i < n; i++) {
      unsigned int m;
      FAST_JSON_DATA_TYPE v;

      m = ((*r >> 10) % (sizeof (name) - 5)) + 4;
      for (j = 0; j < m; j++) {
        do {
	  *r = *r * RAND_IA + RAND_IC;
	  name[j] = ((*r >> 10) % 96) + 32;
	} while (name[j] == '\\');
      }
      name[j] = '\0';
      *r = *r * RAND_IA + RAND_IC;
      v = NULL;
      switch ((*r >> 10) % 7) {
      case 0:
	v = add_null (json);
	break;
      case 1:
	v = add_boolean (json, r);
	break;
      case 2:
	v = add_integer (json, r);
	break;
      case 3:
	v = add_double (json, r);
	break;
      case 4:
	v = add_string (json, r);
	break;
      case 5:
	v = add_array (json, r, depth + 1);
	break;
      case 6:
	v = add_object (json, r, depth + 1);
	break;
      }
      if (v) {
        fast_json_add_object (json, o, name, v);
      }
    }
    return o;
  }
  return NULL;
}

static FAST_JSON_DATA_TYPE
add_array (FAST_JSON_TYPE json, uint64_t * r, int depth)
{
  if (depth < 10) {
    unsigned int i;
    unsigned int n;
    FAST_JSON_DATA_TYPE a;

    a = fast_json_create_array (json);
    *r = *r * RAND_IA + RAND_IC;
    n = (*r >> 10) % SIZE;
    for (i = 0; i < n; i++) {
      FAST_JSON_DATA_TYPE v;

      *r = *r * RAND_IA + RAND_IC;
      v = NULL;
      switch ((*r >> 10) % 7) {
      case 0:
	v = add_null (json);
	break;
      case 1:
	v = add_boolean (json, r);
	break;
      case 2:
	v = add_integer (json, r);
	break;
      case 3:
	v = add_double (json, r);
	break;
      case 4:
	v = add_string (json, r);
	break;
      case 5:
	v = add_array (json, r, depth + 1);
	break;
      case 6:
	v = add_object (json, r, depth + 1);
	break;
      }
      if (v) {
        fast_json_add_array (json, a, v);
      }
    }
    return a;
  }
  return NULL;
}

int
main (void)
{
  unsigned int depth = 0;
  uint64_t r = 1234567890;
  FAST_JSON_TYPE json;
  FAST_JSON_DATA_TYPE a;

  json = fast_json_create (NULL, NULL, NULL);
  fast_json_options (json, FAST_JSON_NO_CHECK_LOOP);
  a = add_array (json, &r, depth);
  fast_json_print_file (json, a, stdout, 0);
  fast_json_free (json);
  return 0;
}
