/* Copyright 2019 Herman ten Brugge
 *
 * Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
 * http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
 * <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
 * option. This file may not be copied, modified, or distributed
 * except according to those terms.
 */

/* Some json files start with '0xEF 0xBB 0xBF' wich is a non breaking
 * space in utf8. This is used to detect how to decode the file.
 * This is however not valid json and will not work.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include <locale.h>
#include <errno.h>
#include "fast_json.h"
#if USE_FAST_CONVERT
#include "fast_convert.h"
#endif

#define	FAST_JSON_EOF		(-1)
#define	FAST_JSON_INITIAL_SIZE	(4)
#define	FAST_JSON_BUFFER_SIZE	(4096)
#define	FAST_JSON_BIG_SIZE	(FAST_JSON_BUFFER_SIZE / \
				 sizeof (struct fast_json_data_struct))

#define SSORT(S_base,S_nel,S_width,S_comp)                              \
{                                                                       \
      size_t S_wnel, S_gap, S_wgap, S_i, S_j, S_k;                      \
      char   *S_a, *S_b, S_tmp;                                         \
                                                                        \
      S_wnel = (S_width) * (S_nel);                                     \
      for (S_gap = 0; ++S_gap < (S_nel);)                               \
            S_gap *= 3;                                                 \
      while ( S_gap /= 3 )                                              \
      {                                                                 \
            S_wgap = (S_width) * S_gap;                                 \
            for (S_i = S_wgap; S_i < S_wnel; S_i += (S_width))          \
            {                                                           \
                  for (S_j = S_i - S_wgap; ;S_j -= S_wgap)              \
                  {                                                     \
                        S_a = S_j + (char *)(S_base);                   \
                        S_b = S_a + S_wgap;                             \
                        if ( (S_comp)(S_a, S_b) <= 0 )                  \
                              break;                                    \
                        S_k = (S_width);                                \
                        do                                              \
                        {                                               \
                              S_tmp = *S_a;                             \
                              *S_a++ = *S_b;                            \
                              *S_b++ = S_tmp;                           \
                        } while ( --S_k );                              \
                        if (S_j < S_wgap)                               \
                              break;                                    \
                  }                                                     \
            }                                                           \
      }                                                                 \
}

/* Do not use the ctype functions */
#define	fast_json_isdigit(c)	((c) >= '0' && (c) <= '9')
#define	fast_json_isxdigit(c)	(((c) >= '0' && (c) <= '9') || \
				 ((c) >= 'a' && (c) <= 'f') || \
			  	 ((c) >= 'A' && (c) <= 'F'))
#define	fast_json_isalpha(c)	(((c) >= 'a' && (c) <= 'z') || \
				 ((c) >= 'A' && (c) <= 'Z'))
#define	fast_json_isspace(c)	((c) == ' ' || (c) == '\t' || \
				 (c) == '\n' || (c) == '\r')

typedef int (*fast_json_getc_func) (FAST_JSON_TYPE json);
typedef int (*fast_json_puts_func) (FAST_JSON_TYPE json, const char *str,
				    unsigned int len);

typedef struct fast_json_big_struct
{
  size_t count;
  struct fast_json_big_struct *data;
} FAST_JSON_BIG_TYPE;

struct fast_json_struct
{
  const char *json_str;
  char decimal_point;
  FAST_JSON_ERROR_ENUM error;
  unsigned int options;
  fast_json_malloc_type my_malloc;
  fast_json_realloc_type my_realloc;
  fast_json_free_type my_free;
  fast_json_getc_func getc;
  fast_json_puts_func puts;
  union
  {
    struct
    {
      const char *string;
      size_t pos;
      size_t len;
    } str;
    FILE *fp;
    struct
    {
      int fd;
      char buffer[FAST_JSON_BUFFER_SIZE];
      size_t pos;
      size_t len;
    } fd;
    struct
    {
      size_t len;
      size_t max;
      char *txt;
    } buf;
  } u;
  int last_char;
  unsigned int puts_len;
  char puts_buf[FAST_JSON_BUFFER_SIZE];
  unsigned int n_save;
  unsigned int max_save;
  char *save;
  size_t line;
  size_t column;
  size_t last_column;
  size_t position;
  unsigned int max_reuse;
  unsigned int n_reuse;
  struct fast_json_data_struct *json_reuse;
  unsigned int n_big_malloc;
  FAST_JSON_BIG_TYPE *big_malloc;
  FAST_JSON_BIG_TYPE *big_malloc_free;
  char error_str[1000];
};

typedef struct fast_json_name_value_struct
{
  char *name;
  FAST_JSON_DATA_TYPE value;
} FAST_JSON_NAME_VALUE_TYPE;

typedef struct fast_json_object_struct
{
  size_t len;
  size_t max;
  FAST_JSON_NAME_VALUE_TYPE data[1];
} FAST_JSON_OBJECT_TYPE;

typedef struct fast_json_array_struct
{
  size_t len;
  size_t max;
  FAST_JSON_DATA_TYPE values[1];
} FAST_JSON_ARRAY_TYPE;

struct fast_json_data_struct
{
  unsigned char type;		/* FAST_JSON_VALUE_TYPE type */
  unsigned char is_str;
  unsigned short used;
  unsigned int index;
  union
  {
    unsigned int boolean_value;
    fast_json_int_64 int_value;
    double double_value;
    char *string_value;
    char i_string_value[8];
    FAST_JSON_OBJECT_TYPE *object;
    FAST_JSON_ARRAY_TYPE *array;
    FAST_JSON_DATA_TYPE next;
  } u;
};

static double fast_json_nan (unsigned int sign);
static double fast_json_inf (unsigned int sign);
static char *fast_json_strdup (FAST_JSON_TYPE json, const char *str);
static FAST_JSON_DATA_TYPE fast_json_data_create (FAST_JSON_TYPE json);
static void fast_json_data_free (FAST_JSON_TYPE json,
				 FAST_JSON_DATA_TYPE ptr);
static void fast_json_store_error (FAST_JSON_TYPE json,
				   FAST_JSON_ERROR_ENUM error,
				   const char *str);
static int fast_json_getc_string (FAST_JSON_TYPE json);
static int fast_json_getc_string_len (FAST_JSON_TYPE json);
static int fast_json_getc_file (FAST_JSON_TYPE json);
static int fast_json_getc_fd (FAST_JSON_TYPE json);
static void fast_json_ungetc (FAST_JSON_TYPE json, int c);
static int fast_json_getc_save (FAST_JSON_TYPE json);
static void fast_json_getc_save_start (FAST_JSON_TYPE json, int c);
static char *fast_json_ungetc_save (FAST_JSON_TYPE json, int c);
static FAST_JSON_ERROR_ENUM fast_json_skip_whitespace (FAST_JSON_TYPE json,
						       int *next);
static unsigned int fast_json_hex4 (FAST_JSON_TYPE json, const char **buf);
static FAST_JSON_ERROR_ENUM fast_json_check_string (FAST_JSON_TYPE json,
						    const char *save,
						    const char *end,
						    char *out);
static FAST_JSON_DATA_TYPE fast_json_parse_value (FAST_JSON_TYPE json, int c);
static FAST_JSON_DATA_TYPE fast_json_parse_all (FAST_JSON_TYPE json);
static FAST_JSON_ERROR_ENUM fast_json_skip_whitespace2 (FAST_JSON_TYPE json,
							const char **buf);
static void fast_json_store_error2 (FAST_JSON_TYPE json,
				    FAST_JSON_ERROR_ENUM error,
				    const char *cp, const char *sep);
static FAST_JSON_DATA_TYPE fast_json_parse_value2 (FAST_JSON_TYPE json,
						   const char **buf);
static int fast_json_puts_string (FAST_JSON_TYPE json, const char *str,
				  unsigned int len);
static int fast_json_puts_string_len (FAST_JSON_TYPE json, const char *str,
				      unsigned int len);
static int fast_json_puts_file (FAST_JSON_TYPE json, const char *str,
				unsigned int len);
static int fast_json_puts_fd (FAST_JSON_TYPE json, const char *str,
			      unsigned int len);
static int fast_json_puts (FAST_JSON_TYPE json, const char *str,
			   unsigned int len);
static int fast_json_last_puts (FAST_JSON_TYPE json, const char *str,
				unsigned int len);
static int fast_json_print_string_value (FAST_JSON_TYPE json, const char *s);
static int fast_json_print_spaces (FAST_JSON_TYPE json, unsigned int n);
static int fast_json_compare_object (const void *a, const void *b);
static int fast_json_print_buffer (FAST_JSON_TYPE json,
				   FAST_JSON_DATA_TYPE value,
				   unsigned int n, unsigned int nice);
static unsigned int fast_json_check_loop (FAST_JSON_DATA_TYPE data,
					  FAST_JSON_DATA_TYPE value);
static FAST_JSON_ERROR_ENUM fast_json_add_array_end (FAST_JSON_TYPE json,
						     FAST_JSON_DATA_TYPE
						     array,
						     FAST_JSON_DATA_TYPE
						     value);
static FAST_JSON_ERROR_ENUM fast_json_add_object_end (FAST_JSON_TYPE json,
						      FAST_JSON_DATA_TYPE
						      object,
						      const char *name,
						      FAST_JSON_DATA_TYPE
						      value);
static void fast_json_update_crc (unsigned int *crc, const char *str);
static FAST_JSON_ERROR_ENUM fast_json_parse_crc (FAST_JSON_TYPE json,
						 unsigned int *crc, int c);
static FAST_JSON_ERROR_ENUM fast_json_calc_crc_all (FAST_JSON_TYPE json,
						    unsigned int *res_crc);

static const char fast_json_utf8_size[256] = {
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
  2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
  3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
  4, 4, 4, 4, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static double
fast_json_nan (unsigned int sign)
{
  union
  {
    uint64_t u;
    double d;
  } td;

  td.u = sign ? UINT64_C (0xFFF8000000000000) : UINT64_C (0x7FF8000000000000);
  return td.d;
}

static double
fast_json_inf (unsigned int sign)
{
  union
  {
    uint64_t u;
    double d;
  } td;

  td.u = sign ? UINT64_C (0xFFF0000000000000) : UINT64_C (0x7FF0000000000000);
  return td.d;
}

static char *
fast_json_strdup (FAST_JSON_TYPE json, const char *str)
{
  size_t len = strlen (str) + 1;
  void *ret = (*json->my_malloc) (len);

  if (ret) {
    memcpy (ret, str, len);
  }
  return (char *) ret;
}

static FAST_JSON_DATA_TYPE
fast_json_data_create (FAST_JSON_TYPE json)
{
  FAST_JSON_DATA_TYPE v;

  if (json->n_reuse) {
    json->n_reuse--;
    v = json->json_reuse;
    json->json_reuse = json->json_reuse->u.next;
  }
  else if ((json->options & FAST_JSON_BIG_ALLOC) != 0) {
    v =
      (FAST_JSON_DATA_TYPE) (*json->my_malloc) (FAST_JSON_BIG_SIZE *
						sizeof (*v));
    if (v) {
      size_t i;
      size_t index;
      FAST_JSON_BIG_TYPE *b = json->big_malloc_free;

      if (b == NULL) {
	unsigned int size =
	  json->n_big_malloc == 0 ? 1 : json->n_big_malloc * 2;

	b =
	  (FAST_JSON_BIG_TYPE *) (*json->my_realloc) (json->big_malloc,
						      size * sizeof (*b));
	if (b == NULL) {
	  (*json->my_free) (v);
	  return NULL;
	}
	for (i = json->n_big_malloc + 1; i < size; i++) {
	  b[i].count = i;
	  b[i].data = json->big_malloc_free;
	  json->big_malloc_free = &b[i];
	}
	index = json->n_big_malloc;
	b[index].count = FAST_JSON_BIG_SIZE;
	b[index].data = (FAST_JSON_BIG_TYPE *) v;
	json->n_big_malloc = size;
	json->big_malloc = b;
      }
      else {
	json->big_malloc_free = json->big_malloc_free->data;
	index = b->count;
	json->big_malloc[index].count = FAST_JSON_BIG_SIZE;
	json->big_malloc[index].data = (FAST_JSON_BIG_TYPE *) v;
      }
      for (i = 0; i < FAST_JSON_BIG_SIZE; i++) {
	FAST_JSON_DATA_TYPE ptr = &v[i];

	json->n_reuse++;
	ptr->index = index;
	ptr->u.next = json->json_reuse;
	json->json_reuse = ptr;
      }
      json->n_reuse--;
      v = json->json_reuse;
      json->json_reuse = json->json_reuse->u.next;
    }
  }
  else {
    v = (FAST_JSON_DATA_TYPE) (*json->my_malloc) (sizeof (*v));
    if (v) {
      v->index = 0xFFFFFFFFu;
    }
  }
  return v;
}

static void
fast_json_data_free (FAST_JSON_TYPE json, FAST_JSON_DATA_TYPE ptr)
{
  if (ptr) {
    size_t index = ptr->index;

    if (ptr->index != 0xFFFFFFFFu) {
      if (--json->big_malloc[ptr->index].count == 0) {

	(*json->my_free) (json->big_malloc[index].data);
	json->big_malloc[index].count = index;
	json->big_malloc[index].data = json->big_malloc_free;
	json->big_malloc_free = &json->big_malloc[index];
      }
    }
    else {
      if (json->n_reuse < json->max_reuse) {
	json->n_reuse++;
	ptr->u.next = json->json_reuse;
	json->json_reuse = ptr;
      }
      else {
	(*json->my_free) (ptr);
      }
    }
  }
}

static void
fast_json_store_error (FAST_JSON_TYPE json, FAST_JSON_ERROR_ENUM error,
		       const char *cp)
{
  int len = sizeof (json->error_str) - 1;
  char *rp = json->error_str;

  json->error = error;
  while (len-- && *cp) {
    *rp++ = *cp++;
  }
  *rp = '\0';
}

static int
fast_json_getc_string (FAST_JSON_TYPE json)
{
  int c;

  c = json->u.str.string[json->u.str.pos];
  if (c == '\0') {
    return FAST_JSON_EOF;
  }
  json->u.str.pos++;
  return c & 0xFFu;
}

static int
fast_json_getc_string_len (FAST_JSON_TYPE json)
{
  if (json->u.str.pos >= json->u.str.len) {
    return FAST_JSON_EOF;
  }
  return json->u.str.string[json->u.str.pos++] & 0xFFu;
}

static int
fast_json_getc_file (FAST_JSON_TYPE json)
{
  return getc (json->u.fp);
}

static int
fast_json_getc_fd (FAST_JSON_TYPE json)
{
#if 0				/* too slow */
  char c;
  int len = read (json->u.fd.fd, &c, 1);

  if (len <= 0) {
    return FAST_JSON_EOF;
  }
  return c & 0xFFu;
#else
  if (json->u.fd.pos >= json->u.fd.len) {
    int len =
      read (json->u.fd.fd, json->u.fd.buffer, sizeof (json->u.fd.buffer));

    if (len <= 0) {
      return FAST_JSON_EOF;
    }
    json->u.fd.len = len;
    json->u.fd.pos = 0;
  }
  return json->u.fd.buffer[json->u.fd.pos++] & 0xFFu;
#endif
}

static int
fast_json_getc (FAST_JSON_TYPE json)
{
  int c;

  if (json->last_char) {
    c = json->last_char;
    json->last_char = 0;
  }
  else {
    c = json->getc (json);
  }
  if (c <= 0) {
    return FAST_JSON_EOF;
  }
  json->column += fast_json_utf8_size[c] != 0;
  json->position++;
  if (c == '\n') {
    json->line++;
    json->last_column = json->column;
    json->column = 0;
  }
  return c & 0xFFu;
}

static void
fast_json_ungetc (FAST_JSON_TYPE json, int c)
{
  if (c > 0) {
    if (c == '\n') {
      json->line--;
      json->column = json->last_column;
    }
    json->column -= fast_json_utf8_size[c] != 0;
    json->position--;
    json->last_char = c;
  }
}

static int
fast_json_getc_save (FAST_JSON_TYPE json)
{
  int c = fast_json_getc (json);

  if (c > 0) {
    if (json->n_save + 2 > json->max_save) {
      unsigned int new_max = json->max_save + FAST_JSON_BUFFER_SIZE;
      char *new_save;

      new_save = (char *) json->my_realloc (json->save, new_max);
      if (new_save == NULL) {
	return 0;
      }
      json->max_save = new_max;
      json->save = new_save;
    }
    json->save[json->n_save++] = c;
  }
  return c;
}

static void
fast_json_getc_save_start (FAST_JSON_TYPE json, int c)
{
  json->n_save = 0;
  if (json->max_save == 0) {
    unsigned int new_max = FAST_JSON_BUFFER_SIZE;
    char *new_save;

    new_save = (char *) json->my_realloc (json->save, new_max);
    if (new_save == NULL) {
      return;
    }
    json->max_save = new_max;
    json->save = new_save;
  }
  json->save[json->n_save++] = c;
}

static char *
fast_json_ungetc_save (FAST_JSON_TYPE json, int c)
{
  if (c > 0) {
    fast_json_ungetc (json, c);
    if (json->n_save) {
      json->n_save--;
    }
  }
  json->save[json->n_save] = 0;
  return json->save;
}

static FAST_JSON_ERROR_ENUM
fast_json_skip_whitespace (FAST_JSON_TYPE json, int *next)
{
  FAST_JSON_ERROR_ENUM retval = FAST_JSON_OK;

  for (;;) {
    int c = fast_json_getc (json);

    while (fast_json_isspace (c)) {
      c = fast_json_getc (json);
    }
    if (c == '/') {
      c = fast_json_getc (json);
      if (c == '*') {
	c = fast_json_getc (json);
	while (c > 0) {
	  if (c == '*') {
	    c = fast_json_getc (json);
	    if (c == '/') {
	      break;
	    }
	  }
	  else {
	    c = fast_json_getc (json);
	  }
	}
      }
      else if (c == '/') {
	c = fast_json_getc (json);
	while (c > 0) {
	  if (c == '\n') {
	    break;
	  }
	  c = fast_json_getc (json);
	}
      }
      else {
	char str[3];

	str[0] = '/';
	str[1] = c > 0 ? c : '\0';
	str[2] = '\0';
	fast_json_store_error (json, FAST_JSON_COMMENT_ERROR, str);
	retval = FAST_JSON_COMMENT_ERROR;
	break;
      }
    }
    else {
      *next = c;
      break;
    }
  }
  return retval;
}

static unsigned int
fast_json_hex4 (FAST_JSON_TYPE json, const char **buf)
{
  unsigned int i;
  unsigned int h = 0;
  const char *str = *buf;

  for (i = 0; i < 4; i++) {
    h = h << 4;
    if (*str >= '0' && *str <= '9') {
      h += (*str) - '0';
    }
    else if (*str >= 'A' && *str <= 'F') {
      h += 10 + (*str) - 'A';
    }
    else if (*str >= 'a' && *str <= 'f') {
      h += 10 + (*str) - 'a';
    }
    else {
      fast_json_store_error (json, FAST_JSON_UNICODE_ESCAPE_ERROR, *buf);
      return 0xFFFFFFFFu;
    }
    str++;
  }
  *buf = str;
  return h;
}

static FAST_JSON_ERROR_ENUM
fast_json_check_string (FAST_JSON_TYPE json, const char *save,
			const char *end, char *out)
{
  const char *ptr;
  const char *save_ptr;
  char *ptr2;
  unsigned int uc;

  ptr = save;
  ptr2 = out;
  while (ptr < end) {
    static const char special1[256] = {
      1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
      1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
      0, 0, '"', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '\\', 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
      1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
      1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
      1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
      1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
      1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
      1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
      1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1
    };
    if (special1[*ptr & 0xFFu] == 0) {
      *ptr2++ = *ptr++;
    }
    else if ((*ptr & 0x80u) != 0) {
      unsigned char u = *ptr;
      unsigned char size = 0;
      unsigned int error = 1;

      switch (fast_json_utf8_size[u]) {
      case 0:			/* FALLTHRU */
      case 1:
	/* u >= 0x00u && u <= 0x7Fu not possible */
	/* u >= 0x80u && u <= 0xBFu should be size 2 */
	/* u >= 0xC0u && u <= 0xC1u remapped 0x00u..0x7Fu */
	/* u >= 0xF5u && u <= 0xFFu not valid */
	break;
      case 2:
	if (ptr[1]) {
	  unsigned char u1 = ptr[1];

	  if (u1 >= 0x80u && u1 <= 0xBFu) {
	    error = 0;
	    size = 2;
	    uc = ((u & 0x1Fu) << 6) | (u1 & 0x3Fu);
	  }
	}
	break;
      case 3:
	if (ptr[1] && ptr[2]) {
	  unsigned char u1 = ptr[1];
	  unsigned char u2 = ptr[2];

	  if (u1 >= 0x80u && u1 <= 0xBFu && u2 >= 0x80u && u2 <= 0xBFu) {
	    error = 0;
	    size = 3;
	    uc = ((u & 0x0Fu) << 12) | ((u1 & 0x3Fu) << 6) | (u2 & 0x3Fu);
	  }
	}
	break;
      case 4:
	if (ptr[1] && ptr[2] && ptr[3]) {
	  unsigned char u1 = ptr[1];
	  unsigned char u2 = ptr[2];
	  unsigned char u3 = ptr[3];

	  if (u1 >= 0x80u && u1 <= 0xBFu &&
	      u2 >= 0x80u && u2 <= 0xBFu && u3 >= 0x80u && u3 <= 0xBFu) {
	    error = 0;
	    size = 4;
	    uc = ((u & 0x7u) << 18) | ((u1 & 0x3Fu) << 12) |
	      ((u2 & 0x3Fu) << 6) | (u3 & 0x3Fu);
	  }
	}
	break;
      }
      if (error == 0 &&
	  (uc > 0x10FFFFu ||
	   (uc >= 0xD800u && uc <= 0xDFFFu) ||
	   (size == 2 && uc < 0x80u) ||
	   (size == 3 && uc < 0x800u) || (size == 4 && uc < 0x10000u))) {
	error = 1;
      }

      if (error) {
	fast_json_store_error (json, FAST_JSON_UTF8_ERROR, ptr);
	return FAST_JSON_UTF8_ERROR;
      }
      while (size--) {
	*ptr2++ = *ptr++;
      }
    }
    else if (*ptr == '\\') {
      static const char special2[256] = {
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '/',
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, '\b', 0, 0, 0, '\f', 0, 0, 0, 0, 0, 0, 0, '\n', 0,
	0, 0, '\r', 0, '\t', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
      };
      ptr++;
      if (special2[*ptr & 0xFFu]) {
	*ptr2++ = special2[*ptr++ & 0xFFu];
      }
      else if (*ptr == 'u') {
	save_ptr = ptr - 1;
	ptr++;
	uc = fast_json_hex4 (json, &ptr);
	if (uc == 0xFFFFFFFFu) {
	  return json->error;
	}
	if (uc >= 0xD800u && uc <= 0xDBFFu) {
	  if (*ptr == '\\' && ptr[1] == 'u') {
	    unsigned int uc2;
	    ptr += 2;

	    uc2 = fast_json_hex4 (json, &ptr);
	    if (uc2 == 0xFFFFFFFFu) {
	      return json->error;
	    }
	    if (uc2 >= 0xDC00u && uc2 <= 0xDFFFu) {
	      uc = ((uc - 0xD800u) << 10) + (uc2 - 0xDC00u) + 0x10000u;
	    }
	    else {
	      fast_json_store_error (json, FAST_JSON_UNICODE_ERROR, save_ptr);
	      return FAST_JSON_UNICODE_ERROR;
	    }
	  }
	  else {
	    fast_json_store_error (json, FAST_JSON_UNICODE_ERROR, save_ptr);
	    return FAST_JSON_UNICODE_ERROR;
	  }
	}
	if (uc >= 0xDC00u && uc <= 0xDFFFu) {
	  fast_json_store_error (json, FAST_JSON_UNICODE_ERROR, save_ptr);
	  return FAST_JSON_UNICODE_ERROR;
	}
	if (uc < 0x80u) {
	  if (uc == 0) {
	    *ptr2++ = '\\';
	    *ptr2++ = 'u';
	    *ptr2++ = '0';
	    *ptr2++ = '0';
	    *ptr2++ = '0';
	    *ptr2++ = '0';
	  }
	  else {
	    if (uc == '\\' || uc == '"') {
	      *ptr2++ = '\\';
	    }
	    *ptr2++ = uc;
	  }
	}
	else if (uc < 0x800u) {
	  *ptr2++ = ((uc >> 6) & 0x1Fu) | 0xC0u;
	  *ptr2++ = ((uc >> 0) & 0x3Fu) | 0x80u;
	}
	else if (uc < 0x10000u) {
	  *ptr2++ = ((uc >> 12) & 0x0Fu) | 0xE0u;
	  *ptr2++ = ((uc >> 6) & 0x3Fu) | 0x80u;
	  *ptr2++ = ((uc >> 0) & 0x3Fu) | 0x80u;
	}
	else if (uc <= 0x10FFFFu) {
	  *ptr2++ = ((uc >> 18) & 0x07u) | 0xF0u;
	  *ptr2++ = ((uc >> 12) & 0x3Fu) | 0x80u;
	  *ptr2++ = ((uc >> 6) & 0x3Fu) | 0x80u;
	  *ptr2++ = ((uc >> 0) & 0x3Fu) | 0x80u;
	}
	else {
	  /* Not possible */
	  fast_json_store_error (json, FAST_JSON_UNICODE_ERROR, save_ptr);
	  return FAST_JSON_UNICODE_ERROR;
	}
      }
      else if (*ptr == '\\' || *ptr == '"') {
	*ptr2++ = '\\';
	*ptr2++ = *ptr++;
      }
      else {
	fast_json_store_error (json, FAST_JSON_ESCAPE_CHARACTER_ERROR, ptr);
	return FAST_JSON_ESCAPE_CHARACTER_ERROR;
      }
    }
    else if (*ptr == '"') {
      fast_json_store_error (json, FAST_JSON_ESCAPE_CHARACTER_ERROR, ptr);
      return FAST_JSON_CONTROL_CHARACTER_ERROR;
    }
    else {
      fast_json_store_error (json, FAST_JSON_CONTROL_CHARACTER_ERROR, ptr);
      return FAST_JSON_CONTROL_CHARACTER_ERROR;
    }
  }
  *ptr2 = 0;
  return FAST_JSON_OK;
}

static FAST_JSON_DATA_TYPE
fast_json_parse_value (FAST_JSON_TYPE json, int c)
{
  char *save;
  FAST_JSON_DATA_TYPE v;

  fast_json_getc_save_start (json, c);
  switch (c) {
  case 'n':			/* FALLTHRU */
  case 'N':
    while (fast_json_isalpha (c)) {
      c = fast_json_getc_save (json);
    }
    save = fast_json_ungetc_save (json, c);
    if (strcmp (save, "null") == 0) {
      v = fast_json_create_null (json);
    }
    else if ((json->options & FAST_JSON_INF_NAN) != 0 &&
	     strcasecmp (save, "nan") == 0) {
      c = fast_json_getc_save (json);
      if (c == '(') {
	c = fast_json_getc_save (json);
	while (fast_json_isalpha (c) || fast_json_isdigit (c) || c == '_') {
	  c = fast_json_getc_save (json);
	}
	if (c != ')') {
	  save = fast_json_ungetc_save (json, 0);
	  fast_json_store_error (json, FAST_JSON_NUMBER_ERROR, save);
	  return NULL;
	}
      }
      else {
	fast_json_ungetc (json, c);
      }
      v = fast_json_create_double_value (json, fast_json_nan (0));
    }
    else {
      fast_json_store_error (json, FAST_JSON_VALUE_ERROR, save);
      return NULL;
    }
    break;
  case 'f':
    while (fast_json_isalpha (c)) {
      c = fast_json_getc_save (json);
    }
    save = fast_json_ungetc_save (json, c);
    if (strcmp (save, "false") == 0) {
      v = fast_json_create_false (json);
    }
    else {
      fast_json_store_error (json, FAST_JSON_VALUE_ERROR, save);
      return NULL;
    }
    break;
  case 't':
    while (fast_json_isalpha (c)) {
      c = fast_json_getc_save (json);
    }
    save = fast_json_ungetc_save (json, c);
    if (strcmp (save, "true") == 0) {
      v = fast_json_create_true (json);
    }
    else {
      fast_json_store_error (json, FAST_JSON_VALUE_ERROR, save);
      return NULL;
    }
    break;
  case 'i':			/* FALLTHRU */
  case 'I':
    while (fast_json_isalpha (c)) {
      c = fast_json_getc_save (json);
    }
    save = fast_json_ungetc_save (json, c);
    if ((json->options & FAST_JSON_INF_NAN) != 0 &&
	(strcasecmp (save, "inf") == 0 ||
	 strcasecmp (save, "infinity") == 0)) {
      v = fast_json_create_double_value (json, fast_json_inf (0));
    }
    else {
      fast_json_store_error (json, FAST_JSON_VALUE_ERROR, save);
      return NULL;
    }
    break;
  case '"':
    {
      char str[8];
      char *out = &str[0];

      c = fast_json_getc (json);
      fast_json_getc_save_start (json, c);
      while (c > 0 && c != '"') {
	if (c == '\\') {
	  fast_json_getc_save (json);
	}
	c = fast_json_getc_save (json);
      }
      save = fast_json_ungetc_save (json, c);
      if (json->n_save >= sizeof (str)) {
	out = (char *) (*json->my_malloc) (json->n_save + 1);
	if (out == NULL) {
	  json->error = FAST_JSON_MALLOC_ERROR;
	  return NULL;
	}
      }
      if (fast_json_check_string (json, save, save + json->n_save, out) !=
	  FAST_JSON_OK) {
	if (out != &str[0]) {
	  (*json->my_free) (out);
	}
	return NULL;
      }
      c = fast_json_getc (json);
      if (c != '"') {
	fast_json_store_error (json, FAST_JSON_STRING_END_ERROR, save);
	if (out != &str[0]) {
	  (*json->my_free) (out);
	}
	return NULL;
      }
      v = fast_json_data_create (json);
      if (v) {
	v->type = FAST_JSON_STRING;
	v->used = 0;
	if (out != &str[0]) {
	  v->is_str = 0;
	  v->u.string_value = out;
	}
	else {
	  v->is_str = 1;
	  strcpy (v->u.i_string_value, out);
	}
      }
    }
    break;
  case '+':			/* FALLTHRU */
  case '-':			/* FALLTHRU */
  case '0':			/* FALLTHRU */
  case '1':			/* FALLTHRU */
  case '2':			/* FALLTHRU */
  case '3':			/* FALLTHRU */
  case '4':			/* FALLTHRU */
  case '5':			/* FALLTHRU */
  case '6':			/* FALLTHRU */
  case '7':			/* FALLTHRU */
  case '8':			/* FALLTHRU */
  case '9':
    {
      unsigned int integer = 1;
      unsigned int hex = 0;
      unsigned int sign = 0;
      char *end;
      double n;

      fast_json_getc_save_start (json, c);
      if (c == '+') {
	if ((json->options & FAST_JSON_INF_NAN) != 0) {
	  c = fast_json_getc_save (json);
	}
      }
      else if (c == '-') {
	sign = 1;
	c = fast_json_getc_save (json);
      }
      if (c == '0') {
	c = fast_json_getc_save (json);
	if ((json->options & FAST_JSON_ALLOW_OCT_HEX) != 0) {
	  if (c == 'x' || c == 'X') {
	    hex = 1;
	    c = fast_json_getc_save (json);
	    while (fast_json_isxdigit (c)) {
	      c = fast_json_getc_save (json);
	    }
	  }
	  else {
	    while (c >= '0' && c <= '7') {
	      c = fast_json_getc_save (json);
	    }
	  }
	}
      }
      else if (fast_json_isdigit (c)) {
	do {
	  c = fast_json_getc_save (json);
	} while (fast_json_isdigit (c));
      }
      else {
	unsigned int last_n_save = json->n_save > 0 ? json->n_save - 1 : 0;

	while (fast_json_isalpha (c)) {
	  c = fast_json_getc_save (json);
	}
	fast_json_ungetc_save (json, c);
	if ((json->options & FAST_JSON_INF_NAN) != 0) {
	  if (strcasecmp (&json->save[last_n_save], "inf") == 0 ||
	      strcasecmp (&json->save[last_n_save], "infinity") == 0) {
	    return fast_json_create_double_value (json, fast_json_inf (sign));
	  }
	  else if (strcasecmp (&json->save[last_n_save], "nan") == 0) {
	    c = fast_json_getc_save (json);
	    if (c == '(') {
	      c = fast_json_getc_save (json);
	      while (fast_json_isalpha (c) || fast_json_isdigit (c) ||
		     c == '_') {
		c = fast_json_getc_save (json);
	      }
	      if (c != ')') {
		save = fast_json_ungetc_save (json, 0);
		fast_json_store_error (json, FAST_JSON_NUMBER_ERROR, save);
		return NULL;
	      }
	    }
	    else {
	      fast_json_ungetc (json, c);
	    }
	    return fast_json_create_double_value (json, fast_json_nan (sign));
	  }
	}
	fast_json_getc_save (json);
	save = fast_json_ungetc_save (json, 0);
	fast_json_store_error (json, FAST_JSON_NUMBER_ERROR, save);
	return NULL;
      }
      if (c == '.') {
	json->save[json->n_save - 1] = json->decimal_point;
	c = fast_json_getc_save (json);
	integer = 0;
	if (hex) {
	  if (fast_json_isxdigit (c)) {
	    do {
	      c = fast_json_getc_save (json);
	    } while (fast_json_isxdigit (c));
	  }
	  else {
	    save = fast_json_ungetc_save (json, 0);
	    fast_json_store_error (json, FAST_JSON_NUMBER_ERROR, save);
	    return NULL;
	  }
	}
	else {
	  if (fast_json_isdigit (c)) {
	    do {
	      c = fast_json_getc_save (json);
	    } while (fast_json_isdigit (c));
	  }
	  else {
	    save = fast_json_ungetc_save (json, 0);
	    fast_json_store_error (json, FAST_JSON_NUMBER_ERROR, save);
	    return NULL;
	  }
	}
      }
      if (c == 'e' || c == 'E' || (hex && (c == 'p' || c == 'P'))) {
	integer = 0;
	c = fast_json_getc_save (json);
	if (c == '+' || c == '-') {
	  c = fast_json_getc_save (json);
	}
	if (fast_json_isdigit (c)) {
	  do {
	    c = fast_json_getc_save (json);
	  } while (fast_json_isdigit (c));
	}
	else {
	  save = fast_json_ungetc_save (json, 0);
	  fast_json_store_error (json, FAST_JSON_NUMBER_ERROR, save);
	  return NULL;
	}
      }
      save = fast_json_ungetc_save (json, c);
      if ((json->options & FAST_JSON_PARSE_INT_AS_DOUBLE) == 0 && integer) {
	fast_json_int_64 int_value;

	errno = 0;
#if USE_FAST_CONVERT
	int_value = fast_strtos64 (save, &end);
#else
#if __WORDSIZE == 64
	int_value = strtol (save, &end, 0);
#else
	int_value = strtoll (save, &end, 0);
#endif
#endif
	if (*end == '\0' && errno != ERANGE) {
	  return fast_json_create_integer_value (json, int_value);
	}
      }
      errno = 0;
#if USE_FAST_CONVERT
      n = fast_strtod (save, &end);
#else
      n = strtod (save, &end);
#endif
      if (*end != '\0' || errno == ERANGE) {
	fast_json_store_error (json, FAST_JSON_NUMBER_ERROR, save);
	return NULL;
      }
      return fast_json_create_double_value (json, n);
    }
    break;
  case '[':
    if (fast_json_skip_whitespace (json, &c) != FAST_JSON_OK) {
      return NULL;
    }
    v = fast_json_create_array (json);
    if (v == NULL) {
      fast_json_store_error (json, FAST_JSON_MALLOC_ERROR, "");
      return NULL;
    }
    if (c == ']') {
      return v;
    }
    for (;;) {
      FAST_JSON_DATA_TYPE n = fast_json_parse_value (json, c);

      if (n == NULL) {
	fast_json_value_free (json, v);
	return NULL;
      }
      if (fast_json_add_array_end (json, v, n) != FAST_JSON_OK) {
	fast_json_value_free (json, n);
	fast_json_value_free (json, v);
	return NULL;
      }
      if (fast_json_skip_whitespace (json, &c) != FAST_JSON_OK) {
	fast_json_value_free (json, v);
	return NULL;
      }
      if (c != ',') {
	break;
      }
      if (fast_json_skip_whitespace (json, &c) != FAST_JSON_OK) {
	fast_json_value_free (json, v);
	return NULL;
      }
    }
    if (c != ']') {
      fast_json_store_error (json, FAST_JSON_ARRAY_END_ERROR, "");
      fast_json_value_free (json, v);
      return NULL;
    }
    break;
  case '{':
    if (fast_json_skip_whitespace (json, &c) != FAST_JSON_OK) {
      return NULL;
    }
    v = fast_json_create_object (json);
    if (v == NULL) {
      fast_json_store_error (json, FAST_JSON_MALLOC_ERROR, "");
      return NULL;
    }
    if (c == '}') {
      return v;
    }
    for (;;) {
      char name[16];
      char *out = &name[0];
      FAST_JSON_DATA_TYPE n;

      if (c != '"') {
	fast_json_store_error (json, FAST_JSON_STRING_START_ERROR, "");
	fast_json_value_free (json, v);
	return NULL;
      }
      c = fast_json_getc (json);
      fast_json_getc_save_start (json, c);
      while (c > 0 && c != '"') {
	if (c == '\\') {
	  fast_json_getc_save (json);
	}
	c = fast_json_getc_save (json);
      }
      save = fast_json_ungetc_save (json, c);
      if (json->n_save >= sizeof (name)) {
	out = (char *) (*json->my_malloc) (json->n_save + 1);
	if (out == NULL) {
	  json->error = FAST_JSON_MALLOC_ERROR;
	  fast_json_value_free (json, v);
	  return NULL;
	}
      }
      if (fast_json_check_string (json, save, save + json->n_save, out) !=
	  FAST_JSON_OK) {
	if (out != &name[0]) {
	  (*json->my_free) (out);
	}
	fast_json_value_free (json, v);
	return NULL;
      }
      c = fast_json_getc (json);
      if (c != '"') {
	fast_json_store_error (json, FAST_JSON_STRING_END_ERROR, save);
	if (out != &name[0]) {
	  (*json->my_free) (out);
	}
	fast_json_value_free (json, v);
	return NULL;
      }
      if (fast_json_skip_whitespace (json, &c) != FAST_JSON_OK) {
	if (out != &name[0]) {
	  (*json->my_free) (out);
	}
	fast_json_value_free (json, v);
	return NULL;
      }
      if (c != ':') {
	fast_json_store_error (json, FAST_JSON_OBJECT_SEPERATOR_ERROR, "");
	if (out != &name[0]) {
	  (*json->my_free) (out);
	}
	fast_json_value_free (json, v);
	return NULL;
      }
      if (fast_json_skip_whitespace (json, &c) != FAST_JSON_OK) {
	if (out != &name[0]) {
	  (*json->my_free) (out);
	}
	fast_json_value_free (json, v);
	return NULL;
      }
      n = fast_json_parse_value (json, c);
      if (n == NULL) {
	if (out != &name[0]) {
	  (*json->my_free) (out);
	}
	fast_json_value_free (json, v);
	return NULL;
      }
      if (fast_json_add_object_end (json, v, out, n) != FAST_JSON_OK) {
	fast_json_value_free (json, n);
	if (out != &name[0]) {
	  (*json->my_free) (out);
	}
	fast_json_value_free (json, v);
	return NULL;
      }
      if (out != &name[0]) {
	(*json->my_free) (out);
      }
      if (fast_json_skip_whitespace (json, &c) != FAST_JSON_OK) {
	fast_json_value_free (json, v);
	return NULL;
      }
      if (c != ',') {
	break;
      }
      if (fast_json_skip_whitespace (json, &c) != FAST_JSON_OK) {
	fast_json_value_free (json, v);
	return NULL;
      }
    }
    if (c != '}') {
      fast_json_store_error (json, FAST_JSON_OBJECT_END_ERROR, "");
      fast_json_value_free (json, v);
      return NULL;
    }
    break;
  default:
    save = fast_json_ungetc_save (json, 0);
    fast_json_store_error (json, FAST_JSON_VALUE_ERROR, save);
    return NULL;
  }
  return v;
}

FAST_JSON_TYPE
fast_json_create (fast_json_malloc_type malloc_fn,
		  fast_json_realloc_type realloc_fn,
		  fast_json_free_type free_fn)
{
  FAST_JSON_TYPE json =
    (FAST_JSON_TYPE) (malloc_fn ? malloc_fn : malloc) (sizeof (*json));

  if (json) {
    memset (json, 0, sizeof (*json));
    json->my_malloc = malloc_fn ? malloc_fn : malloc;
    json->my_realloc = malloc_fn ? realloc_fn : realloc;
    json->my_free = free_fn ? free_fn : free;
    json->decimal_point = *localeconv ()->decimal_point;
  }
  return json;
}

FAST_JSON_ERROR_ENUM
fast_json_options (FAST_JSON_TYPE json, unsigned int value)
{
  FAST_JSON_ERROR_ENUM retval = FAST_JSON_VALUE_ERROR;

  if (json) {
    json->options = value;
    retval = FAST_JSON_OK;
  }
  return retval;
}

FAST_JSON_ERROR_ENUM
fast_json_max_reuse (FAST_JSON_TYPE json, size_t n)
{
  FAST_JSON_ERROR_ENUM retval = FAST_JSON_VALUE_ERROR;

  if (json) {
    json->max_reuse = n;
    retval = FAST_JSON_OK;
  }
  return retval;
}

void
fast_json_free (FAST_JSON_TYPE json)
{
  if (json) {
    size_t i;
    FAST_JSON_BIG_TYPE *b;

    while (json->json_reuse) {
      FAST_JSON_DATA_TYPE next = json->json_reuse->u.next;

      if (json->json_reuse->index == 0xFFFFFFFFu) {
	(*json->my_free) (json->json_reuse);
      }
      json->json_reuse = next;
    }
    b = json->big_malloc_free;
    while (b) {
      FAST_JSON_BIG_TYPE *next = b->data;

      b->data = NULL;
      b = next;
    }
    for (i = 0; i < json->n_big_malloc; i++) {
      (*json->my_free) (json->big_malloc[i].data);
    }
    (*json->my_free) (json->big_malloc);
    (*json->my_free) (json->save);
    (*json->my_free) (json);
  }
}

size_t
fast_json_parser_line (FAST_JSON_TYPE json)
{
  return json ? json->line : 0;
}

size_t
fast_json_parser_column (FAST_JSON_TYPE json)
{
  return json ? json->column : 0;
}

size_t
fast_json_parser_position (FAST_JSON_TYPE json)
{
  return json ? json->position : 0;
}

FAST_JSON_ERROR_ENUM
fast_json_parser_error (FAST_JSON_TYPE json)
{
  return json ? json->error : FAST_JSON_OK;
}

const char *
fast_json_error_str (FAST_JSON_ERROR_ENUM error)
{
  switch (error) {
  case FAST_JSON_OK:
    return ("OK");
  case FAST_JSON_MALLOC_ERROR:
    return ("Malloc error");
  case FAST_JSON_COMMENT_ERROR:
    return ("Comment error");
  case FAST_JSON_NUMBER_ERROR:
    return ("Number error");
  case FAST_JSON_CONTROL_CHARACTER_ERROR:
    return ("Control character error");
  case FAST_JSON_ESCAPE_CHARACTER_ERROR:
    return ("Escape character error");
  case FAST_JSON_UTF8_ERROR:
    return ("UTF8 character error");
  case FAST_JSON_UNICODE_ERROR:
    return ("Unicode error");
  case FAST_JSON_UNICODE_ESCAPE_ERROR:
    return ("Unicode escape error");
  case FAST_JSON_STRING_START_ERROR:
    return ("String start error");
  case FAST_JSON_STRING_END_ERROR:
    return ("String end error");
  case FAST_JSON_VALUE_ERROR:
    return ("Value error");
  case FAST_JSON_ARRAY_END_ERROR:
    return ("Array end error");
  case FAST_JSON_OBJECT_SEPERATOR_ERROR:
    return ("Object seperator error");
  case FAST_JSON_OBJECT_END_ERROR:
    return ("Object end error");
  case FAST_JSON_PARSE_ERROR:
    return ("Parse error");
  case FAST_JSON_NO_DATA_ERROR:
    return ("No data error");
  case FAST_JSON_INDEX_ERROR:
    return ("Index error");
  case FAST_JSON_LOOP_ERROR:
    return ("Loop error");
  }
  return NULL;
}

const char *
fast_json_parser_error_str (FAST_JSON_TYPE json)
{
  return json ? (const char *) json->error_str : "";
}

static FAST_JSON_DATA_TYPE
fast_json_parse_all (FAST_JSON_TYPE json)
{
  int c;
  FAST_JSON_DATA_TYPE v = NULL;
  unsigned int save_options = json->options;

  json->options |= FAST_JSON_NO_CHECK_LOOP;
  json->error = FAST_JSON_OK;
  json->error_str[0] = '\0';
  json->line = 1;
  json->column = 0;
  json->position = 0;
  json->last_char = 0;
  json->decimal_point = *localeconv ()->decimal_point;
  if (fast_json_skip_whitespace (json, &c) == FAST_JSON_OK) {
    if (c != FAST_JSON_EOF) {
      v = fast_json_parse_value (json, c);
      if (v) {
	if ((json->options & FAST_JSON_NO_EOF_CHECK) == 0) {
	  if (fast_json_skip_whitespace (json, &c) == FAST_JSON_OK) {
	    fast_json_ungetc (json, c);
	    if (c != FAST_JSON_EOF) {
	      fast_json_store_error (json, FAST_JSON_OBJECT_END_ERROR, "");
	      fast_json_value_free (json, v);
	      v = NULL;
	    }
	  }
	  else {
	    fast_json_value_free (json, v);
	    v = NULL;
	  }
	}
      }
    }
    else {
      json->error = FAST_JSON_NO_DATA_ERROR;
    }
  }
  json->options = save_options;
  return v;
}

FAST_JSON_DATA_TYPE
fast_json_parse_string (FAST_JSON_TYPE json, const char *json_str)
{
  FAST_JSON_DATA_TYPE v = NULL;

  if (json && json_str) {
    json->getc = fast_json_getc_string;
    json->u.str.string = json_str;
    json->u.str.pos = 0;
    v = fast_json_parse_all (json);
  }
  return v;
}

FAST_JSON_DATA_TYPE
fast_json_parse_string_len (FAST_JSON_TYPE json, const char *json_str,
			    size_t len)
{
  FAST_JSON_DATA_TYPE v = NULL;

  if (json && json_str) {
    json->getc = fast_json_getc_string_len;
    json->u.str.string = json_str;
    json->u.str.pos = 0;
    json->u.str.len = len;
    v = fast_json_parse_all (json);
  }
  return v;
}

FAST_JSON_DATA_TYPE
fast_json_parse_file (FAST_JSON_TYPE json, FILE * fp)
{
  FAST_JSON_DATA_TYPE v = NULL;

  if (json && fp) {
    json->getc = fast_json_getc_file;
    json->u.fp = fp;
    v = fast_json_parse_all (json);
    if (v && json->last_char) {
      ungetc (json->last_char, fp);
    }
  }
  return v;
}

FAST_JSON_DATA_TYPE
fast_json_parse_file_name (FAST_JSON_TYPE json, const char *name)
{
  FAST_JSON_DATA_TYPE v = NULL;

  if (json && name) {
    FILE *fp = fopen (name, "r");

    if (fp) {
      json->getc = fast_json_getc_file;
      json->u.fp = fp;
      v = fast_json_parse_all (json);
      fclose (fp);
    }
  }
  return v;
}

FAST_JSON_DATA_TYPE
fast_json_parse_fd (FAST_JSON_TYPE json, int fd)
{
  FAST_JSON_DATA_TYPE v = NULL;

  if (json) {
    json->getc = fast_json_getc_fd;
    json->u.fd.fd = fd;
    json->u.fd.pos = 0;
    json->u.fd.len = 0;
    v = fast_json_parse_all (json);
  }
  return v;
}

static void
fast_json_store_error2 (FAST_JSON_TYPE json, FAST_JSON_ERROR_ENUM error,
			const char *cp, const char *sep)
{
  int len = sizeof (json->error_str) - 1;
  char *rp = json->error_str;
  const char *s;

  json->error = error;
  while (len-- && *cp) {
    *rp++ = *cp++;
  }
  *rp = '\0';
  rp = json->error_str;
  while (*rp) {
    s = sep;
    while (*s) {
      if (*rp == *s) {
	*rp = '\0';
	break;
      }
      s++;
    }
    if (*rp == '\0') {
      break;
    }
    rp++;
  }
  s = json->json_str;
  json->line = 1;
  json->column = 0;
  json->position = 0;
  while (s != cp) {
    json->position++;
    json->column += fast_json_utf8_size[*s & 0xFFu] != 0;
    if (*s == '\n') {
      json->line++;
      json->column = 0;
    }
    s++;
  }
}

static FAST_JSON_ERROR_ENUM
fast_json_skip_whitespace2 (FAST_JSON_TYPE json, const char **buf)
{
  FAST_JSON_ERROR_ENUM retval = FAST_JSON_OK;
  const char *cp = *buf;

  for (;;) {
    while (fast_json_isspace (*cp)) {
      cp++;
    }
    if (*cp == '/') {
      if (cp[1] == '*') {
	cp += 2;
	while (*cp) {
	  if (*cp == '*' && cp[1] == '/') {
	    cp += 2;
	    break;
	  }
	  cp++;
	}
      }
      else if (cp[1] == '/') {
	cp += 2;
	while (*cp) {
	  if (*cp == '\n') {
	    cp++;
	    break;
	  }
	  cp++;
	}
      }
      else {
	fast_json_store_error2 (json, FAST_JSON_COMMENT_ERROR, cp, ":,]}");
	retval = FAST_JSON_COMMENT_ERROR;
	break;
      }
    }
    else {
      break;
    }
  }
  *buf = cp;
  return retval;
}

static FAST_JSON_DATA_TYPE
fast_json_parse_value2 (FAST_JSON_TYPE json, const char **buf)
{
  const char *value = *buf;
  FAST_JSON_DATA_TYPE v;

  switch (*value) {
  case 'n':			/* FALLTHRU */
  case 'N':
    if (strncmp (value, "null", strlen ("null")) == 0) {
      value += strlen ("null");
      v = fast_json_create_null (json);
    }
    else if ((json->options & FAST_JSON_INF_NAN) != 0 &&
	     strncasecmp (value, "nan", strlen ("nan")) == 0) {
      const char *cp;
      const char *save = value;

      value += strlen ("nan");
      cp = value;
      if (*cp == '(') {
	cp++;
	while (fast_json_isalpha (*cp) || fast_json_isdigit (*cp) ||
	       *cp == '_') {
	  cp++;
	}
	if (*cp != ')') {
	  fast_json_store_error2 (json, FAST_JSON_NUMBER_ERROR, save, ":,]}");
	  return NULL;
	}
	value = cp + 1;
      }
      v = fast_json_create_double_value (json, fast_json_nan (0));
    }
    else {
      fast_json_store_error2 (json, FAST_JSON_VALUE_ERROR, value, ":,]}");
      return NULL;
    }
    break;
  case 'f':
    if (strncmp (value, "false", strlen ("false")) == 0) {
      value += strlen ("false");
      v = fast_json_create_false (json);
    }
    else {
      fast_json_store_error2 (json, FAST_JSON_VALUE_ERROR, value, ":,]}");
      return NULL;
    }
    break;
  case 't':
    if (strncmp (value, "true", strlen ("true")) == 0) {
      value += strlen ("true");
      v = fast_json_create_true (json);
    }
    else {
      fast_json_store_error2 (json, FAST_JSON_VALUE_ERROR, value, ":,]}");
      return NULL;
    }
    break;
  case 'i':			/* FALLTHRU */
  case 'I':
    if ((json->options & FAST_JSON_INF_NAN) != 0 &&
	strncasecmp (value, "inf", 3) == 0) {
      value += strlen ("inf");
      if (strncasecmp (value, "inity", strlen ("inity")) == 0) {
	value += strlen ("inity");
      }
      v = fast_json_create_double_value (json, fast_json_inf (0));
    }
    else {
      fast_json_store_error2 (json, FAST_JSON_VALUE_ERROR, value, ":,]}");
      return NULL;
    }
    break;
  case '"':
    {
      char str[8];
      char *out = &str[0];
      const char *save;

      value++;
      save = value;
      while (*value && *value != '"') {
	if (*value++ == '\\') {
	  value++;
	}
      }
      if ((value - save) >= sizeof (str)) {
	out = (char *) (*json->my_malloc) ((value - save) + 1);
	if (out == NULL) {
	  fast_json_store_error2 (json, FAST_JSON_MALLOC_ERROR, value,
				  ":,]}");
	  return NULL;
	}
      }
      if (fast_json_check_string (json, save, value, out) != FAST_JSON_OK) {
	fast_json_store_error2 (json, json->error, save, ":,]}");
	if (out != &str[0]) {
	  (*json->my_free) (out);
	}
	return NULL;
      }
      if (*value != '"') {
	fast_json_store_error2 (json, FAST_JSON_STRING_END_ERROR, value,
				":,]}");
	if (out != &str[0]) {
	  (*json->my_free) (out);
	}
	return NULL;
      }
      value++;
      v = fast_json_data_create (json);
      if (v) {
	v->type = FAST_JSON_STRING;
	v->used = 0;
	if (out != &str[0]) {
	  v->is_str = 0;
	  v->u.string_value = out;
	}
	else {
	  v->is_str = 1;
	  strcpy (v->u.i_string_value, out);
	}
      }
    }
    break;
  case '+':			/* FALLTHRU */
  case '-':			/* FALLTHRU */
  case '0':			/* FALLTHRU */
  case '1':			/* FALLTHRU */
  case '2':			/* FALLTHRU */
  case '3':			/* FALLTHRU */
  case '4':			/* FALLTHRU */
  case '5':			/* FALLTHRU */
  case '6':			/* FALLTHRU */
  case '7':			/* FALLTHRU */
  case '8':			/* FALLTHRU */
  case '9':
    {
      unsigned int integer = 1;
      unsigned int hex = 0;
      unsigned int sign = 0;
      const char *save;
      char *end;
      const char *dp = NULL;

      save = value;
      if (*value == '+') {
	if ((json->options & FAST_JSON_INF_NAN) != 0) {
	  value++;
	}
      }
      else if (*value == '-') {
	sign = 1;
	value++;
      }
      if (*value == '0') {
	value++;
	if ((json->options & FAST_JSON_ALLOW_OCT_HEX) != 0) {
	  if (*value == 'x' || *value == 'X') {
	    hex = 1;
	    value++;
	    while (fast_json_isxdigit (*value)) {
	      value++;
	    }
	  }
	  else {
	    while (*value >= '0' && *value <= '7') {
	      value++;
	    }
	  }
	}
      }
      else if (fast_json_isdigit (*value)) {
	do {
	  value++;
	} while (fast_json_isdigit (*value));
      }
      else {
	if ((json->options & FAST_JSON_INF_NAN) != 0) {
	  if (strncasecmp (value, "inf", strlen ("inf")) == 0) {
	    value += strlen ("inf");
	    if (strncasecmp (value, "inity", strlen ("inity")) == 0) {
	      value += strlen ("inity");
	    }
	    v = fast_json_create_double_value (json, fast_json_inf (sign));
	    break;
	  }
	  else if (strncasecmp (value, "nan", strlen ("nan")) == 0) {
	    const char *cp;

	    value += strlen ("nan");
	    cp = value;
	    if (*cp == '(') {
	      cp++;
	      while (fast_json_isalpha (*cp) || fast_json_isdigit (*cp) ||
		     *cp == '_') {
		cp++;
	      }
	      if (*cp != ')') {
		fast_json_store_error2 (json, FAST_JSON_NUMBER_ERROR, save,
					":,]}");
		return NULL;
	      }
	      value = cp + 1;
	    }
	    v = fast_json_create_double_value (json, fast_json_nan (sign));
	    break;
	  }
	}
	fast_json_store_error2 (json, FAST_JSON_NUMBER_ERROR, save, ":,]}");
	return NULL;
      }
      if (*value == '.') {
	dp = value++;
	integer = 0;
	if (hex) {
	  if (fast_json_isxdigit (*value)) {
	    do {
	      value++;
	    } while (fast_json_isxdigit (*value));
	  }
	  else {
	    fast_json_store_error2 (json, FAST_JSON_NUMBER_ERROR, save,
				    ":,]}");
	    return NULL;
	  }
	}
	else {
	  if (fast_json_isdigit (*value)) {
	    do {
	      value++;
	    } while (fast_json_isdigit (*value));
	  }
	  else {
	    fast_json_store_error2 (json, FAST_JSON_NUMBER_ERROR, save,
				    ":,]}");
	    return NULL;
	  }
	}
      }
      if (*value == 'e' || *value == 'E' ||
	  (hex && (*value == 'p' || *value == 'P'))) {
	integer = 0;
	value++;
	if (*value == '+' || *value == '-') {
	  value++;
	}
	if (fast_json_isdigit (*value)) {
	  do {
	    value++;
	  } while (fast_json_isdigit (*value));
	}
	else {
	  fast_json_store_error2 (json, FAST_JSON_NUMBER_ERROR, save, ":,]}");
	  return NULL;
	}
      }
      if ((json->options & FAST_JSON_PARSE_INT_AS_DOUBLE) == 0 && integer) {
	fast_json_int_64 int_value;

	errno = 0;
#if USE_FAST_CONVERT
	int_value = fast_strtos64 (save, &end);
#else
#if __WORDSIZE == 64
	int_value = strtol (save, &end, 0);
#else
	int_value = strtoll (save, &end, 0);
#endif
#endif
	if (end == value && errno != ERANGE) {
	  v = fast_json_create_integer_value (json, int_value);
	  break;
	}
      }
      {
	double n;

	if (dp && json->decimal_point != '.') {
	  char e;
	  char b[100];
	  char *s = &b[0];

	  if ((value - save) >= sizeof (b)) {
	    s = (char *) (*json->my_malloc) ((value - save) + 1);
	    if (s == NULL) {
	      fast_json_store_error2 (json, FAST_JSON_MALLOC_ERROR, save,
				      ":,]}");
	      return NULL;
	    }
	  }
	  memcpy (s, save, value - save);
	  s[value - save] = '\0';
	  s[dp - save] = json->decimal_point;
	  errno = 0;
#if USE_FAST_CONVERT
	  n = fast_strtod (s, &end);
#else
	  n = strtod (s, &end);
#endif
	  e = *end;
	  if (s != &b[0]) {
	    (*json->my_free) (s);
	  }
	  if (e != '\0' || errno == ERANGE) {
	    fast_json_store_error2 (json, FAST_JSON_NUMBER_ERROR, save,
				    ":,]}");
	    return NULL;
	  }
	}
	else {
	  errno = 0;
#if USE_FAST_CONVERT
	  n = fast_strtod (save, &end);
#else
	  n = strtod (save, &end);
#endif
	  if (end != value || errno == ERANGE) {
	    fast_json_store_error2 (json, FAST_JSON_NUMBER_ERROR, save,
				    ":,]}");
	    return NULL;
	  }
	}
	v = fast_json_create_double_value (json, n);
      }
    }
    break;
  case '[':
    value++;
    if (fast_json_skip_whitespace2 (json, &value) != FAST_JSON_OK) {
      return NULL;
    }
    v = fast_json_create_array (json);
    if (v == NULL) {
      fast_json_store_error2 (json, FAST_JSON_MALLOC_ERROR, value, ":,]}");
      return NULL;
    }
    if (*value == ']') {
      value++;
      break;
    }
    for (;;) {
      FAST_JSON_DATA_TYPE n = fast_json_parse_value2 (json, &value);

      if (n == NULL) {
	fast_json_value_free (json, v);
	return NULL;
      }
      if (fast_json_add_array_end (json, v, n) != FAST_JSON_OK) {
	fast_json_value_free (json, n);
	fast_json_value_free (json, v);
	return NULL;
      }
      if (fast_json_skip_whitespace2 (json, &value) != FAST_JSON_OK) {
	fast_json_value_free (json, v);
	return NULL;
      }
      if (*value != ',') {
	break;
      }
      value++;
      if (fast_json_skip_whitespace2 (json, &value) != FAST_JSON_OK) {
	fast_json_value_free (json, v);
	return NULL;
      }
    }
    if (*value != ']') {
      fast_json_store_error2 (json, FAST_JSON_ARRAY_END_ERROR, value, ":,]}");
      fast_json_value_free (json, v);
      return NULL;
    }
    value++;
    break;
  case '{':
    {
      value++;
      if (fast_json_skip_whitespace2 (json, &value) != FAST_JSON_OK) {
	return NULL;
      }
      v = fast_json_create_object (json);
      if (v == NULL) {
	fast_json_store_error2 (json, FAST_JSON_MALLOC_ERROR, value, ":,]}");
	return NULL;
      }
      if (*value == '}') {
	value++;
	break;
      }
      for (;;) {
	const char *save;
	char name[16];
	char *out = &name[0];
	FAST_JSON_DATA_TYPE n;

	if (*value++ != '"') {
	  fast_json_store_error2 (json, FAST_JSON_STRING_START_ERROR, value,
				  ":,]}");
	  fast_json_value_free (json, v);
	  return NULL;
	}
	save = value;
	while (*value && *value != '"') {
	  if (*value++ == '\\') {
	    value++;
	  }
	}
	if ((value - save) >= sizeof (name)) {
	  out = (char *) (*json->my_malloc) ((value - save) + 1);
	  if (out == NULL) {
	    fast_json_store_error2 (json, FAST_JSON_MALLOC_ERROR, save,
				    ":,]}");
	    fast_json_value_free (json, v);
	    return NULL;
	  }
	}
	if (fast_json_check_string (json, save, value, out) != FAST_JSON_OK) {
	  fast_json_store_error2 (json, json->error, save, ":,]}");
	  if (out != &name[0]) {
	    (*json->my_free) (out);
	  }
	  fast_json_value_free (json, v);
	  return NULL;
	}
	if (*value != '"') {
	  fast_json_store_error2 (json, FAST_JSON_STRING_END_ERROR, save,
				  ":,]}");
	  if (out != &name[0]) {
	    (*json->my_free) (out);
	  }
	  fast_json_value_free (json, v);
	  return NULL;
	}
	value++;
	if (fast_json_skip_whitespace2 (json, &value) != FAST_JSON_OK) {
	  if (out != &name[0]) {
	    (*json->my_free) (out);
	  }
	  fast_json_value_free (json, v);
	  return NULL;
	}
	if (*value != ':') {
	  fast_json_store_error2 (json, FAST_JSON_OBJECT_SEPERATOR_ERROR,
				  value, ":,]}");
	  if (out != &name[0]) {
	    (*json->my_free) (out);
	  }
	  fast_json_value_free (json, v);
	  return NULL;
	}
	value++;
	if (fast_json_skip_whitespace2 (json, &value) != FAST_JSON_OK) {
	  if (out != &name[0]) {
	    (*json->my_free) (out);
	  }
	  fast_json_value_free (json, v);
	  return NULL;
	}
	n = fast_json_parse_value2 (json, &value);
	if (n == NULL) {
	  if (out != &name[0]) {
	    (*json->my_free) (out);
	  }
	  fast_json_value_free (json, v);
	  return NULL;
	}
	if (fast_json_add_object_end (json, v, out, n) != FAST_JSON_OK) {
	  fast_json_value_free (json, n);
	  if (out != &name[0]) {
	    (*json->my_free) (out);
	  }
	  fast_json_value_free (json, v);
	  return NULL;
	}
	if (out != &name[0]) {
	  (*json->my_free) (out);
	}
	if (fast_json_skip_whitespace2 (json, &value) != FAST_JSON_OK) {
	  fast_json_value_free (json, v);
	  return NULL;
	}
	if (*value != ',') {
	  break;
	}
	value++;
	if (fast_json_skip_whitespace2 (json, &value) != FAST_JSON_OK) {
	  fast_json_value_free (json, v);
	  return NULL;
	}
      }
      if (*value != '}') {
	fast_json_store_error2 (json, FAST_JSON_OBJECT_END_ERROR, value,
				":,]}");
	fast_json_value_free (json, v);
	return NULL;
      }
      value++;
    }
    break;
  default:
    fast_json_store_error2 (json, FAST_JSON_VALUE_ERROR, value, ":,]}");
    return NULL;
  }
  *buf = value;
  return v;
}

FAST_JSON_DATA_TYPE
fast_json_parse_string2 (FAST_JSON_TYPE json, const char *json_str)
{
  FAST_JSON_DATA_TYPE v = NULL;
  unsigned int save_options = json->options;

  json->options |= FAST_JSON_NO_CHECK_LOOP;
  json->json_str = json_str;
  json->error = FAST_JSON_OK;
  json->error_str[0] = '\0';
  json->line = 1;
  json->column = 0;
  json->position = 0;
  json->decimal_point = *localeconv ()->decimal_point;
  if (fast_json_skip_whitespace2 (json, &json_str) == FAST_JSON_OK) {
    if (*json_str != '\0') {
      v = fast_json_parse_value2 (json, &json_str);
      if (v) {
	if ((json->options & FAST_JSON_NO_EOF_CHECK) == 0) {
	  if (fast_json_skip_whitespace2 (json, &json_str) == FAST_JSON_OK) {
	    if (*json_str != '\0') {
	      fast_json_store_error2 (json, FAST_JSON_OBJECT_END_ERROR,
				      json_str, ":,]}");
	      fast_json_value_free (json, v);
	      v = NULL;
	    }
	  }
	  else {
	    fast_json_value_free (json, v);
	    v = NULL;
	  }
	}
	else {
	  const char *s = json->json_str;

	  json->line = 1;
	  json->column = 0;
	  json->position = 0;
	  while (s != json_str) {
	    json->position++;
	    json->column += fast_json_utf8_size[*s & 0xFFu] != 0;
	    if (*s == '\n') {
	      json->line++;
	      json->column = 0;
	    }
	    s++;
	  }
	}
      }
    }
    else {
      json->error = FAST_JSON_NO_DATA_ERROR;
    }
  }
  json->options = save_options;
  return v;
}

int
fast_json_value_equal (FAST_JSON_DATA_TYPE value1, FAST_JSON_DATA_TYPE value2)
{
  if (value1 && value2 && value1->type == value2->type) {
    switch (value1->type) {
    case FAST_JSON_OBJECT:
      {
	FAST_JSON_OBJECT_TYPE *o1 = value1->u.object;
	FAST_JSON_OBJECT_TYPE *o2 = value2->u.object;

	if (o1 && o2 && o1->len == o2->len) {
	  unsigned int i;

	  for (i = 0; i < o1->len; i++) {
	    if (fast_json_value_equal (o1->data[i].value,
				       o2->data[i].value) == 0) {
	      return 0;
	    }
	  }
	}
	else if ((o1 == NULL && o2) || (o1 && o2 == NULL) ||
		 (o1 && o2 && o1->len != o2->len)) {
	  return 0;
	}
      }
      break;
    case FAST_JSON_ARRAY:
      {
	FAST_JSON_ARRAY_TYPE *a1 = value1->u.array;
	FAST_JSON_ARRAY_TYPE *a2 = value2->u.array;

	if (a1 && a2 && a1->len == a2->len) {
	  unsigned int i;

	  for (i = 0; i < a1->len; i++) {
	    if (fast_json_value_equal (a1->values[i], a2->values[i]) == 0) {
	      return 0;
	    }
	  }
	}
	else if ((a1 == NULL && a2) || (a1 && a2 == NULL) ||
		 (a1 && a2 && a1->len != a2->len)) {
	  return 0;
	}
      }
      break;
    case FAST_JSON_INTEGER:
      if (value1->u.int_value != value2->u.int_value) {
	return 0;
      }
      break;
    case FAST_JSON_DOUBLE:
      if (value1->u.double_value != value2->u.double_value) {
	return 0;
      }
      break;
    case FAST_JSON_STRING:
      {
	char *str1 = value1->is_str ? &value1->u.i_string_value[0]
	  : value1->u.string_value;
	char *str2 = value2->is_str ? &value2->u.i_string_value[0]
	  : value2->u.string_value;

	if (strcmp (str1, str2) != 0) {
	  return 0;
	}
      }
      break;
    case FAST_JSON_BOOLEAN:
      if (value1->u.boolean_value != value2->u.boolean_value) {
	return 0;
      }
      break;
    case FAST_JSON_NULL:
      break;
    }
    return 1;
  }
  if ((value1 == NULL && value2) || (value1 && value2 == NULL) ||
      (value1 && value2 && value1->type != value2->type)) {
    return 0;
  }
  return 1;
}

FAST_JSON_DATA_TYPE
fast_json_value_copy (FAST_JSON_TYPE json, FAST_JSON_DATA_TYPE value)
{
  FAST_JSON_DATA_TYPE v = NULL;

  if (json && value) {
    FAST_JSON_DATA_TYPE nv;

    switch (value->type) {
    case FAST_JSON_OBJECT:
      {
	FAST_JSON_OBJECT_TYPE *o = value->u.object;

	v = fast_json_create_object (json);
	if (v && o) {
	  unsigned int i;

	  for (i = 0; i < o->len; i++) {
	    nv = fast_json_value_copy (json, o->data[i].value);
	    if (nv) {
	      if (fast_json_add_object_end (json, v, o->data[i].name, nv) !=
		  FAST_JSON_OK) {
		fast_json_value_free (json, v);
		v = NULL;
		break;
	      }
	    }
	    else {
	      fast_json_value_free (json, v);
	      v = NULL;
	      break;
	    }
	  }
	}
      }
      break;
    case FAST_JSON_ARRAY:
      {
	FAST_JSON_ARRAY_TYPE *a = value->u.array;

	v = fast_json_create_array (json);
	if (v && a) {
	  unsigned int i;

	  for (i = 0; i < a->len; i++) {
	    nv = fast_json_value_copy (json, a->values[i]);
	    if (nv) {
	      if (fast_json_add_array_end (json, v, nv) != FAST_JSON_OK) {
		fast_json_value_free (json, v);
		v = NULL;
		break;
	      }
	    }
	    else {
	      fast_json_value_free (json, v);
	      v = NULL;
	      break;
	    }
	  }
	}
      }
      break;
    case FAST_JSON_INTEGER:
      v = fast_json_create_integer_value (json, value->u.int_value);
      break;
    case FAST_JSON_DOUBLE:
      v = fast_json_create_double_value (json, value->u.double_value);
      break;
    case FAST_JSON_STRING:
      if (value->is_str) {
	v = fast_json_create_string (json, &value->u.i_string_value[0]);
      }
      else {
	v = fast_json_create_string (json, value->u.string_value);
      }
      break;
    case FAST_JSON_BOOLEAN:
      v = fast_json_create_boolean_value (json, value->u.boolean_value);
      break;
    case FAST_JSON_NULL:
      v = fast_json_create_null (json);
      break;
    }
  }
  return v;
}

void
fast_json_value_free (FAST_JSON_TYPE json, FAST_JSON_DATA_TYPE value)
{
  if (json && value) {
    switch (value->type) {
    case FAST_JSON_OBJECT:
      {
	FAST_JSON_OBJECT_TYPE *o = value->u.object;

	if (o) {
	  unsigned int i;

	  for (i = 0; i < o->len; i++) {
	    (*json->my_free) (o->data[i].name);
	    fast_json_value_free (json, o->data[i].value);
	  }
	}
	(*json->my_free) (o);
	fast_json_data_free (json, value);
      }
      break;
    case FAST_JSON_ARRAY:
      {
	FAST_JSON_ARRAY_TYPE *a = value->u.array;

	if (a) {
	  unsigned int i;

	  for (i = 0; i < a->len; i++) {
	    fast_json_value_free (json, a->values[i]);
	  }
	}
	(*json->my_free) (a);
	fast_json_data_free (json, value);
      }
      break;
    case FAST_JSON_INTEGER:
      fast_json_data_free (json, value);
      break;
    case FAST_JSON_DOUBLE:
      fast_json_data_free (json, value);
      break;
    case FAST_JSON_STRING:
      if (value->is_str == 0) {
	(*json->my_free) (value->u.string_value);
      }
      fast_json_data_free (json, value);
      break;
    case FAST_JSON_BOOLEAN:
      fast_json_data_free (json, value);
      break;
    case FAST_JSON_NULL:
      fast_json_data_free (json, value);
      break;
    }
  }
}

static int
fast_json_puts_string (FAST_JSON_TYPE json, const char *str, unsigned int len)
{
  if ((json->u.buf.len + len) > json->u.buf.max) {
    unsigned int new_max = json->u.buf.max +
      ((len + (FAST_JSON_BUFFER_SIZE - 1)) /
       FAST_JSON_BUFFER_SIZE) * FAST_JSON_BUFFER_SIZE;
    char *new_txt;

    new_txt = (char *) (*json->my_realloc) (json->u.buf.txt, new_max);
    if (new_txt == NULL) {
      return -1;
    }
    json->u.buf.max = new_max;
    json->u.buf.txt = new_txt;
  }
  memcpy (json->u.buf.txt + json->u.buf.len, str, len);
  json->u.buf.len += len;
  return 0;
}

static int
fast_json_puts_string_len (FAST_JSON_TYPE json, const char *str,
			   unsigned int len)
{
  unsigned int n = json->u.buf.max - json->u.buf.len;
  unsigned int size = len;

  if (size > n) {
    size = n;
  }
  if ((json->u.buf.len + size) <= json->u.buf.max) {
    memcpy (json->u.buf.txt + json->u.buf.len, str, size);
  }
  json->u.buf.len += len;
  return 0;
}

static int
fast_json_puts_file (FAST_JSON_TYPE json, const char *str, unsigned int len)
{
  return fwrite (str, 1, len, json->u.fp) == len ? 0 : -1;
}

static int
fast_json_puts_fd (FAST_JSON_TYPE json, const char *str, unsigned int len)
{
  return write (json->u.fd.fd, str, len) == len ? 0 : -1;
}

static int
fast_json_puts (FAST_JSON_TYPE json, const char *str, unsigned int len)
{
  if (json->puts_len + len <= sizeof (json->puts_buf)) {
    memcpy (json->puts_buf + json->puts_len, str, len);
    json->puts_len += len;
  }
  else {
    while (len) {
      unsigned int size = sizeof (json->puts_buf) - json->puts_len;

      if (size > len) {
	size = len;
      }
      memcpy (json->puts_buf + json->puts_len, str, size);
      json->puts_len += size;
      str += size;
      len -= size;
      if (json->puts_len == sizeof (json->puts_buf)) {
	if ((*json->puts) (json, json->puts_buf, json->puts_len)) {
	  return -1;
	}
	json->puts_len = 0;
      }
    }
  }
  return 0;
}

static int
fast_json_last_puts (FAST_JSON_TYPE json, const char *str, unsigned int len)
{
  if (fast_json_puts (json, str, len)) {
    return -1;
  }
  if (json->puts_len) {
    return (*json->puts) (json, json->puts_buf, json->puts_len);
  }
  return 0;
}

static int
fast_json_print_string_value (FAST_JSON_TYPE json, const char *s)
{
  if (fast_json_puts (json, "\"", 1)) {
    return -1;
  }
  if (s) {
    const char *last = NULL;

    while (*s) {
      static const char special[256] = {
	1, 1, 1, 1, 1, 1, 1, 1, 'b', 't', 'n', 1, 'f', 'r', 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '/',
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1
      };

      if (special[*s & 0xFFu]) {
	static const char hex[16] = {
	  '0', '1', '2', '3', '4', '5', '6', '7',
	  '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'
	};
	char v[12];
	unsigned int size;

	if (last) {
	  if (fast_json_puts (json, last, s - last)) {
	    return -1;
	  }
	  last = NULL;
	}
	if ((*s & 0x80u) != 0) {
	  size = 0;
	  if (json->options & FAST_JSON_PRINT_UNICODE_ESCAPE) {
	    unsigned char u = (unsigned char) *s;
	    unsigned int uc;

	    switch (fast_json_utf8_size[u]) {
	    case 0:		/* FALLTHRU */
	    case 1:
	      /* Should never happen */
	      break;
	    case 2:
	      if (s[1]) {
		uc = ((s[0] & 0x1Fu) << 6) | (s[1] & 0x3Fu);
		v[0] = '\\';
		v[1] = 'u';
		v[2] = hex[(uc >> 12) & 0xFu];
		v[3] = hex[(uc >> 8) & 0xFu];
		v[4] = hex[(uc >> 4) & 0xFu];
		v[5] = hex[(uc >> 0) & 0xFu];
		size = 6;
		s += 2;
	      }
	      break;
	    case 3:
	      if (s[1] && s[2]) {
		uc =
		  ((s[0] & 0x0Fu) << 12) | ((s[1] & 0x3Fu) << 6) | (s[2] &
								    0x3Fu);
		v[0] = '\\';
		v[1] = 'u';
		v[2] = hex[(uc >> 12) & 0xFu];
		v[3] = hex[(uc >> 8) & 0xFu];
		v[4] = hex[(uc >> 4) & 0xFu];
		v[5] = hex[(uc >> 0) & 0xFu];
		size = 6;
		s += 3;
	      }
	      break;
	    case 4:
	      if (s[1] && s[2] && s[3]) {
		unsigned int n;

		uc = ((s[0] & 0x7u) << 18) | ((s[1] & 0x3Fu) << 12) |
		  ((s[2] & 0x3Fu) << 6) | (s[3] & 0x3Fu);
		uc -= 0x10000u;
		n = ((uc >> 10) & 0x3FFu) + 0xD800u;
		v[0] = '\\';
		v[1] = 'u';
		v[2] = hex[(n >> 12) & 0xFu];
		v[3] = hex[(n >> 8) & 0xFu];
		v[4] = hex[(n >> 4) & 0xFu];
		v[5] = hex[(n >> 0) & 0xFu];
		n = (uc & 0x3FFu) + 0xDC00u;
		v[6] = '\\';
		v[7] = 'u';
		v[8] = hex[(n >> 12) & 0xFu];
		v[9] = hex[(n >> 8) & 0xFu];
		v[10] = hex[(n >> 4) & 0xFu];
		v[11] = hex[(n >> 0) & 0xFu];
		size = 12;
		s += 4;
	      }
	      break;
	    }
	  }
	  else {
	    switch (fast_json_utf8_size[*s & 0xFFu]) {
	    case 0:		/* FALLTHRU */
	    case 1:
	      /* Should never happen */
	      break;
	    case 2:
	      if (s[1]) {
		v[0] = s[0];
		v[1] = s[1];
		size = 2;
		s += 2;
	      }
	      break;
	    case 3:
	      if (s[1] && s[2]) {
		v[0] = s[0];
		v[1] = s[1];
		v[2] = s[2];
		size = 3;
		s += 3;
	      }
	      break;
	    case 4:
	      if (s[1] && s[2] && s[3]) {
		v[0] = s[0];
		v[1] = s[1];
		v[2] = s[2];
		v[3] = s[3];
		size = 4;
		s += 4;
	      }
	      break;
	    }
	  }
	  if (size == 0) {
	    /* Should never happen */
	    v[0] = *s++;
	    size = 1;
	  }
	}
	else if (special[*s & 0xFFu] == 1) {
	  unsigned char u = (unsigned char) *s++;

	  v[0] = '\\';
	  v[1] = 'u';
	  v[2] = hex[(u >> 12) & 0xFu];
	  v[3] = hex[(u >> 8) & 0xFu];
	  v[4] = hex[(u >> 4) & 0xFu];
	  v[5] = hex[(u >> 0) & 0xFu];
	  size = 6;
	}
	else {
	  v[0] = '\\';
	  v[1] = special[*s++ & 0xFFu];
	  size = 2;
	}
	if (fast_json_puts (json, v, size)) {
	  return -1;
	}
      }
      else {
	if (last == NULL) {
	  last = s;
	}
	s++;
      }
    }
    if (last) {
      if (fast_json_puts (json, last, s - last)) {
	return -1;
      }
    }
  }
  return fast_json_puts (json, "\"", 1);
}

static int
fast_json_print_spaces (FAST_JSON_TYPE json, unsigned int n)
{
  unsigned int size = n * 2;
  static const char spaces[7] = {
    ' ', ' ', ' ', ' ', ' ', ' ', ' '
  };
  static const char tabs[8] = {
    '\t', '\t', '\t', '\t', '\t', '\t', '\t', '\t'
  };

  while (size >= 8) {
    unsigned int t = size >= 64 ? 8 : size / 8;

    if (fast_json_puts (json, tabs, t)) {
      return -1;
    }
    size -= 8 * t;
  }
  if (size) {
    if (fast_json_puts (json, spaces, size)) {
      return -1;
    }
  }
  return 0;
}

static int
fast_json_compare_object (const void *a, const void *b)
{
  const FAST_JSON_NAME_VALUE_TYPE *na = (const FAST_JSON_NAME_VALUE_TYPE *) a;
  const FAST_JSON_NAME_VALUE_TYPE *nb = (const FAST_JSON_NAME_VALUE_TYPE *) b;

  return strcmp (na->name, nb->name);
}

static int
fast_json_print_buffer (FAST_JSON_TYPE json,
			FAST_JSON_DATA_TYPE value, unsigned int n,
			unsigned int nice)
{
  if (value) {
    switch (value->type) {
    case FAST_JSON_OBJECT:
      {
	FAST_JSON_OBJECT_TYPE *o;

	n++;
	if (fast_json_puts (json, "{\n", nice ? 2 : 1)) {
	  return -1;
	}
	o = value->u.object;
	if (o) {
	  unsigned int i;
	  FAST_JSON_NAME_VALUE_TYPE *p = NULL;
	  FAST_JSON_NAME_VALUE_TYPE *d = o->data;

	  if (json->options & FAST_JSON_SORT_OBJECTS) {
	    unsigned int s = o->len * sizeof (FAST_JSON_NAME_VALUE_TYPE);

	    p = (FAST_JSON_NAME_VALUE_TYPE *) json->my_malloc (s);
	    if (p) {
	      memcpy (p, o->data, s);
	      SSORT (p, o->len, sizeof (FAST_JSON_NAME_VALUE_TYPE),
		     fast_json_compare_object);
	      d = p;
	    }
	  }

	  for (i = 0; i < o->len; i++) {
	    if ((nice && fast_json_print_spaces (json, n)) ||
		fast_json_print_string_value (json, d[i].name) ||
		fast_json_puts (json, ": ", nice ? 2 : 1) ||
		fast_json_print_buffer (json, d[i].value, n, nice) ||
		((i + 1) < o->len &&
		 fast_json_puts (json, ",\n", nice ? 2 : 1))) {
	      return -1;
	    }
	  }
	  if (p) {
	    json->my_free (p);
	  }
	}
	n--;
	if ((nice && (fast_json_puts (json, "\n", 1) ||
		      fast_json_print_spaces (json, n))) ||
	    fast_json_puts (json, "}", 1)) {
	  return -1;
	}
      }
      break;
    case FAST_JSON_ARRAY:
      {
	FAST_JSON_ARRAY_TYPE *a;

	n++;
	if (fast_json_puts (json, "[\n", nice ? 2 : 1)) {
	  return -1;
	}
	a = value->u.array;
	if (a) {
	  unsigned int i;

	  for (i = 0; i < a->len; i++) {
	    if ((nice && fast_json_print_spaces (json, n)) ||
		fast_json_print_buffer (json, a->values[i], n, nice) ||
		((i + 1) < a->len &&
		 fast_json_puts (json, ",\n", nice ? 2 : 1))) {
	      return -1;
	    }
	  }
	}
	n--;
	if ((nice && (fast_json_puts (json, "\n", 1) ||
		      fast_json_print_spaces (json, n))) ||
	    fast_json_puts (json, "]", 1)) {
	  return -1;
	}
      }
      break;
    case FAST_JSON_INTEGER:
      {
	char v[100];

#if USE_FAST_CONVERT
	fast_sint64 (value->u.int_value, v);
#else
	snprintf (v, sizeof (v), "%" FAST_JSON_FMT_INT, value->u.int_value);
#endif
	return fast_json_puts (json, v, strlen (v));
      }
    case FAST_JSON_DOUBLE:
      {
	char *cp;
	char v[100];

#if USE_FAST_CONVERT
	fast_dtoa (value->u.double_value, PREC_DBL_NR, v);
#else
	snprintf (v, sizeof (v), "%.17g", value->u.double_value);
#endif
	cp = v;
	if (*cp == '-' || *cp == '+') {
	  cp++;
	}
	while (fast_json_isdigit (*cp)) {
	  cp++;
	}
	if (*cp == json->decimal_point) {
	  *cp = '.';
	}
	else if (*cp == '\0') {
	  strcpy (cp, ".0");
	}
	return fast_json_puts (json, v, strlen (v));
      }
    case FAST_JSON_STRING:
      return fast_json_print_string_value (json,
					   value->is_str
					   ? &value->u.i_string_value[0]
					   : value->u.string_value);
    case FAST_JSON_BOOLEAN:
      {
	const char *v = value->u.boolean_value ? "true" : "false";

	return fast_json_puts (json, v, strlen (v));
      }
    case FAST_JSON_NULL:
      return fast_json_puts (json, "null", strlen ("null"));
    }
  }
  return 0;
}

char *
fast_json_print_string (FAST_JSON_TYPE json, FAST_JSON_DATA_TYPE value,
			unsigned int nice)
{
  if (json && value) {
    json->puts = fast_json_puts_string;
    json->puts_len = 0;
    json->u.buf.len = 0;
    json->u.buf.max = 0;
    json->u.buf.txt = NULL;
    json->decimal_point = *localeconv ()->decimal_point;
    if (fast_json_print_buffer (json, value, 0, nice)) {
      (*json->my_free) (json->u.buf.txt);
      return NULL;
    }
    if (fast_json_last_puts (json, nice ? "\n" : "", nice ? 2 : 1)) {
      (*json->my_free) (json->u.buf.txt);
      return NULL;
    }
    return json->u.buf.txt;
  }
  return NULL;
}

int
fast_json_print_string_len (FAST_JSON_TYPE json, FAST_JSON_DATA_TYPE value,
			    char *str, size_t len, unsigned int nice)
{
  if (json && value) {
    json->puts = fast_json_puts_string_len;
    json->puts_len = 0;
    json->u.buf.len = 0;
    json->u.buf.max = len;
    json->u.buf.txt = str;
    json->decimal_point = *localeconv ()->decimal_point;
    if (fast_json_print_buffer (json, value, 0, nice)) {
      return -1;
    }
    if (fast_json_last_puts (json, nice ? "\n" : "", nice ? 2 : 1)) {
      return -1;
    }
    return json->u.buf.len;
  }
  return -1;
}

int
fast_json_print_file (FAST_JSON_TYPE json, FAST_JSON_DATA_TYPE value,
		      FILE * fp, unsigned int nice)
{
  if (json && value && fp) {
    json->puts = fast_json_puts_file;
    json->puts_len = 0;
    json->u.fp = fp;
    json->decimal_point = *localeconv ()->decimal_point;
    if (fast_json_print_buffer (json, value, 0, nice)) {
      return -1;
    }
    if (fast_json_last_puts (json, "\n", nice ? 1 : 0)) {
      return -1;
    }
    return 0;
  }
  return -1;
}

int
fast_json_print_file_name (FAST_JSON_TYPE json, FAST_JSON_DATA_TYPE value,
			   const char *name, unsigned int nice)
{
  int retval = 1;

  if (json && value && name) {
    FILE *fp = fopen (name, "w");

    if (fp) {
      json->puts = fast_json_puts_file;
      json->puts_len = 0;
      json->u.fp = fp;
      json->decimal_point = *localeconv ()->decimal_point;
      retval = fast_json_print_buffer (json, value, 0, nice);
      if (retval == 0) {
	retval = fast_json_last_puts (json, "\n", nice ? 1 : 0);
      }
      fclose (fp);
    }
  }
  return retval;
}

int
fast_json_print_fd (FAST_JSON_TYPE json, FAST_JSON_DATA_TYPE value,
		    int fd, unsigned int nice)
{
  if (json && value) {
    json->puts = fast_json_puts_fd;
    json->puts_len = 0;
    json->u.fd.fd = fd;
    json->decimal_point = *localeconv ()->decimal_point;
    if (fast_json_print_buffer (json, value, 0, nice)) {
      return -1;
    }
    if (fast_json_last_puts (json, "\n", nice ? 1 : 0)) {
      return -1;
    }
    return 0;
  }
  return -1;
}

void
fast_json_release_print_value (FAST_JSON_TYPE json, char *str)
{
  if (json) {
    (*json->my_free) (str);
  }
}

FAST_JSON_DATA_TYPE
fast_json_create_null (FAST_JSON_TYPE json)
{
  if (json) {
    FAST_JSON_DATA_TYPE item = fast_json_data_create (json);

    if (item) {
      item->type = FAST_JSON_NULL;
      item->used = 0;
    }
    return item;
  }
  return NULL;
}

FAST_JSON_DATA_TYPE
fast_json_create_true (FAST_JSON_TYPE json)
{
  if (json) {
    FAST_JSON_DATA_TYPE item = fast_json_data_create (json);

    if (item) {
      item->type = FAST_JSON_BOOLEAN;
      item->used = 0;
      item->u.boolean_value = 1;
    }
    return item;
  }
  return NULL;
}

FAST_JSON_DATA_TYPE
fast_json_create_false (FAST_JSON_TYPE json)
{
  if (json) {
    FAST_JSON_DATA_TYPE item = fast_json_data_create (json);

    if (item) {
      item->type = FAST_JSON_BOOLEAN;
      item->used = 0;
      item->u.boolean_value = 0;
    }
    return item;
  }
  return NULL;
}

FAST_JSON_DATA_TYPE
fast_json_create_boolean_value (FAST_JSON_TYPE json, unsigned int value)
{
  if (json) {
    FAST_JSON_DATA_TYPE item = fast_json_data_create (json);

    if (item) {
      item->type = FAST_JSON_BOOLEAN;
      item->used = 0;
      item->u.boolean_value = value ? 1 : 0;
    }
    return item;
  }
  return NULL;
}

FAST_JSON_DATA_TYPE
fast_json_create_integer_value (FAST_JSON_TYPE json, fast_json_int_64 value)
{
  if (json) {
    FAST_JSON_DATA_TYPE item = fast_json_data_create (json);

    if (item) {
      item->type = FAST_JSON_INTEGER;
      item->used = 0;
      item->u.int_value = value;
    }
    return item;
  }
  return NULL;
}

FAST_JSON_DATA_TYPE
fast_json_create_double_value (FAST_JSON_TYPE json, double value)
{
  if (json) {
    if ((json->options & FAST_JSON_INF_NAN) ||
	(!isnan (value) && !isinf (value))) {
      FAST_JSON_DATA_TYPE item = fast_json_data_create (json);

      if (item) {
	item->type = FAST_JSON_DOUBLE;
	item->used = 0;
	item->u.double_value = value;
      }
      return item;
    }
  }
  return NULL;
}

FAST_JSON_DATA_TYPE
fast_json_create_string (FAST_JSON_TYPE json, const char *value)
{
  FAST_JSON_DATA_TYPE item = NULL;

  if (json && value) {
    int len = strlen (value);
    char str[8];
    char *new_value = &str[0];

    if (len >= sizeof (str)) {
      new_value = fast_json_strdup (json, value);
    }
    else {
      strcpy (str, value);
    }
    if (new_value) {
      if (fast_json_check_string (json, value, value + len, new_value) ==
	  FAST_JSON_OK) {
	item = fast_json_data_create (json);
	if (item) {
	  item->type = FAST_JSON_STRING;
	  item->used = 0;
	  if (len >= sizeof (str)) {
	    item->is_str = 0;
	    item->u.string_value = new_value;
	  }
	  else {
	    item->is_str = 1;
	    strcpy (item->u.i_string_value, new_value);
	  }
	}
      }
      else if (new_value != &str[0]) {
	(*json->my_free) (new_value);
      }
    }
  }
  return item;
}

FAST_JSON_DATA_TYPE
fast_json_create_array (FAST_JSON_TYPE json)
{
  if (json) {
    FAST_JSON_DATA_TYPE item = fast_json_data_create (json);

    if (item) {
      item->type = FAST_JSON_ARRAY;
      item->used = 0;
      item->u.array = NULL;
    }
    return item;
  }
  return NULL;
}

FAST_JSON_DATA_TYPE
fast_json_create_object (FAST_JSON_TYPE json)
{
  if (json) {
    FAST_JSON_DATA_TYPE item = fast_json_data_create (json);

    if (item) {
      item->type = FAST_JSON_OBJECT;
      item->used = 0;
      item->u.object = NULL;
    }
    return item;
  }
  return NULL;
}

FAST_JSON_DATA_TYPE
fast_json_create_boolean_array (FAST_JSON_TYPE json,
				const unsigned int *numbers, size_t len)
{
  if (json) {
    FAST_JSON_DATA_TYPE array = fast_json_create_array (json);

    if (array) {
      size_t i;

      for (i = 0; i < len; i++) {
	FAST_JSON_DATA_TYPE v =
	  fast_json_create_boolean_value (json, numbers[i]);

	if (v == NULL) {
	  fast_json_value_free (json, array);
	  return NULL;
	}
	if (fast_json_add_array_end (json, array, v) != FAST_JSON_OK) {
	  fast_json_value_free (json, v);
	  fast_json_value_free (json, array);
	  return NULL;
	}
      }
    }
    return array;
  }
  return NULL;
}

FAST_JSON_DATA_TYPE
fast_json_create_integer_array (FAST_JSON_TYPE json,
				const fast_json_int_64 * numbers, size_t len)
{
  if (json) {
    FAST_JSON_DATA_TYPE array = fast_json_create_array (json);

    if (array) {
      size_t i;

      for (i = 0; i < len; i++) {
	FAST_JSON_DATA_TYPE v =
	  fast_json_create_integer_value (json, numbers[i]);

	if (v == NULL) {
	  fast_json_value_free (json, array);
	  return NULL;
	}
	if (fast_json_add_array_end (json, array, v) != FAST_JSON_OK) {
	  fast_json_value_free (json, v);
	  fast_json_value_free (json, array);
	  return NULL;
	}
      }
    }
    return array;
  }
  return NULL;
}

FAST_JSON_DATA_TYPE
fast_json_create_double_array (FAST_JSON_TYPE json, const double *numbers,
			       size_t len)
{
  if (json) {
    FAST_JSON_DATA_TYPE array = fast_json_create_array (json);

    if (array) {
      size_t i;

      for (i = 0; i < len; i++) {
	FAST_JSON_DATA_TYPE v =
	  fast_json_create_double_value (json, numbers[i]);

	if (v == NULL) {
	  fast_json_value_free (json, array);
	  return NULL;
	}
	if (fast_json_add_array_end (json, array, v) != FAST_JSON_OK) {
	  fast_json_value_free (json, v);
	  fast_json_value_free (json, array);
	  return NULL;
	}
      }
    }
    return array;
  }
  return NULL;
}

FAST_JSON_DATA_TYPE
fast_json_create_string_array (FAST_JSON_TYPE json, const char **strings,
			       size_t len)
{
  if (json) {
    FAST_JSON_DATA_TYPE array = fast_json_create_array (json);

    if (array) {
      size_t i;

      for (i = 0; i < len; i++) {
	FAST_JSON_DATA_TYPE v = fast_json_create_string (json, strings[i]);

	if (v == NULL) {
	  fast_json_value_free (json, array);
	  return NULL;
	}
	if (fast_json_add_array_end (json, array, v) != FAST_JSON_OK) {
	  fast_json_value_free (json, v);
	  fast_json_value_free (json, array);
	  return NULL;
	}
      }
    }
    return array;
  }
  return NULL;
}

static unsigned int
fast_json_check_loop (FAST_JSON_DATA_TYPE data, FAST_JSON_DATA_TYPE value)
{
  if (data == value) {
    return 1;
  }
  if (value->type == FAST_JSON_ARRAY) {
    FAST_JSON_ARRAY_TYPE *a = value->u.array;

    if (a) {
      unsigned int i;

      for (i = 0; i < a->len; i++) {
	FAST_JSON_DATA_TYPE v = a->values[i];

	if (data == v ||
	    ((v->type == FAST_JSON_ARRAY || v->type == FAST_JSON_OBJECT) &&
	     fast_json_check_loop (data, v))) {
	  return 1;
	}
      }
    }
  }
  else if (value->type == FAST_JSON_OBJECT) {
    FAST_JSON_OBJECT_TYPE *o = value->u.object;

    if (o) {
      unsigned int i;

      for (i = 0; i < o->len; i++) {
	FAST_JSON_DATA_TYPE v = o->data[i].value;

	if (data == v ||
	    ((v->type == FAST_JSON_ARRAY || v->type == FAST_JSON_OBJECT) &&
	     fast_json_check_loop (data, v))) {
	  return 1;
	}
      }
    }
  }
  return 0;
}

static FAST_JSON_ERROR_ENUM
fast_json_add_array_end (FAST_JSON_TYPE json, FAST_JSON_DATA_TYPE array,
			 FAST_JSON_DATA_TYPE value)
{
  FAST_JSON_ERROR_ENUM retval = FAST_JSON_MALLOC_ERROR;
  FAST_JSON_ARRAY_TYPE *a = array->u.array;

  if (a == NULL) {
    unsigned int size = sizeof (FAST_JSON_ARRAY_TYPE) +
      (FAST_JSON_INITIAL_SIZE - 1) * sizeof (FAST_JSON_DATA_TYPE);

    a = array->u.array = (FAST_JSON_ARRAY_TYPE *) (*json->my_malloc) (size);
    if (a) {
      a->len = 0;
      a->max = FAST_JSON_INITIAL_SIZE;
    }
  }
  if (a) {
    if (a->len == a->max) {
      unsigned int l = a->max ? a->max * 2 : 1;
      unsigned int size = sizeof (FAST_JSON_ARRAY_TYPE) +
	(l - 1) * sizeof (FAST_JSON_DATA_TYPE);
      FAST_JSON_ARRAY_TYPE *na =
	(FAST_JSON_ARRAY_TYPE *) (*json->my_realloc) (a, size);

      if (na) {
	a = array->u.array = na;
	a->max = l;
      }
    }
    if (a->len != a->max) {
      array->used = 1;
      value->used = 1;
      a->values[a->len++] = value;
      retval = FAST_JSON_OK;
    }
  }
  return retval;
}

FAST_JSON_ERROR_ENUM
fast_json_add_array (FAST_JSON_TYPE json, FAST_JSON_DATA_TYPE array,
		     FAST_JSON_DATA_TYPE value)
{
  FAST_JSON_ERROR_ENUM retval = FAST_JSON_MALLOC_ERROR;

  if (json && array && array->type == FAST_JSON_ARRAY && value) {
    if ((json->options & FAST_JSON_NO_CHECK_LOOP) ||
	array->used == 0 || value->used == 0 ||
	(fast_json_check_loop (array, value) == 0 &&
	 fast_json_check_loop (value, array) == 0)) {
      retval = fast_json_add_array_end (json, array, value);
    }
    else {
      retval = FAST_JSON_LOOP_ERROR;
    }
  }
  return retval;
}

static FAST_JSON_ERROR_ENUM
fast_json_add_object_end (FAST_JSON_TYPE json, FAST_JSON_DATA_TYPE object,
			  const char *name, FAST_JSON_DATA_TYPE value)
{
  FAST_JSON_ERROR_ENUM retval = FAST_JSON_MALLOC_ERROR;
  FAST_JSON_OBJECT_TYPE *o = object->u.object;

  if (o == NULL) {
    size_t size = sizeof (FAST_JSON_OBJECT_TYPE) +
      (FAST_JSON_INITIAL_SIZE - 1) * sizeof (FAST_JSON_NAME_VALUE_TYPE);

    o = object->u.object =
      (FAST_JSON_OBJECT_TYPE *) (*json->my_malloc) (size);
    if (o) {
      o->len = 0;
      o->max = FAST_JSON_INITIAL_SIZE;
    }
  }
  else {
    size_t l;

    for (l = 0; l < o->len; l++) {
      if (strcmp (o->data[l].name, name) == 0) {
	fast_json_value_free (json, o->data[l].value);
	o->data[l].value = value;
	retval = FAST_JSON_OK;
	return retval;
      }
    }
  }
  if (o) {
    if (o->len == o->max) {
      size_t l = o->max ? o->max * 2 : 1;
      size_t size = sizeof (FAST_JSON_OBJECT_TYPE) +
	(l - 1) * sizeof (FAST_JSON_NAME_VALUE_TYPE);

      FAST_JSON_OBJECT_TYPE *no =
	(FAST_JSON_OBJECT_TYPE *) (*json->my_realloc) (o, size);

      if (no) {
	o = object->u.object = no;
	o->max = l;
      }
    }
    if (o->len != o->max) {
      object->used = 1;
      value->used = 1;
      o->data[o->len].name = fast_json_strdup (json, name);
      o->data[o->len].value = value;
      if (o->data[o->len].name != NULL) {
	o->len++;
	retval = FAST_JSON_OK;
      }
    }
  }
  return retval;
}

FAST_JSON_ERROR_ENUM
fast_json_add_object (FAST_JSON_TYPE json, FAST_JSON_DATA_TYPE object,
		      const char *name, FAST_JSON_DATA_TYPE value)
{
  FAST_JSON_ERROR_ENUM retval = FAST_JSON_MALLOC_ERROR;

  if (json && object && object->type == FAST_JSON_OBJECT && name && value) {
    if ((json->options & FAST_JSON_NO_CHECK_LOOP) ||
	object->used == 0 || value->used == 0 ||
	(fast_json_check_loop (object, value) == 0 &&
	 fast_json_check_loop (value, object) == 0)) {
      retval = fast_json_add_object_end (json, object, name, value);
    }
    else {
      retval = FAST_JSON_LOOP_ERROR;
    }
  }
  return retval;
}

FAST_JSON_ERROR_ENUM
fast_json_patch_array (FAST_JSON_TYPE json, FAST_JSON_DATA_TYPE array,
		       FAST_JSON_DATA_TYPE value, size_t index)
{
  FAST_JSON_ERROR_ENUM retval = FAST_JSON_INDEX_ERROR;

  if (json && array && array->type == FAST_JSON_ARRAY && value &&
      array->u.array && index < array->u.array->len) {
    if ((json->options & FAST_JSON_NO_CHECK_LOOP) ||
	array->used == 0 || value->used == 0 ||
	(fast_json_check_loop (array, value) == 0 &&
	 fast_json_check_loop (value, array) == 0)) {
      array->used = 1;
      value->used = 1;
      fast_json_value_free (json, array->u.array->values[index]);
      array->u.array->values[index] = value;
      retval = FAST_JSON_OK;
    }
    else {
      retval = FAST_JSON_LOOP_ERROR;
    }
  }
  return retval;
}

FAST_JSON_ERROR_ENUM
fast_json_insert_array (FAST_JSON_TYPE json, FAST_JSON_DATA_TYPE array,
			FAST_JSON_DATA_TYPE value, size_t index)
{
  FAST_JSON_ERROR_ENUM retval = FAST_JSON_INDEX_ERROR;

  if (json && array && array->type == FAST_JSON_ARRAY && value &&
      array->u.array && index < array->u.array->len) {
    if ((json->options & FAST_JSON_NO_CHECK_LOOP) ||
	array->used == 0 || value->used == 0 ||
	(fast_json_check_loop (array, value) == 0 &&
	 fast_json_check_loop (value, array) == 0)) {
      retval = fast_json_add_array_end (json, array, value);
      if (retval == FAST_JSON_OK) {
	size_t i;
	FAST_JSON_DATA_TYPE *value_array = array->u.array->values;

	array->used = 1;
	value->used = 1;
	for (i = array->u.array->len - 1; i > index; i--) {
	  value_array[i] = value_array[i - 1];
	}
	value_array[index] = value;
      }
    }
    else {
      retval = FAST_JSON_LOOP_ERROR;
    }
  }
  return retval;
}

FAST_JSON_ERROR_ENUM
fast_json_remove_array (FAST_JSON_TYPE json, FAST_JSON_DATA_TYPE array,
			size_t index)
{
  FAST_JSON_ERROR_ENUM retval = FAST_JSON_INDEX_ERROR;

  if (json && array && array->type == FAST_JSON_ARRAY &&
      array->u.array && index < array->u.array->len) {
    size_t i;
    FAST_JSON_DATA_TYPE *value_array = array->u.array->values;

    fast_json_value_free (json, value_array[index]);
    array->u.array->len--;
    for (i = index; i < array->u.array->len; i++) {
      value_array[i] = value_array[i + 1];
    }
    retval = FAST_JSON_OK;
  }
  return retval;
}

FAST_JSON_ERROR_ENUM
fast_json_patch_object (FAST_JSON_TYPE json, FAST_JSON_DATA_TYPE object,
			FAST_JSON_DATA_TYPE value, size_t index)
{
  FAST_JSON_ERROR_ENUM retval = FAST_JSON_INDEX_ERROR;

  if (json && object && object->type == FAST_JSON_OBJECT && value &&
      object->u.object && index < object->u.object->len) {
    if (((json->options & FAST_JSON_NO_CHECK_LOOP) ||
	 object->used == 0 || value->used == 0 ||
	 (fast_json_check_loop (object, value) == 0 &&
	  fast_json_check_loop (value, object) == 0))) {
      object->used = 1;
      value->used = 1;
      fast_json_value_free (json, object->u.object->data[index].value);
      object->u.object->data[index].value = value;
      retval = FAST_JSON_OK;
    }
    else {
      retval = FAST_JSON_LOOP_ERROR;
    }
  }
  return retval;
}

FAST_JSON_ERROR_ENUM
fast_json_insert_object (FAST_JSON_TYPE json, FAST_JSON_DATA_TYPE object,
			 const char *name, FAST_JSON_DATA_TYPE value,
			 size_t index)
{
  FAST_JSON_ERROR_ENUM retval = FAST_JSON_INDEX_ERROR;

  if (json && object && object->type == FAST_JSON_OBJECT && name && value &&
      object->u.object && index < object->u.object->len) {
    if (((json->options & FAST_JSON_NO_CHECK_LOOP) ||
	 object->used == 0 || value->used == 0 ||
	 (fast_json_check_loop (object, value) == 0 &&
	  fast_json_check_loop (value, object) == 0))) {
      retval = fast_json_add_object_end (json, object, name, value);
      if (retval == FAST_JSON_OK) {
	size_t i;
	FAST_JSON_NAME_VALUE_TYPE *data = object->u.object->data;
	char *save_name = data[object->u.object->len - 1].name;

	object->used = 1;
	value->used = 1;
	for (i = object->u.object->len - 1; i > index; i--) {
	  data[i] = data[i - 1];
	}
	data[index].name = save_name;
	data[index].value = value;
      }
    }
    else {
      retval = FAST_JSON_LOOP_ERROR;
    }
  }
  return retval;
}

FAST_JSON_ERROR_ENUM
fast_json_remove_object (FAST_JSON_TYPE json, FAST_JSON_DATA_TYPE object,
			 size_t index)
{
  FAST_JSON_ERROR_ENUM retval = FAST_JSON_INDEX_ERROR;

  if (json && object && object->type == FAST_JSON_OBJECT &&
      object->u.object && index < object->u.object->len) {
    size_t i;
    FAST_JSON_NAME_VALUE_TYPE *data = object->u.object->data;

    (*json->my_free) (data[index].name);
    fast_json_value_free (json, data[index].value);
    object->u.object->len--;
    for (i = index; i < object->u.object->len; i++) {
      data[i] = data[i + 1];
    }
    retval = FAST_JSON_OK;
  }
  return retval;
}

FAST_JSON_VALUE_TYPE
fast_json_get_type (FAST_JSON_DATA_TYPE data)
{
  return data ? data->type : FAST_JSON_NULL;
}

size_t
fast_json_get_array_size (FAST_JSON_DATA_TYPE data)
{
  return data && data->type == FAST_JSON_ARRAY &&
    data->u.array ? data->u.array->len : 0;
}

FAST_JSON_DATA_TYPE
fast_json_get_array_data (FAST_JSON_DATA_TYPE data, size_t index)
{
  if (data && data->type == FAST_JSON_ARRAY &&
      data->u.array && index < data->u.array->len) {
    return data->u.array->values[index];
  }
  return NULL;
}

size_t
fast_json_get_object_size (FAST_JSON_DATA_TYPE data)
{
  return data && data->type == FAST_JSON_OBJECT &&
    data->u.object ? data->u.object->len : 0;
}

char *
fast_json_get_object_name (FAST_JSON_DATA_TYPE data, size_t index)
{
  if (data && data->type == FAST_JSON_OBJECT &&
      data->u.object && index < data->u.object->len) {
    return data->u.object->data[index].name;
  }
  return NULL;
}

FAST_JSON_DATA_TYPE
fast_json_get_object_data (FAST_JSON_DATA_TYPE data, size_t index)
{
  if (data && data->type == FAST_JSON_OBJECT &&
      data->u.object && index < data->u.object->len) {
    return data->u.object->data[index].value;
  }
  return NULL;
}

FAST_JSON_DATA_TYPE
fast_json_get_object_by_name (FAST_JSON_DATA_TYPE object, const char *name)
{
  if (object && object->type == FAST_JSON_OBJECT && name) {
    FAST_JSON_OBJECT_TYPE *o = object->u.object;

    if (o) {
      unsigned int i;

      for (i = 0; i < o->len; i++) {
	if (strcmp (o->data[i].name, name) == 0) {
	  return (o->data[i].value);
	}
      }
    }
  }
  return NULL;
}

fast_json_int_64
fast_json_get_integer (FAST_JSON_DATA_TYPE data)
{
  return data && data->type == FAST_JSON_INTEGER ? data->u.int_value : 0;
}

double
fast_json_get_double (FAST_JSON_DATA_TYPE data)
{
  return data && data->type == FAST_JSON_DOUBLE ? data->u.double_value : 0.0;
}

char *
fast_json_get_string (FAST_JSON_DATA_TYPE data)
{
  return data && data->type == FAST_JSON_STRING
    ? (data->is_str ? &data->u.i_string_value[0] : data->u.string_value)
    : NULL;
}

unsigned int
fast_json_get_boolean (FAST_JSON_DATA_TYPE data)
{
  return data && data->type == FAST_JSON_BOOLEAN ? data->u.boolean_value : 0;
}

FAST_JSON_ERROR_ENUM
fast_json_set_integer (FAST_JSON_DATA_TYPE data, fast_json_int_64 value)
{
  FAST_JSON_ERROR_ENUM retval = FAST_JSON_VALUE_ERROR;

  if (data && data->type == FAST_JSON_INTEGER) {
    data->u.int_value = value;
    retval = FAST_JSON_OK;
  }
  return retval;
}

FAST_JSON_ERROR_ENUM
fast_json_set_double (FAST_JSON_TYPE json, FAST_JSON_DATA_TYPE data,
		      double value)
{
  FAST_JSON_ERROR_ENUM retval = FAST_JSON_VALUE_ERROR;

  if (json && data && data->type == FAST_JSON_DOUBLE) {
    if ((json->options & FAST_JSON_INF_NAN) ||
	(!isnan (value) && !isinf (value))) {
      data->u.double_value = value;
      retval = FAST_JSON_OK;
    }
  }
  return retval;
}

FAST_JSON_ERROR_ENUM
fast_json_set_string (FAST_JSON_TYPE json, FAST_JSON_DATA_TYPE data,
		      const char *value)
{
  FAST_JSON_ERROR_ENUM retval = FAST_JSON_VALUE_ERROR;

  if (json && data && data->type == FAST_JSON_STRING && value) {
    int len = strlen (value);
    char str[8];
    char *new_value;

    if (len >= sizeof (str)) {
      new_value = fast_json_strdup (json, value);
    }
    else {
      new_value = strcpy (str, value);
    }
    if (new_value) {
      if (fast_json_check_string (json, value, value + len, new_value) ==
	  FAST_JSON_OK) {
	if (data->is_str == 0) {
	  (*json->my_free) (data->u.string_value);
	}
	if (len >= sizeof (str)) {
	  data->is_str = 0;
	  data->u.string_value = new_value;
	}
	else {
	  data->is_str = 1;
	  strcpy (data->u.i_string_value, new_value);
	}
	retval = FAST_JSON_OK;
      }
      else {
	(*json->my_free) (new_value);
      }
    }
  }
  return retval;
}

FAST_JSON_ERROR_ENUM
fast_json_set_boolean_value (FAST_JSON_DATA_TYPE data, unsigned int value)
{
  FAST_JSON_ERROR_ENUM retval = FAST_JSON_VALUE_ERROR;

  if (data && data->type == FAST_JSON_BOOLEAN) {
    data->u.boolean_value = value ? 1 : 0;
    retval = FAST_JSON_OK;
  }
  return retval;
}

#if 0
#include <stdio.h>

int
main (void)
{
  unsigned int i;
  unsigned int j;
  unsigned int crc;

  for (i = 0; i < 256; i++) {
    crc = i;

    for (j = 0; j < 8; j++) {
      crc = crc & 1u ? 0xEDB88320u ^ (crc >> 1u) : crc >> 1u;
    }
    printf ("0x%08X, ", crc);
    if ((i & 3) == 3) {
      printf ("\n");
    }
  }
  return 0;
}
#endif
static const unsigned int fast_json_crctab[256] = {
  0x00000000u, 0x77073096u, 0xEE0E612Cu, 0x990951BAu,
  0x076DC419u, 0x706AF48Fu, 0xE963A535u, 0x9E6495A3u,
  0x0EDB8832u, 0x79DCB8A4u, 0xE0D5E91Eu, 0x97D2D988u,
  0x09B64C2Bu, 0x7EB17CBDu, 0xE7B82D07u, 0x90BF1D91u,
  0x1DB71064u, 0x6AB020F2u, 0xF3B97148u, 0x84BE41DEu,
  0x1ADAD47Du, 0x6DDDE4EBu, 0xF4D4B551u, 0x83D385C7u,
  0x136C9856u, 0x646BA8C0u, 0xFD62F97Au, 0x8A65C9ECu,
  0x14015C4Fu, 0x63066CD9u, 0xFA0F3D63u, 0x8D080DF5u,
  0x3B6E20C8u, 0x4C69105Eu, 0xD56041E4u, 0xA2677172u,
  0x3C03E4D1u, 0x4B04D447u, 0xD20D85FDu, 0xA50AB56Bu,
  0x35B5A8FAu, 0x42B2986Cu, 0xDBBBC9D6u, 0xACBCF940u,
  0x32D86CE3u, 0x45DF5C75u, 0xDCD60DCFu, 0xABD13D59u,
  0x26D930ACu, 0x51DE003Au, 0xC8D75180u, 0xBFD06116u,
  0x21B4F4B5u, 0x56B3C423u, 0xCFBA9599u, 0xB8BDA50Fu,
  0x2802B89Eu, 0x5F058808u, 0xC60CD9B2u, 0xB10BE924u,
  0x2F6F7C87u, 0x58684C11u, 0xC1611DABu, 0xB6662D3Du,
  0x76DC4190u, 0x01DB7106u, 0x98D220BCu, 0xEFD5102Au,
  0x71B18589u, 0x06B6B51Fu, 0x9FBFE4A5u, 0xE8B8D433u,
  0x7807C9A2u, 0x0F00F934u, 0x9609A88Eu, 0xE10E9818u,
  0x7F6A0DBBu, 0x086D3D2Du, 0x91646C97u, 0xE6635C01u,
  0x6B6B51F4u, 0x1C6C6162u, 0x856530D8u, 0xF262004Eu,
  0x6C0695EDu, 0x1B01A57Bu, 0x8208F4C1u, 0xF50FC457u,
  0x65B0D9C6u, 0x12B7E950u, 0x8BBEB8EAu, 0xFCB9887Cu,
  0x62DD1DDFu, 0x15DA2D49u, 0x8CD37CF3u, 0xFBD44C65u,
  0x4DB26158u, 0x3AB551CEu, 0xA3BC0074u, 0xD4BB30E2u,
  0x4ADFA541u, 0x3DD895D7u, 0xA4D1C46Du, 0xD3D6F4FBu,
  0x4369E96Au, 0x346ED9FCu, 0xAD678846u, 0xDA60B8D0u,
  0x44042D73u, 0x33031DE5u, 0xAA0A4C5Fu, 0xDD0D7CC9u,
  0x5005713Cu, 0x270241AAu, 0xBE0B1010u, 0xC90C2086u,
  0x5768B525u, 0x206F85B3u, 0xB966D409u, 0xCE61E49Fu,
  0x5EDEF90Eu, 0x29D9C998u, 0xB0D09822u, 0xC7D7A8B4u,
  0x59B33D17u, 0x2EB40D81u, 0xB7BD5C3Bu, 0xC0BA6CADu,
  0xEDB88320u, 0x9ABFB3B6u, 0x03B6E20Cu, 0x74B1D29Au,
  0xEAD54739u, 0x9DD277AFu, 0x04DB2615u, 0x73DC1683u,
  0xE3630B12u, 0x94643B84u, 0x0D6D6A3Eu, 0x7A6A5AA8u,
  0xE40ECF0Bu, 0x9309FF9Du, 0x0A00AE27u, 0x7D079EB1u,
  0xF00F9344u, 0x8708A3D2u, 0x1E01F268u, 0x6906C2FEu,
  0xF762575Du, 0x806567CBu, 0x196C3671u, 0x6E6B06E7u,
  0xFED41B76u, 0x89D32BE0u, 0x10DA7A5Au, 0x67DD4ACCu,
  0xF9B9DF6Fu, 0x8EBEEFF9u, 0x17B7BE43u, 0x60B08ED5u,
  0xD6D6A3E8u, 0xA1D1937Eu, 0x38D8C2C4u, 0x4FDFF252u,
  0xD1BB67F1u, 0xA6BC5767u, 0x3FB506DDu, 0x48B2364Bu,
  0xD80D2BDAu, 0xAF0A1B4Cu, 0x36034AF6u, 0x41047A60u,
  0xDF60EFC3u, 0xA867DF55u, 0x316E8EEFu, 0x4669BE79u,
  0xCB61B38Cu, 0xBC66831Au, 0x256FD2A0u, 0x5268E236u,
  0xCC0C7795u, 0xBB0B4703u, 0x220216B9u, 0x5505262Fu,
  0xC5BA3BBEu, 0xB2BD0B28u, 0x2BB45A92u, 0x5CB36A04u,
  0xC2D7FFA7u, 0xB5D0CF31u, 0x2CD99E8Bu, 0x5BDEAE1Du,
  0x9B64C2B0u, 0xEC63F226u, 0x756AA39Cu, 0x026D930Au,
  0x9C0906A9u, 0xEB0E363Fu, 0x72076785u, 0x05005713u,
  0x95BF4A82u, 0xE2B87A14u, 0x7BB12BAEu, 0x0CB61B38u,
  0x92D28E9Bu, 0xE5D5BE0Du, 0x7CDCEFB7u, 0x0BDBDF21u,
  0x86D3D2D4u, 0xF1D4E242u, 0x68DDB3F8u, 0x1FDA836Eu,
  0x81BE16CDu, 0xF6B9265Bu, 0x6FB077E1u, 0x18B74777u,
  0x88085AE6u, 0xFF0F6A70u, 0x66063BCAu, 0x11010B5Cu,
  0x8F659EFFu, 0xF862AE69u, 0x616BFFD3u, 0x166CCF45u,
  0xA00AE278u, 0xD70DD2EEu, 0x4E048354u, 0x3903B3C2u,
  0xA7672661u, 0xD06016F7u, 0x4969474Du, 0x3E6E77DBu,
  0xAED16A4Au, 0xD9D65ADCu, 0x40DF0B66u, 0x37D83BF0u,
  0xA9BCAE53u, 0xDEBB9EC5u, 0x47B2CF7Fu, 0x30B5FFE9u,
  0xBDBDF21Cu, 0xCABAC28Au, 0x53B39330u, 0x24B4A3A6u,
  0xBAD03605u, 0xCDD70693u, 0x54DE5729u, 0x23D967BFu,
  0xB3667A2Eu, 0xC4614AB8u, 0x5D681B02u, 0x2A6F2B94u,
  0xB40BBE37u, 0xC30C8EA1u, 0x5A05DF1Bu, 0x2D02EF8Du
};

static void
fast_json_update_crc (unsigned int *crc, const char *str)
{
  unsigned int temp = *crc;

  while (*str) {
    temp = (temp >> 8) ^ fast_json_crctab[(temp & 0xFFu) ^ *str++];
  }
  *crc = temp;
}

static FAST_JSON_ERROR_ENUM
fast_json_parse_crc (FAST_JSON_TYPE json, unsigned int *crc, int c)
{
  FAST_JSON_ERROR_ENUM error;
  char *save;

  fast_json_getc_save_start (json, c);
  switch (c) {
  case 'n':			/* FALLTHRU */
  case 'N':
    while (fast_json_isalpha (c)) {
      c = fast_json_getc_save (json);
    }
    save = fast_json_ungetc_save (json, c);
    if (strcmp (save, "null") == 0) {
      fast_json_update_crc (crc, save);
    }
    else if ((json->options & FAST_JSON_INF_NAN) &&
	     strcasecmp (save, "nan") == 0) {
      c = fast_json_getc_save (json);
      if (c == '(') {
	c = fast_json_getc_save (json);
	while (fast_json_isalpha (c) || fast_json_isdigit (c) || c == '_') {
	  c = fast_json_getc_save (json);
	}
	if (c != ')') {
	  save = fast_json_ungetc_save (json, 0);
	  fast_json_store_error (json, FAST_JSON_NUMBER_ERROR, save);
	  return FAST_JSON_NUMBER_ERROR;
	}
	c = 0;
      }
      save = fast_json_ungetc_save (json, c);
      fast_json_update_crc (crc, save);
    }
    else {
      fast_json_store_error (json, FAST_JSON_VALUE_ERROR, save);
      return FAST_JSON_VALUE_ERROR;
    }
    break;
  case 'f':
    while (fast_json_isalpha (c)) {
      c = fast_json_getc_save (json);
    }
    save = fast_json_ungetc_save (json, c);
    if (strcmp (save, "false") == 0) {
      fast_json_update_crc (crc, save);
    }
    else {
      fast_json_store_error (json, FAST_JSON_VALUE_ERROR, save);
      return FAST_JSON_VALUE_ERROR;
    }
    break;
  case 't':
    while (fast_json_isalpha (c)) {
      c = fast_json_getc_save (json);
    }
    save = fast_json_ungetc_save (json, c);
    if (strcmp (save, "true") == 0) {
      fast_json_update_crc (crc, save);
    }
    else {
      fast_json_store_error (json, FAST_JSON_VALUE_ERROR, save);
      return FAST_JSON_VALUE_ERROR;
    }
    break;
  case 'i':			/* FALLTHRU */
  case 'I':
    while (fast_json_isalpha (c)) {
      c = fast_json_getc_save (json);
    }
    save = fast_json_ungetc_save (json, c);
    if ((json->options & FAST_JSON_INF_NAN) != 0 &&
	(strcasecmp (save, "inf") == 0 ||
	 strcasecmp (save, "infinity") == 0)) {
      fast_json_update_crc (crc, save);
    }
    else {
      fast_json_store_error (json, FAST_JSON_VALUE_ERROR, save);
      return FAST_JSON_VALUE_ERROR;
    }
    break;
  case '"':
    c = fast_json_getc (json);
    fast_json_getc_save_start (json, c);
    while (c > 0 && c != '"') {
      if (c == '\\') {
	fast_json_getc_save (json);
      }
      c = fast_json_getc_save (json);
    }
    save = fast_json_ungetc_save (json, c);
    fast_json_update_crc (crc, save);
    c = fast_json_getc (json);
    if (c != '"') {
      fast_json_store_error (json, FAST_JSON_STRING_END_ERROR, save);
      return FAST_JSON_STRING_END_ERROR;
    }
    break;
  case '+':
    if ((json->options & FAST_JSON_INF_NAN) == 0) {
      save = fast_json_ungetc_save (json, 0);
      fast_json_store_error (json, FAST_JSON_NUMBER_ERROR, save);
      return FAST_JSON_NUMBER_ERROR;
    }
    /* FALLTHRU */
  case '-':			/* FALLTHRU */
  case '0':			/* FALLTHRU */
  case '1':			/* FALLTHRU */
  case '2':			/* FALLTHRU */
  case '3':			/* FALLTHRU */
  case '4':			/* FALLTHRU */
  case '5':			/* FALLTHRU */
  case '6':			/* FALLTHRU */
  case '7':			/* FALLTHRU */
  case '8':			/* FALLTHRU */
  case '9':
    {
      unsigned int hex = 0;

      if (c == '+' || c == '-') {
	c = fast_json_getc_save (json);
      }
      if (c == '0') {
	c = fast_json_getc_save (json);
	if ((json->options & FAST_JSON_ALLOW_OCT_HEX) != 0) {
	  if (c == 'x' || c == 'X') {
	    hex = 1;
	    c = fast_json_getc_save (json);
	    while (fast_json_isxdigit (c)) {
	      c = fast_json_getc_save (json);
	    }
	  }
	  else {
	    while (c >= '0' && c <= '7') {
	      c = fast_json_getc_save (json);
	    }
	  }
	}
      }
      else if (fast_json_isdigit (c)) {
	do {
	  c = fast_json_getc_save (json);
	} while (fast_json_isdigit (c));
      }
      else {
	unsigned int last_n_save = json->n_save > 0 ? json->n_save - 1 : 0;

	while (fast_json_isalpha (c)) {
	  c = fast_json_getc_save (json);
	}
	save = fast_json_ungetc_save (json, c);
	if ((json->options & FAST_JSON_INF_NAN) != 0) {
	  if (strcasecmp (&json->save[last_n_save], "inf") == 0 ||
	      strcasecmp (&json->save[last_n_save], "infinity") == 0) {
	    fast_json_update_crc (crc, save);
	    break;
	  }
	  else if (strcasecmp (&json->save[last_n_save], "nan") == 0) {
	    c = fast_json_getc_save (json);
	    if (c == '(') {
	      c = fast_json_getc_save (json);
	      while (fast_json_isalpha (c) || fast_json_isdigit (c) ||
		     c == '_') {
		c = fast_json_getc_save (json);
	      }
	      if (c != ')') {
		save = fast_json_ungetc_save (json, 0);
		fast_json_store_error (json, FAST_JSON_NUMBER_ERROR, save);
		return FAST_JSON_NUMBER_ERROR;
	      }
	      c = 0;
	    }
	    save = fast_json_ungetc_save (json, c);
	    fast_json_update_crc (crc, save);
	    break;
	  }
	}
	fast_json_getc_save (json);
	save = fast_json_ungetc_save (json, 0);
	fast_json_store_error (json, FAST_JSON_NUMBER_ERROR, save);
	return FAST_JSON_NUMBER_ERROR;
      }
      if (c == '.') {
	c = fast_json_getc_save (json);
	if (hex) {
	  if (fast_json_isxdigit (c)) {
	    do {
	      c = fast_json_getc_save (json);
	    } while (fast_json_isxdigit (c));
	  }
	  else {
	    save = fast_json_ungetc_save (json, 0);
	    fast_json_store_error (json, FAST_JSON_NUMBER_ERROR, save);
	    return FAST_JSON_NUMBER_ERROR;
	  }
	}
	else {
	  if (fast_json_isdigit (c)) {
	    do {
	      c = fast_json_getc_save (json);
	    } while (fast_json_isdigit (c));
	  }
	  else {
	    save = fast_json_ungetc_save (json, 0);
	    fast_json_store_error (json, FAST_JSON_NUMBER_ERROR, save);
	    return FAST_JSON_NUMBER_ERROR;
	  }
	}
      }
      if (c == 'e' || c == 'E' || (hex && (c == 'p' || c == 'P'))) {
	c = fast_json_getc_save (json);
	if (c == '+' || c == '-') {
	  c = fast_json_getc_save (json);
	}
	if (fast_json_isdigit (c)) {
	  do {
	    c = fast_json_getc_save (json);
	  } while (fast_json_isdigit (c));
	}
	else {
	  save = fast_json_ungetc_save (json, 0);
	  fast_json_store_error (json, FAST_JSON_NUMBER_ERROR, save);
	  return FAST_JSON_NUMBER_ERROR;
	}
      }
      save = fast_json_ungetc_save (json, c);
      fast_json_update_crc (crc, save);
    }
    break;
  case '[':
    error = fast_json_skip_whitespace (json, &c);
    if (error != FAST_JSON_OK) {
      return error;
    }
    if (c == ']') {
      break;
    }
    for (;;) {
      error = fast_json_parse_crc (json, crc, c);
      if (error != FAST_JSON_OK) {
	return error;
      }
      error = fast_json_skip_whitespace (json, &c);
      if (error != FAST_JSON_OK) {
	return error;
      }
      if (c != ',') {
	break;
      }
      error = fast_json_skip_whitespace (json, &c);
      if (error != FAST_JSON_OK) {
	return error;
      }
    }
    if (c != ']') {
      fast_json_store_error (json, FAST_JSON_ARRAY_END_ERROR, "");
      return FAST_JSON_ARRAY_END_ERROR;
    }
    break;
  case '{':
    error = fast_json_skip_whitespace (json, &c);
    if (error != FAST_JSON_OK) {
      return error;
    }
    if (c == '}') {
      break;
    }
    for (;;) {
      if (c != '"') {
	fast_json_store_error (json, FAST_JSON_STRING_START_ERROR, "");
	return FAST_JSON_STRING_START_ERROR;
      }
      c = fast_json_getc (json);
      fast_json_getc_save_start (json, c);
      while (c > 0 && c != '"') {
	if (c == '\\') {
	  fast_json_getc_save (json);
	}
	c = fast_json_getc_save (json);
      }
      save = fast_json_ungetc_save (json, c);
      fast_json_update_crc (crc, save);
      c = fast_json_getc (json);
      if (c != '"') {
	fast_json_store_error (json, FAST_JSON_STRING_END_ERROR, save);
	return FAST_JSON_STRING_END_ERROR;
      }
      error = fast_json_skip_whitespace (json, &c);
      if (error != FAST_JSON_OK) {
	return error;
      }
      if (c != ':') {
	fast_json_store_error (json, FAST_JSON_OBJECT_SEPERATOR_ERROR, "");
	return FAST_JSON_OBJECT_SEPERATOR_ERROR;
      }
      error = fast_json_skip_whitespace (json, &c);
      if (error != FAST_JSON_OK) {
	return error;
      }
      error = fast_json_parse_crc (json, crc, c);
      if (error != FAST_JSON_OK) {
	return error;
      }
      error = fast_json_skip_whitespace (json, &c);
      if (error != FAST_JSON_OK) {
	return error;
      }
      if (c != ',') {
	break;
      }
      error = fast_json_skip_whitespace (json, &c);
      if (error != FAST_JSON_OK) {
	return error;
      }
    }
    if (c != '}') {
      fast_json_store_error (json, FAST_JSON_OBJECT_END_ERROR, "");
      return FAST_JSON_OBJECT_END_ERROR;
    }
    break;
  default:
    save = fast_json_ungetc_save (json, 0);
    fast_json_store_error (json, FAST_JSON_VALUE_ERROR, save);
    return FAST_JSON_VALUE_ERROR;
  }
  return FAST_JSON_OK;
}

static FAST_JSON_ERROR_ENUM
fast_json_calc_crc_all (FAST_JSON_TYPE json, unsigned int *res_crc)
{
  int c;
  FAST_JSON_ERROR_ENUM error;
  unsigned int crc = 0xFFFFFFFFu;

  json->error = FAST_JSON_OK;
  json->error_str[0] = '\0';
  json->line = 1;
  json->column = 0;
  json->position = 0;
  json->last_char = 0;
  error = fast_json_skip_whitespace (json, &c);
  if (error == FAST_JSON_OK) {
    if (c != FAST_JSON_EOF) {
      error = fast_json_parse_crc (json, &crc, c);
      if (error == FAST_JSON_OK) {
	if ((json->options & FAST_JSON_NO_EOF_CHECK) == 0) {
	  error = fast_json_skip_whitespace (json, &c);
	  if (error == FAST_JSON_OK) {
	    fast_json_ungetc (json, c);
	    if (c != FAST_JSON_EOF) {
	      fast_json_store_error (json, FAST_JSON_OBJECT_END_ERROR, "");
	      error = FAST_JSON_OBJECT_END_ERROR;
	    }
	  }
	}
      }
    }
    else {
      error = FAST_JSON_NO_DATA_ERROR;
    }
  }
  *res_crc = crc ^ 0xFFFFFFFFu;
  return error;
}

FAST_JSON_ERROR_ENUM
fast_json_calc_crc_string (FAST_JSON_TYPE json, const char *json_str,
			   unsigned int *res_crc)
{
  FAST_JSON_ERROR_ENUM error = FAST_JSON_OK;

  if (json && json_str && res_crc) {
    json->getc = fast_json_getc_string;
    json->u.str.string = json_str;
    json->u.str.pos = 0;
    error = fast_json_calc_crc_all (json, res_crc);
  }
  return error;
}

FAST_JSON_ERROR_ENUM
fast_json_calc_crc_string_len (FAST_JSON_TYPE json, const char *json_str,
			       size_t len, unsigned int *res_crc)
{
  FAST_JSON_ERROR_ENUM error = FAST_JSON_OK;

  if (json && json_str && res_crc) {
    json->getc = fast_json_getc_string_len;
    json->u.str.string = json_str;
    json->u.str.pos = 0;
    json->u.str.len = len;
    error = fast_json_calc_crc_all (json, res_crc);
  }
  return error;
}

FAST_JSON_ERROR_ENUM
fast_json_calc_crc_file (FAST_JSON_TYPE json, FILE * fp,
			 unsigned int *res_crc)
{
  FAST_JSON_ERROR_ENUM error = FAST_JSON_OK;

  if (json && fp && res_crc) {
    json->getc = fast_json_getc_file;
    json->u.fp = fp;
    error = fast_json_calc_crc_all (json, res_crc);
    if (error == FAST_JSON_OK && json->last_char) {
      ungetc (json->last_char, fp);
    }
  }
  return error;
}

FAST_JSON_ERROR_ENUM
fast_json_calc_crc_file_name (FAST_JSON_TYPE json, const char *name,
			      unsigned int *res_crc)
{
  FAST_JSON_ERROR_ENUM error = FAST_JSON_OK;

  if (json && name && res_crc) {
    FILE *fp = fopen (name, "r");

    if (fp) {
      json->getc = fast_json_getc_file;
      json->u.fp = fp;
      error = fast_json_calc_crc_all (json, res_crc);
      fclose (fp);
    }
  }
  return error;
}

FAST_JSON_ERROR_ENUM
fast_json_calc_crc_fd (FAST_JSON_TYPE json, int fd, unsigned int *res_crc)
{
  FAST_JSON_ERROR_ENUM error = FAST_JSON_OK;

  if (json && res_crc) {
    json->getc = fast_json_getc_fd;
    json->u.fd.fd = fd;
    json->u.fd.pos = 0;
    json->u.fd.len = 0;
    error = fast_json_calc_crc_all (json, res_crc);
  }
  return error;
}
