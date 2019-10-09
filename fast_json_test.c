/* Copyright 2019 Herman ten Brugge
 *
 * Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
 * http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
 * <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
 * option. This file may not be copied, modified, or distributed
 * except according to those terms.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <locale.h>
#if USE_FAST_CONVERT
#include "fast_convert/fast_convert.h"
#endif
#include "fast_json.h"

#define	TEST1_FILE	"test1.json"
#define	TEST2_FILE	"test2.json"

static void
parser_check_error (FAST_JSON_TYPE json, const char *str,
		    FAST_JSON_ERROR_ENUM error, unsigned int line,
		    const char *error_str, const char *error_str2)
{
  FAST_JSON_DATA_TYPE v;

  v = fast_json_parse_string (json, str);
  if (v != NULL) {
    fprintf (stderr, "Unexpected value: %s\n",
	     fast_json_print_string (json, v, 1));
    exit (1);
  }
  if (fast_json_parser_error (json) != error) {
    fprintf (stderr, "Unexpected error: expected %s, received %s\n",
	     fast_json_error_str (error),
	     fast_json_error_str (fast_json_parser_error (json)));
    exit (1);
  }
  if (fast_json_parser_line (json) != line) {
    fprintf (stderr, "Unexpected line: expected %u, received %lu\n",
	     line, (unsigned long) fast_json_parser_line (json));
    exit (1);
  }
  if (strcmp (fast_json_parser_error_str (json), error_str) != 0) {
    fprintf (stderr,
	     "Unexpected error string: expected '%s', received '%s'\n",
	     error_str, fast_json_parser_error_str (json));
    exit (1);
  }
  v = fast_json_parse_string2 (json, str);
  if (v != NULL) {
    fprintf (stderr, "Unexpected value: %s\n",
	     fast_json_print_string (json, v, 1));
    exit (1);
  }
  if (fast_json_parser_error (json) != error) {
    fprintf (stderr, "Unexpected error: expected %s, received %s\n",
	     fast_json_error_str (error),
	     fast_json_error_str (fast_json_parser_error (json)));
    exit (1);
  }
  if (fast_json_parser_line (json) != line) {
    fprintf (stderr, "Unexpected line: expected %u, received %lu\n",
	     line, (unsigned long) fast_json_parser_line (json));
    exit (1);
  }
  if (strcmp (fast_json_parser_error_str (json), error_str2) != 0) {
    fprintf (stderr,
	     "Unexpected error string: expected '%s', received '%s'\n",
	     error_str2, fast_json_parser_error_str (json));
    exit (1);
  }
}

static void
parser_check_noerror (FAST_JSON_TYPE json, FAST_JSON_DATA_TYPE v)
{
  if (v == NULL) {
    fprintf (stderr, "Unexpected error: %lu:%s '%20.20s'\n",
	     (unsigned long) fast_json_parser_line (json),
	     fast_json_error_str (fast_json_parser_error (json)),
	     fast_json_parser_error_str (json));
    exit (1);
  }
  fast_json_value_free (json, v);
}

static void
check_noerror (FAST_JSON_TYPE json, FAST_JSON_DATA_TYPE v)
{
  if (v == NULL) {
    fprintf (stderr, "Unexpected error. Value not defined\n");
    exit (1);
  }
  fast_json_value_free (json, v);
}

static void
tst_error (const char *error, const char *expected)
{
  if (strcmp (error, expected) != 0) {
    fprintf (stderr, "Unexpected error expected: '%s', received: '%s'\n",
	     expected, error);
  }
}

static uint64_t malloc_n_malloc = 0;
static uint64_t malloc_n_free = 0;
static uint64_t malloc_n_realloc = 0;
static uint64_t malloc_size = 0;
static uint64_t malloc_max_size = 0;

static void *
my_malloc (size_t size)
{
  void *ptr = malloc (size + 8);

  malloc_n_malloc++;
  if (ptr) {
    malloc_size += size;
    if (malloc_size > malloc_max_size) {
      malloc_max_size = malloc_size;
    }
    *((size_t *) ptr) = size;
    return (char *) ptr + 8;
  }
  return ptr;
}

static void *
my_realloc (void *ptr, size_t new_size)
{
  void *new_ptr;

  malloc_n_realloc++;
  if (ptr) {
    size_t size;

    ptr = (void *) ((char *) ptr - 8);
    size = *((size_t *) ptr);
    malloc_size -= size;
    new_ptr = realloc (ptr, new_size + 8);
  }
  else {
    new_ptr = malloc (new_size + 8);
  }
  if (new_ptr) {
    malloc_size += new_size;
    if (malloc_size > malloc_max_size) {
      malloc_max_size = malloc_size;
    }
    *((size_t *) new_ptr) = new_size;
    return (char *) new_ptr + 8;
  }
  return new_ptr;
}

static void
my_free (void *ptr)
{
  malloc_n_free++;
  if (ptr) {
    size_t size;
    volatile char *p;

    ptr = (void *) ((char *) ptr - 8);
    size = *((size_t *) ptr);
    malloc_size -= size;
    p = (volatile char *) ptr;
    while (size--) {
      *p++ = 0;
    }
    free (ptr);
  }
}

int
main (void)
{
  unsigned int i;
  unsigned int j;
  char *cp;
  char *np;
  FILE *fp;
  int fd;
  struct stat st;
  FAST_JSON_DATA_TYPE v;
  FAST_JSON_DATA_TYPE n;
  FAST_JSON_TYPE json;
  FAST_JSON_ERROR_ENUM e;
  FAST_JSON_VALUE_TYPE t;
  unsigned int bool_numbers[] = { 0, 1, 0, 1 };
  fast_json_int_64 int_numbers[] = { 1, 2, 3 };
  double double_numbers[] = { 1, 2, 3 };
  const char *string_values[] = { "1", "2", "3" };
  const char *multi_str = "42true";
  char str[100];

  json = fast_json_create (my_malloc, my_realloc, my_free);
  fast_json_max_reuse (json, 1);

  /* json error tests */
  fast_json_parse_string (json, "\n\n\n\n\n      -");
  if (fast_json_parser_column (json) != 7) {
    fprintf (stderr, "Unexpected column: expected 7, received %lu\n",
	     (unsigned long) fast_json_parser_column (json));
    exit (1);
  }
  if (fast_json_parser_position (json) != 12) {
    fprintf (stderr, "Unexpected position: expected 12, received %lu\n",
	     (unsigned long) fast_json_parser_position (json));
    exit (1);
  }
  fast_json_parse_string2 (json, "\n\n\n\n\n      -");
  if (fast_json_parser_column (json) != 7) {
    fprintf (stderr, "Unexpected column: expected 7, received %lu\n",
	     (unsigned long) fast_json_parser_column (json));
    exit (1);
  }
  if (fast_json_parser_position (json) != 12) {
    fprintf (stderr, "Unexpected position: expected 12, received %lu\n",
	     (unsigned long) fast_json_parser_position (json));
    exit (1);
  }
  parser_check_error (json, "\n\n\n\n\n      -", FAST_JSON_NUMBER_ERROR, 6,
		      "-", "-");
  parser_check_error (json, "\"\\ujjjj\"", FAST_JSON_UNICODE_ERROR, 1, "jjjj",
		      "\\ujjjj\"");
  parser_check_error (json, "\"\\u0000\"", FAST_JSON_UNICODE_0_ERROR, 1,
		      "0000", "\\u0000\"");
  parser_check_error (json, "{ [", FAST_JSON_STRING_START_ERROR, 1, "", "");
  parser_check_error (json, "{\"a :", FAST_JSON_STRING_END_ERROR, 1, "a :",
		      "a ");
  parser_check_error (json, "[ 0", FAST_JSON_ARRAY_END_ERROR, 1, "", "");
  parser_check_error (json, "{ \"a\" ", FAST_JSON_OBJECT_SEPERATOR_ERROR, 1,
		      "", "");
  parser_check_error (json, "{\"a\" : 0", FAST_JSON_OBJECT_END_ERROR, 1, "",
		      "");
  parser_check_error (json, "\"a\" : 0", FAST_JSON_OBJECT_END_ERROR, 1, "",
		      "");
  parser_check_error (json, "v", FAST_JSON_VALUE_ERROR, 1, "v", "v");
  parser_check_error (json, "fail", FAST_JSON_VALUE_ERROR, 1, "fail", "fail");
  parser_check_error (json, "tail", FAST_JSON_VALUE_ERROR, 1, "tail", "tail");
  parser_check_error (json, "/test", FAST_JSON_COMMENT_ERROR, 1, "/t",
		      "/test");

  tst_error (fast_json_error_str (FAST_JSON_OK), "OK");
  tst_error (fast_json_error_str (FAST_JSON_MALLOC_ERROR), "Malloc error");
  tst_error (fast_json_error_str (FAST_JSON_COMMENT_ERROR), "Comment error");
  tst_error (fast_json_error_str (FAST_JSON_NUMBER_ERROR), "Number error");
  tst_error (fast_json_error_str (FAST_JSON_UNICODE_ERROR), "Unicode error");
  tst_error (fast_json_error_str (FAST_JSON_UNICODE_0_ERROR),
	     "Unicode 0 error");
  tst_error (fast_json_error_str (FAST_JSON_STRING_START_ERROR),
	     "String start error");
  tst_error (fast_json_error_str (FAST_JSON_STRING_END_ERROR),
	     "String end error");
  tst_error (fast_json_error_str (FAST_JSON_VALUE_ERROR), "Value error");
  tst_error (fast_json_error_str (FAST_JSON_ARRAY_END_ERROR),
	     "Array end error");
  tst_error (fast_json_error_str (FAST_JSON_OBJECT_SEPERATOR_ERROR),
	     "Object seperator error");
  tst_error (fast_json_error_str (FAST_JSON_OBJECT_END_ERROR),
	     "Object end error");
  tst_error (fast_json_error_str (FAST_JSON_PARSE_ERROR), "Parse error");
  tst_error (fast_json_error_str (FAST_JSON_NO_DATA_ERROR), "No data error");
  tst_error (fast_json_error_str (FAST_JSON_INDEX_ERROR), "Index error");
  if (fast_json_error_str ((FAST_JSON_ERROR_ENUM) - 1) != NULL) {
    fprintf (stderr, "Unexpected error\n");
    exit (1);
  }

  /* extra math tests */
  fast_json_options (json, 0);
  parser_check_error (json, "+1", FAST_JSON_NUMBER_ERROR, 1, "+", "+1");
  parser_check_error (json, "+inf", FAST_JSON_NUMBER_ERROR, 1, "+", "+inf");
  parser_check_error (json, "-inf", FAST_JSON_NUMBER_ERROR, 1, "-inf",
		      "-inf");
  parser_check_error (json, "infinity", FAST_JSON_NUMBER_ERROR, 1, "infinity",
		      "infinity");
  parser_check_error (json, "nan", FAST_JSON_NUMBER_ERROR, 1, "nan", "nan");
  parser_check_error (json, "+nan", FAST_JSON_NUMBER_ERROR, 1, "+", "+nan");
  parser_check_error (json, "-nan", FAST_JSON_NUMBER_ERROR, 1, "-nan",
		      "-nan");
  parser_check_error (json, "nan(123)", FAST_JSON_NUMBER_ERROR, 1, "nan",
		      "nan(123)");
  fast_json_options (json, FAST_JSON_INF_NAN);
  parser_check_error (json, "nan(123", FAST_JSON_NUMBER_ERROR, 1, "nan(123",
		      "nan(123");
  parser_check_error (json, "+nan(123", FAST_JSON_NUMBER_ERROR, 1, "+nan(123",
		      "+nan(123");

  v = fast_json_parse_string (json, "+1");
  parser_check_noerror (json, v);
  v = fast_json_parse_string (json, "+inf");
  parser_check_noerror (json, v);
  v = fast_json_parse_string (json, "+infinity");
  parser_check_noerror (json, v);
  v = fast_json_parse_string (json, "-inf");
  parser_check_noerror (json, v);
  v = fast_json_parse_string (json, "-infinity");
  parser_check_noerror (json, v);
  v = fast_json_parse_string (json, "inf");
  parser_check_noerror (json, v);
  v = fast_json_parse_string (json, "infinity");
  parser_check_noerror (json, v);
  v = fast_json_parse_string (json, "nan");
  parser_check_noerror (json, v);
  v = fast_json_parse_string (json, "nan(123)");
  parser_check_noerror (json, v);
  v = fast_json_parse_string (json, "-nan");
  parser_check_noerror (json, v);
  v = fast_json_parse_string (json, "+nan(123)");
  parser_check_noerror (json, v);
  v = fast_json_parse_string (json, "[]");
  parser_check_noerror (json, v);
  v = fast_json_parse_string (json, "{}");
  parser_check_noerror (json, v);

  v = fast_json_parse_string2 (json, "+1");
  parser_check_noerror (json, v);
  v = fast_json_parse_string2 (json, "+inf");
  parser_check_noerror (json, v);
  v = fast_json_parse_string2 (json, "+infinity");
  parser_check_noerror (json, v);
  v = fast_json_parse_string2 (json, "-inf");
  parser_check_noerror (json, v);
  v = fast_json_parse_string2 (json, "-infinity");
  parser_check_noerror (json, v);
  v = fast_json_parse_string2 (json, "inf");
  parser_check_noerror (json, v);
  v = fast_json_parse_string2 (json, "infinity");
  parser_check_noerror (json, v);
  v = fast_json_parse_string2 (json, "nan");
  parser_check_noerror (json, v);
  v = fast_json_parse_string2 (json, "nan(123)");
  parser_check_noerror (json, v);
  v = fast_json_parse_string2 (json, "-nan");
  parser_check_noerror (json, v);
  v = fast_json_parse_string2 (json, "+nan(123)");
  parser_check_noerror (json, v);
  v = fast_json_parse_string2 (json, "[]");
  parser_check_noerror (json, v);
  v = fast_json_parse_string2 (json, "{}");
  parser_check_noerror (json, v);

  v = fast_json_parse_string (json, "1");
  if (fast_json_get_type (v) != FAST_JSON_INTEGER) {
    fprintf (stderr, "Unexpected type: %d\n", fast_json_get_type (v));
    exit (1);
  }
  parser_check_noerror (json, v);
  fast_json_options (json, FAST_JSON_PARSE_INT_AS_DOUBLE);
  v = fast_json_parse_string (json, "1");
  if (fast_json_get_type (v) != FAST_JSON_DOUBLE) {
    fprintf (stderr, "Unexpected type: %d\n", fast_json_get_type (v));
    exit (1);
  }
  parser_check_noerror (json, v);
  fast_json_options (json, 0);

  v = fast_json_parse_string2 (json, "1");
  if (fast_json_get_type (v) != FAST_JSON_INTEGER) {
    fprintf (stderr, "Unexpected type: %d\n", fast_json_get_type (v));
    exit (1);
  }
  parser_check_noerror (json, v);
  fast_json_options (json, FAST_JSON_PARSE_INT_AS_DOUBLE);
  v = fast_json_parse_string2 (json, "1");
  if (fast_json_get_type (v) != FAST_JSON_DOUBLE) {
    fprintf (stderr, "Unexpected type: %d\n", fast_json_get_type (v));
    exit (1);
  }
  parser_check_noerror (json, v);

  fast_json_options (json, FAST_JSON_BIG_ALLOC);
  v =
    fast_json_parse_string (json,
			    "// test\n \t\r\n/* test2 */\n{\"abcdefghijklmnopqrst\":[1,-1,1.2e+1,1.5E-3,\"s12345678\",true,false,null],\"b\":7}");
  cp = fast_json_print_string (json, v, 0);
  i = fast_json_print_string_len (json, v, NULL, 0, 0);
  np = (char *) malloc (i);
  j = fast_json_print_string_len (json, v, np, i, 0);
  if (i != j || strcmp (cp, np) != 0) {
    fprintf (stderr, "print string failed: '%s' != '%s'\n", cp, np);
  }
  free (np);
  parser_check_noerror (json, v);
  if (strcmp
      (cp,
       "{\"abcdefghijklmnopqrst\":[1,-1,12.0,0.0015,\"s12345678\",true,false,null],\"b\":7}")
      != 0) {
    fprintf (stderr, "Unexpected string: expected '%s', received '%s'\n",
	     "{\"abcdefghijklmnopqrst\":[1,-1,12.0,0.0015,\"s12345678\",true,false,null],\"b\":7}",
	     cp);
    exit (1);
  }
  fast_json_release_print_value (json, cp);
  v =
    fast_json_parse_string (json,
			    "{\"a\":[1,1.2,\"s\",true,false,null],\"b\":7}");
  cp = fast_json_print_string (json, v, 1);
  parser_check_noerror (json, v);
  if (strcmp
      (cp,
       "{\n  \"a\": [\n    1,\n    1.2,\n    \"s\",\n    true,\n    false,\n    null\n  ],\n  \"b\": 7\n}\n")
      != 0) {
    fprintf (stderr, "Unexpected string: expected '%s', received '%s'\n",
	     "{\n  \"a\": [\n    1,\n    1.2,\n    \"s\",\n    true,\n    false,\n    null\n  ],\n  \"b\": 7\n}\n",
	     cp);
    exit (1);
  }
  fast_json_release_print_value (json, cp);
  v =
    fast_json_parse_string (json,
			    "\"\\u12aB\\u0020\\u0123\\uD834\\uDD1E\\\\\\/\\b\\f\\n\\r\\t\\\"\"");
  cp = fast_json_print_string (json, v, 0);
  parser_check_noerror (json, v);
  if (strcmp
      (cp,
       "\"\\u12AB \\u0123\\uD834\\uDD1E\\\\\\/\\b\\f\\n\\r\\t\\\"\"") != 0) {
    fprintf (stderr, "Unexpected string: expected '%s', received '%s'\n",
	     "\"\\u12AB \\u0123\\uD834\\uDD1E\\\\\\/\\b\\f\\n\\r\\t\\\"\"",
	     cp);
    exit (1);
  }
  fast_json_release_print_value (json, cp);
  v = fast_json_parse_string (json, "[[[[[0]]]]]");
  cp = fast_json_print_string (json, v, 1);
  parser_check_noerror (json, v);
  if (strcmp
      (cp,
       "[\n  [\n    [\n      [\n\t[\n\t  0\n\t]\n      ]\n    ]\n  ]\n]\n") !=
      0) {
    fprintf (stderr, "Unexpected string: expected '%s', received '%s'\n",
	     "[\n  [\n    [\n      [\n\t[\n\t  0\n\t]\n      ]\n    ]\n  ]\n]\n",
	     cp);
    exit (1);
  }
  fast_json_release_print_value (json, cp);

  v =
    fast_json_parse_string2 (json,
			     "// test\n \t\r\n/* test2 */\n{\"a\":[1,-1,1.2e+1,1.5E-3,\"s\",true,false,null],\"b\":7}");
  cp = fast_json_print_string (json, v, 0);
  i = fast_json_print_string_len (json, v, NULL, 0, 0);
  np = (char *) malloc (i);
  j = fast_json_print_string_len (json, v, np, i, 0);
  if (i != j || strcmp (cp, np) != 0) {
    fprintf (stderr, "print string failed: '%s' != '%s'\n", cp, np);
  }
  free (np);
  parser_check_noerror (json, v);
  if (strcmp (cp, "{\"a\":[1,-1,12.0,0.0015,\"s\",true,false,null],\"b\":7}")
      != 0) {
    fprintf (stderr, "Unexpected string: expected '%s', received '%s'\n",
	     "{\"a\":[1,-1,12.0,0.0015,\"s\",true,false,null],\"b\":7}", cp);
    exit (1);
  }
  fast_json_release_print_value (json, cp);
  v =
    fast_json_parse_string2 (json,
			     "{\"a\":[1,1.2,\"s\",true,false,null],\"b\":7}");
  cp = fast_json_print_string (json, v, 1);
  parser_check_noerror (json, v);
  if (strcmp
      (cp,
       "{\n  \"a\": [\n    1,\n    1.2,\n    \"s\",\n    true,\n    false,\n    null\n  ],\n  \"b\": 7\n}\n")
      != 0) {
    fprintf (stderr, "Unexpected string: expected '%s', received '%s'\n",
	     "{\n  \"a\": [\n    1,\n    1.2,\n    \"s\",\n    true,\n    false,\n    null\n  ],\n  \"b\": 7\n}\n",
	     cp);
    exit (1);
  }
  fast_json_release_print_value (json, cp);
  v =
    fast_json_parse_string2 (json,
			     "\"\\u12aB\\u0020\\u0123\\uD834\\uDD1E\\\\\\/\\b\\f\\n\\r\\t\\\"\"");
  cp = fast_json_print_string (json, v, 0);
  parser_check_noerror (json, v);
  if (strcmp
      (cp,
       "\"\\u12AB \\u0123\\uD834\\uDD1E\\\\\\/\\b\\f\\n\\r\\t\\\"\"") != 0) {
    fprintf (stderr, "Unexpected string: expected '%s', received '%s'\n",
	     "\"\\u12AB \\u0123\\uD834\\uDD1E\\\\\\/\\b\\f\\n\\r\\t\\\"\"",
	     cp);
    exit (1);
  }
  fast_json_release_print_value (json, cp);
  v = fast_json_parse_string2 (json, "[[[[[0]]]]]");
  cp = fast_json_print_string (json, v, 1);
  parser_check_noerror (json, v);
  if (strcmp
      (cp,
       "[\n  [\n    [\n      [\n\t[\n\t  0\n\t]\n      ]\n    ]\n  ]\n]\n") !=
      0) {
    fprintf (stderr, "Unexpected string: expected '%s', received '%s'\n",
	     "[\n  [\n    [\n      [\n\t[\n\t  0\n\t]\n      ]\n    ]\n  ]\n]\n",
	     cp);
    exit (1);
  }
  fast_json_release_print_value (json, cp);

  v = fast_json_create_null (json);
  check_noerror (json, v);
  v = fast_json_create_true (json);
  check_noerror (json, v);
  v = fast_json_create_false (json);
  check_noerror (json, v);
  v = fast_json_create_boolean_value (json, 1);
  check_noerror (json, v);
  v = fast_json_create_integer_value (json, 1);
  check_noerror (json, v);
  v = fast_json_create_double_value (json, 1);
  check_noerror (json, v);
  v = fast_json_create_string (json, "1");
  check_noerror (json, v);
  v = fast_json_create_array (json);
  check_noerror (json, v);
  v = fast_json_create_object (json);
  check_noerror (json, v);

  v =
    fast_json_create_boolean_array (json, bool_numbers,
				    sizeof (bool_numbers) /
				    sizeof (bool_numbers[0]));
  check_noerror (json, v);
  v =
    fast_json_create_integer_array (json, int_numbers,
				    sizeof (int_numbers) /
				    sizeof (int_numbers[0]));
  check_noerror (json, v);
  v =
    fast_json_create_double_array (json, double_numbers,
				   sizeof (double_numbers) /
				   sizeof (double_numbers[0]));
  check_noerror (json, v);
  v =
    fast_json_create_string_array (json, string_values,
				   sizeof (string_values) /
				   sizeof (string_values[0]));
  check_noerror (json, v);

  v = fast_json_create_integer_value (json, 1);
  e = fast_json_add_array (json, NULL, v);
  if (e != FAST_JSON_MALLOC_ERROR) {
    fprintf (stderr, "Unexpected error: %s\n", fast_json_error_str (e));
    exit (1);
  }
  fast_json_value_free (json, v);
  v =
    fast_json_create_integer_array (json, int_numbers,
				    sizeof (int_numbers) /
				    sizeof (int_numbers[0]));
  e = fast_json_add_array (json, v, NULL);
  if (e != FAST_JSON_MALLOC_ERROR) {
    fprintf (stderr, "Unexpected error: %s\n", fast_json_error_str (e));
    exit (1);
  }
  fast_json_value_free (json, v);
  v =
    fast_json_create_integer_array (json, int_numbers,
				    sizeof (int_numbers) /
				    sizeof (int_numbers[0]));
  e = fast_json_add_array (json, v, fast_json_create_integer_value (json, 1));
  if (e != FAST_JSON_OK) {
    fprintf (stderr, "Unexpected error: %s\n", fast_json_error_str (e));
    exit (1);
  }
  fast_json_value_free (json, v);

  v = fast_json_create_integer_value (json, 1);
  e = fast_json_add_object (json, NULL, "a", v);
  if (e != FAST_JSON_MALLOC_ERROR) {
    fprintf (stderr, "Unexpected error: %s\n", fast_json_error_str (e));
    exit (1);
  }
  fast_json_value_free (json, v);
  v = fast_json_create_object (json);
  e = fast_json_add_object (json, v, "a", NULL);
  if (e != FAST_JSON_MALLOC_ERROR) {
    fprintf (stderr, "Unexpected error: %s\n", fast_json_error_str (e));
    exit (1);
  }
  fast_json_value_free (json, v);
  v = fast_json_create_object (json);
  n = fast_json_create_integer_value (json, 1);
  e = fast_json_add_object (json, v, NULL, n);
  if (e != FAST_JSON_MALLOC_ERROR) {
    fprintf (stderr, "Unexpected error: %s\n", fast_json_error_str (e));
    exit (1);
  }
  fast_json_value_free (json, v);
  fast_json_value_free (json, n);
  v = fast_json_create_object (json);
  e =
    fast_json_add_object (json, v, "a",
			  fast_json_create_integer_value (json, 1));
  if (e != FAST_JSON_OK) {
    fprintf (stderr, "Unexpected error: %s\n", fast_json_error_str (e));
    exit (1);
  }
  fast_json_value_free (json, v);
  v = fast_json_create_object (json);
  e =
    fast_json_add_object (json, v, "a",
			  fast_json_create_integer_value (json, 1));
  if (e != FAST_JSON_OK) {
    fprintf (stderr, "Unexpected error: %s\n", fast_json_error_str (e));
    exit (1);
  }
  e =
    fast_json_add_object (json, v, "a",
			  fast_json_create_integer_value (json, 2));
  if (e != FAST_JSON_OK) {
    fprintf (stderr, "Unexpected error: %s\n", fast_json_error_str (e));
    exit (1);
  }
  i = fast_json_get_object_size (v);
  if (i != 1) {
    fprintf (stderr, "Unexpected size\n");
    exit (1);
  }
  n = fast_json_get_object_data (v, 0);
  if (fast_json_get_integer (n) != 2) {
    fprintf (stderr, "Unexpected integer\n");
    exit (1);
  }
  fast_json_value_free (json, v);

  v =
    fast_json_create_integer_array (json, int_numbers,
				    sizeof (int_numbers) /
				    sizeof (int_numbers[0]));
  n = fast_json_create_integer_value (json, 1);
  e = fast_json_patch_array (json, v, n, 3);
  if (e != FAST_JSON_INDEX_ERROR) {
    fprintf (stderr, "Unexpected error: %s\n", fast_json_error_str (e));
    exit (1);
  }
  fast_json_value_free (json, v);
  fast_json_value_free (json, n);
  v =
    fast_json_create_integer_array (json, int_numbers,
				    sizeof (int_numbers) /
				    sizeof (int_numbers[0]));
  e = fast_json_patch_array (json, v, NULL, 1);
  if (e != FAST_JSON_INDEX_ERROR) {
    fprintf (stderr, "Unexpected error: %s\n", fast_json_error_str (e));
    exit (1);
  }
  fast_json_value_free (json, v);
  v =
    fast_json_create_integer_array (json, int_numbers,
				    sizeof (int_numbers) /
				    sizeof (int_numbers[0]));
  e = fast_json_patch_array (json, v, v, 1);
  if (e != FAST_JSON_INDEX_ERROR) {
    fprintf (stderr, "Unexpected error: %s\n", fast_json_error_str (e));
    exit (1);
  }
  fast_json_value_free (json, v);
  v =
    fast_json_create_integer_array (json, int_numbers,
				    sizeof (int_numbers) /
				    sizeof (int_numbers[0]));
  e =
    fast_json_patch_array (json, v, fast_json_create_integer_value (json, 1),
			   1);
  if (e != FAST_JSON_OK) {
    fprintf (stderr, "Unexpected error: %s\n", fast_json_error_str (e));
    exit (1);
  }
  cp = fast_json_print_string (json, v, 0);
  if (strcmp (cp, "[1,1,3]") != 0) {
    fprintf (stderr, "Unexpected string: expected '%s', received '%s'\n",
	     "[1,1,3]", cp);
    exit (1);
  }
  fast_json_release_print_value (json, cp);
  fast_json_value_free (json, v);

  v =
    fast_json_create_integer_array (json, int_numbers,
				    sizeof (int_numbers) /
				    sizeof (int_numbers[0]));
  n = fast_json_create_integer_value (json, 1);
  e = fast_json_insert_array (json, v, n, 3);
  if (e != FAST_JSON_INDEX_ERROR) {
    fprintf (stderr, "Unexpected error: %s\n", fast_json_error_str (e));
    exit (1);
  }
  fast_json_value_free (json, v);
  fast_json_value_free (json, n);
  v =
    fast_json_create_integer_array (json, int_numbers,
				    sizeof (int_numbers) /
				    sizeof (int_numbers[0]));
  e = fast_json_insert_array (json, v, NULL, 1);
  if (e != FAST_JSON_INDEX_ERROR) {
    fprintf (stderr, "Unexpected error: %s\n", fast_json_error_str (e));
    exit (1);
  }
  fast_json_value_free (json, v);
  v =
    fast_json_create_integer_array (json, int_numbers,
				    sizeof (int_numbers) /
				    sizeof (int_numbers[0]));
  e = fast_json_insert_array (json, v, v, 1);
  if (e != FAST_JSON_INDEX_ERROR) {
    fprintf (stderr, "Unexpected error: %s\n", fast_json_error_str (e));
    exit (1);
  }
  fast_json_value_free (json, v);
  v =
    fast_json_create_integer_array (json, int_numbers,
				    sizeof (int_numbers) /
				    sizeof (int_numbers[0]));
  e =
    fast_json_insert_array (json, v, fast_json_create_integer_value (json, 1),
			    1);
  if (e != FAST_JSON_OK) {
    fprintf (stderr, "Unexpected error: %s\n", fast_json_error_str (e));
    exit (1);
  }
  cp = fast_json_print_string (json, v, 0);
  if (strcmp (cp, "[1,1,2,3]") != 0) {
    fprintf (stderr, "Unexpected string: expected '%s', received '%s'\n",
	     "[1,1,1,3]", cp);
    exit (1);
  }
  fast_json_release_print_value (json, cp);
  fast_json_value_free (json, v);

  v =
    fast_json_create_integer_array (json, int_numbers,
				    sizeof (int_numbers) /
				    sizeof (int_numbers[0]));
  e = fast_json_remove_array (json, v, 3);
  if (e != FAST_JSON_INDEX_ERROR) {
    fprintf (stderr, "Unexpected error: %s\n", fast_json_error_str (e));
    exit (1);
  }
  fast_json_value_free (json, v);
  v =
    fast_json_create_integer_array (json, int_numbers,
				    sizeof (int_numbers) /
				    sizeof (int_numbers[0]));
  e = fast_json_remove_array (json, v, 1);
  if (e != FAST_JSON_OK) {
    fprintf (stderr, "Unexpected error: %s\n", fast_json_error_str (e));
    exit (1);
  }
  cp = fast_json_print_string (json, v, 0);
  if (strcmp (cp, "[1,3]") != 0) {
    fprintf (stderr, "Unexpected string: expected '%s', received '%s'\n",
	     "[1,3]", cp);
    exit (1);
  }
  fast_json_release_print_value (json, cp);
  fast_json_value_free (json, v);

  v = fast_json_create_object (json);
  fast_json_add_object (json, v, "a",
			fast_json_create_integer_value (json, 1));
  n = fast_json_create_integer_value (json, 1);
  e = fast_json_patch_object (json, v, n, 2);
  if (e != FAST_JSON_INDEX_ERROR) {
    fprintf (stderr, "Unexpected error: %s\n", fast_json_error_str (e));
    exit (1);
  }
  fast_json_value_free (json, v);
  fast_json_value_free (json, n);
  v = fast_json_create_object (json);
  fast_json_add_object (json, v, "a",
			fast_json_create_integer_value (json, 1));
  e = fast_json_patch_object (json, v, NULL, 0);
  if (e != FAST_JSON_INDEX_ERROR) {
    fprintf (stderr, "Unexpected error: %s\n", fast_json_error_str (e));
    exit (1);
  }
  fast_json_value_free (json, v);
  v = fast_json_create_object (json);
  fast_json_add_object (json, v, "a",
			fast_json_create_integer_value (json, 1));
  n = fast_json_create_integer_value (json, 2);
  e = fast_json_patch_object (json, v, v, 0);
  if (e != FAST_JSON_INDEX_ERROR) {
    fprintf (stderr, "Unexpected error: %s\n", fast_json_error_str (e));
    exit (1);
  }
  fast_json_value_free (json, v);
  fast_json_value_free (json, n);
  v = fast_json_create_object (json);
  fast_json_add_object (json, v, "a",
			fast_json_create_integer_value (json, 1));
  n = fast_json_create_integer_value (json, 2);
  e = fast_json_patch_object (json, v, n, 0);
  if (e != FAST_JSON_OK) {
    fprintf (stderr, "Unexpected error: %s\n", fast_json_error_str (e));
    exit (1);
  }
  cp = fast_json_print_string (json, v, 0);
  if (strcmp (cp, "{\"a\":2}") != 0) {
    fprintf (stderr, "Unexpected string: expected '%s', received '%s'\n",
	     "{\"a\":2}", cp);
    exit (1);
  }
  fast_json_release_print_value (json, cp);
  fast_json_value_free (json, v);

  v = fast_json_create_object (json);
  fast_json_add_object (json, v, "a",
			fast_json_create_integer_value (json, 1));
  n = fast_json_create_integer_value (json, 1);
  e = fast_json_insert_object (json, v, "b", n, 2);
  if (e != FAST_JSON_INDEX_ERROR) {
    fprintf (stderr, "Unexpected error: %s\n", fast_json_error_str (e));
    exit (1);
  }
  fast_json_value_free (json, v);
  fast_json_value_free (json, n);
  v = fast_json_create_object (json);
  fast_json_add_object (json, v, "a",
			fast_json_create_integer_value (json, 1));
  e = fast_json_insert_object (json, v, "b", NULL, 0);
  if (e != FAST_JSON_INDEX_ERROR) {
    fprintf (stderr, "Unexpected error: %s\n", fast_json_error_str (e));
    exit (1);
  }
  fast_json_value_free (json, v);
  v = fast_json_create_object (json);
  n = fast_json_create_integer_value (json, 1);
  fast_json_add_object (json, v, "a",
			fast_json_create_integer_value (json, 1));
  e = fast_json_insert_object (json, v, NULL, n, 0);
  if (e != FAST_JSON_INDEX_ERROR) {
    fprintf (stderr, "Unexpected error: %s\n", fast_json_error_str (e));
    exit (1);
  }
  fast_json_value_free (json, v);
  fast_json_value_free (json, n);
  v = fast_json_create_object (json);
  fast_json_add_object (json, v, "a",
			fast_json_create_integer_value (json, 1));
  n = fast_json_create_integer_value (json, 2);
  e = fast_json_insert_object (json, v, "b", v, 0);
  if (e != FAST_JSON_INDEX_ERROR) {
    fprintf (stderr, "Unexpected error: %s\n", fast_json_error_str (e));
    exit (1);
  }
  fast_json_value_free (json, v);
  fast_json_value_free (json, n);
  v = fast_json_create_object (json);
  fast_json_add_object (json, v, "a",
			fast_json_create_integer_value (json, 1));
  n = fast_json_create_integer_value (json, 2);
  e = fast_json_insert_object (json, v, "b", n, 0);
  if (e != FAST_JSON_OK) {
    fprintf (stderr, "Unexpected error: %s\n", fast_json_error_str (e));
    exit (1);
  }
  cp = fast_json_print_string (json, v, 0);
  if (strcmp (cp, "{\"b\":2,\"a\":1}") != 0) {
    fprintf (stderr, "Unexpected string: expected '%s', received '%s'\n",
	     "{\"b\":2,\"a\":1}", cp);
    exit (1);
  }
  fast_json_release_print_value (json, cp);
  fast_json_value_free (json, v);

  v = fast_json_create_object (json);
  fast_json_add_object (json, v, "a",
			fast_json_create_integer_value (json, 1));
  e = fast_json_remove_object (json, v, 2);
  if (e != FAST_JSON_INDEX_ERROR) {
    fprintf (stderr, "Unexpected error: %s\n", fast_json_error_str (e));
    exit (1);
  }
  fast_json_value_free (json, v);
  v = fast_json_create_object (json);
  fast_json_add_object (json, v, "a",
			fast_json_create_integer_value (json, 1));
  fast_json_add_object (json, v, "b",
			fast_json_create_integer_value (json, 2));
  e = fast_json_remove_object (json, v, 0);
  if (e != FAST_JSON_OK) {
    fprintf (stderr, "Unexpected error: %s\n", fast_json_error_str (e));
    exit (1);
  }
  cp = fast_json_print_string (json, v, 0);
  if (strcmp (cp, "{\"b\":2}") != 0) {
    fprintf (stderr, "Unexpected string: expected '%s', received '%s'\n",
	     "{\"b\":2}", cp);
    exit (1);
  }
  fast_json_release_print_value (json, cp);
  fast_json_value_free (json, v);

  t = fast_json_get_type (NULL);
  if (t != FAST_JSON_NULL) {
    fprintf (stderr, "Unexpected type: %u\n", (unsigned int) t);
    exit (1);
  }
  v =
    fast_json_create_integer_array (json, int_numbers,
				    sizeof (int_numbers) /
				    sizeof (int_numbers[0]));
  t = fast_json_get_type (v);
  if (t != FAST_JSON_ARRAY) {
    fprintf (stderr, "Unexpected type: %u\n", (unsigned int) t);
    exit (1);
  }
  fast_json_value_free (json, v);

  i = fast_json_get_array_size (NULL);
  if (i != 0) {
    fprintf (stderr, "Unexpected size: %u\n", i);
    exit (1);
  }
  v =
    fast_json_create_integer_array (json, int_numbers,
				    sizeof (int_numbers) /
				    sizeof (int_numbers[0]));
  i = fast_json_get_array_size (v);
  if (i != sizeof (int_numbers) / sizeof (int_numbers[0])) {
    fprintf (stderr, "Unexpected size: %u\n", i);
    exit (1);
  }
  fast_json_value_free (json, v);

  n = fast_json_get_array_data (NULL, 0);
  if (n != NULL) {
    fprintf (stderr, "Unexpected data: %s\n",
	     fast_json_print_string (json, n, 0));
    exit (1);
  }
  v =
    fast_json_create_integer_array (json, int_numbers,
				    sizeof (int_numbers) /
				    sizeof (int_numbers[0]));
  n = fast_json_get_array_data (v, 4);
  if (n != NULL) {
    fprintf (stderr, "Unexpected data: %s\n",
	     fast_json_print_string (json, n, 0));
    exit (1);
  }
  fast_json_value_free (json, v);
  v =
    fast_json_create_integer_array (json, int_numbers,
				    sizeof (int_numbers) /
				    sizeof (int_numbers[0]));
  n = fast_json_get_array_data (v, 0);
  cp = fast_json_print_string (json, n, 0);
  if (strcmp (cp, "1") != 0) {
    fprintf (stderr, "Unexpected string: expected '%s', received '%s'\n",
	     "1", cp);
    exit (1);
  }
  fast_json_release_print_value (json, cp);
  fast_json_value_free (json, v);

  i = fast_json_get_object_size (NULL);
  if (i != 0) {
    fprintf (stderr, "Unexpected size: %u\n", i);
    exit (1);
  }
  v = fast_json_create_object (json);
  i = fast_json_get_object_size (v);
  if (i != 0) {
    fprintf (stderr, "Unexpected size: %u\n", i);
    exit (1);
  }
  fast_json_value_free (json, v);

  cp = fast_json_get_object_name (NULL, 0);
  if (cp != NULL) {
    fprintf (stderr, "Unexpected data: %s\n", cp);
    exit (1);
  }
  v = fast_json_create_object (json);
  fast_json_add_object (json, v, "a",
			fast_json_create_integer_value (json, 1));
  cp = fast_json_get_object_name (v, 1);
  if (cp != NULL) {
    fprintf (stderr, "Unexpected data: %s\n", cp);
    exit (1);
  }
  fast_json_value_free (json, v);
  v = fast_json_create_object (json);
  fast_json_add_object (json, v, "a",
			fast_json_create_integer_value (json, 1));
  cp = fast_json_get_object_name (v, 0);
  if (strcmp (cp, "a") != 0) {
    fprintf (stderr, "Unexpected name: %s\n", cp);
    exit (1);
  }
  fast_json_value_free (json, v);

  n = fast_json_get_object_data (NULL, 0);
  if (n != NULL) {
    fprintf (stderr, "Unexpected data: %s\n",
	     fast_json_print_string (json, n, 0));
    exit (1);
  }
  v = fast_json_create_object (json);
  fast_json_add_object (json, v, "a",
			fast_json_create_integer_value (json, 1));
  n = fast_json_get_object_data (v, 1);
  if (n != NULL) {
    fprintf (stderr, "Unexpected data: %s\n",
	     fast_json_print_string (json, n, 0));
    exit (1);
  }
  fast_json_value_free (json, v);
  v = fast_json_create_object (json);
  fast_json_add_object (json, v, "a",
			fast_json_create_integer_value (json, 1));
  n = fast_json_get_object_data (v, 0);
  cp = fast_json_print_string (json, n, 0);
  if (strcmp (cp, "1") != 0) {
    fprintf (stderr, "Unexpected string: expected '%s', received '%s'\n",
	     "1", cp);
    exit (1);
  }
  fast_json_release_print_value (json, cp);
  fast_json_value_free (json, v);

  n = fast_json_get_object_by_name (NULL, "a");
  if (n != NULL) {
    fprintf (stderr, "Unexpected data: %s\n",
	     fast_json_print_string (json, n, 0));
    exit (1);
  }
  v = fast_json_create_object (json);
  fast_json_add_object (json, v, "a",
			fast_json_create_integer_value (json, 1));
  n = fast_json_get_object_by_name (v, NULL);
  if (n != NULL) {
    fprintf (stderr, "Unexpected data: %s\n",
	     fast_json_print_string (json, n, 0));
    exit (1);
  }
  fast_json_value_free (json, v);
  v = fast_json_create_object (json);
  fast_json_add_object (json, v, "a",
			fast_json_create_integer_value (json, 1));
  n = fast_json_get_object_by_name (v, "a");
  cp = fast_json_print_string (json, n, 0);
  if (strcmp (cp, "1") != 0) {
    fprintf (stderr, "Unexpected string: expected '%s', received '%s'\n",
	     "1", cp);
    exit (1);
  }
  fast_json_release_print_value (json, cp);
  fast_json_value_free (json, v);

  if (fast_json_get_integer (NULL) != 0) {
    fprintf (stderr, "Unexpected value\n");
    exit (1);
  }
  v = fast_json_create_integer_value (json, 1);
  if (fast_json_get_integer (v) != 1) {
    fprintf (stderr, "Unexpected value\n");
    exit (1);
  }
  fast_json_value_free (json, v);

  if (fast_json_get_double (NULL) != 0) {
    fprintf (stderr, "Unexpected value\n");
    exit (1);
  }
  v = fast_json_create_double_value (json, 1);
  if (fast_json_get_double (v) != 1) {
    fprintf (stderr, "Unexpected value\n");
    exit (1);
  }
  fast_json_value_free (json, v);

  if (fast_json_get_string (NULL) != NULL) {
    fprintf (stderr, "Unexpected value\n");
    exit (1);
  }
  v = fast_json_create_string (json, "1");
  if (strcmp (fast_json_get_string (v), "1") != 0) {
    fprintf (stderr, "Unexpected value\n");
    exit (1);
  }
  fast_json_value_free (json, v);

  if (fast_json_get_boolean (NULL) != 0) {
    fprintf (stderr, "Unexpected value\n");
    exit (1);
  }
  v = fast_json_create_true (json);
  if (fast_json_get_boolean (v) != 1) {
    fprintf (stderr, "Unexpected value\n");
    exit (1);
  }
  fast_json_value_free (json, v);

  e = fast_json_set_integer (NULL, 2);
  if (e != FAST_JSON_VALUE_ERROR) {
    fprintf (stderr, "Unexpected error\n");
    exit (1);
  }
  v = fast_json_create_integer_value (json, 1);
  e = fast_json_set_integer (v, 2);
  if (e != FAST_JSON_OK || fast_json_get_integer (v) != 2) {
    fprintf (stderr, "Unexpected value\n");
    exit (1);
  }
  fast_json_value_free (json, v);

  e = fast_json_set_double (json, NULL, 2);
  if (e != FAST_JSON_VALUE_ERROR) {
    fprintf (stderr, "Unexpected error\n");
    exit (1);
  }
  v = fast_json_create_double_value (json, 1);
  e = fast_json_set_double (json, v, 2);
  if (e != FAST_JSON_OK || fast_json_get_double (v) != 2) {
    fprintf (stderr, "Unexpected value\n");
    exit (1);
  }
  fast_json_value_free (json, v);

  e = fast_json_set_string (json, NULL, "2");
  if (e != FAST_JSON_VALUE_ERROR) {
    fprintf (stderr, "Unexpected error\n");
    exit (1);
  }
  v = fast_json_create_string (json, "1");
  e = fast_json_set_string (json, v, "2");
  if (e != FAST_JSON_OK || strcmp (fast_json_get_string (v), "2") != 0) {
    fprintf (stderr, "Unexpected value\n");
    exit (1);
  }
  fast_json_value_free (json, v);

  e = fast_json_set_boolean_value (NULL, 0);
  if (e != FAST_JSON_VALUE_ERROR) {
    fprintf (stderr, "Unexpected error\n");
    exit (1);
  }
  v = fast_json_create_true (json);
  e = fast_json_set_boolean_value (v, 0);
  if (e != FAST_JSON_OK || fast_json_get_boolean (v) != 0) {
    fprintf (stderr, "Unexpected value\n");
    exit (1);
  }
  fast_json_value_free (json, v);

  v =
    fast_json_parse_string (json, "[ { \"a\":1 }, -1.0, \"s\", true, null ]");
  n = fast_json_value_copy (json, v);
  cp = fast_json_print_string (json, n, 0);
  if (strcmp (cp, "[{\"a\":1},-1.0,\"s\",true,null]") != 0) {
    fprintf (stderr, "Unexpected value\n");
    exit (1);
  }
  fast_json_release_print_value (json, cp);
  fast_json_value_free (json, v);
  fast_json_value_free (json, n);

  v =
    fast_json_parse_string (json, "[ { \"a\":1 }, -1.0, \"s\", true, null ]");
  n = fast_json_value_copy (json, v);
  if (fast_json_value_equal (v, n) == 0) {
    fprintf (stderr, "equal failed\n");
    exit (1);
  }
  fast_json_value_free (json, v);
  v =
    fast_json_parse_string (json,
			    "[ { \"a\":1 }, -1.0, \"s\", true, false ]");
  if (fast_json_value_equal (v, n) != 0) {
    fprintf (stderr, "equal failed\n");
    exit (1);
  }
  fast_json_value_free (json, v);
  fast_json_value_free (json, n);

  fast_json_options (json, FAST_JSON_SORT_OBJECTS);
  v = fast_json_parse_string (json, "{ \"c\":3, \"b\":2, \"a\":1 }");
  cp = fast_json_print_string (json, v, 0);
  if (strcmp (cp, "{\"a\":1,\"b\":2,\"c\":3}") != 0) {
    fprintf (stderr, "Unexpected value\n");
    exit (1);
  }
  fast_json_release_print_value (json, cp);
  fast_json_value_free (json, v);

  fast_json_options (json, FAST_JSON_ALLOW_OCT_HEX);
  v = fast_json_parse_string (json, "[ 0x3, 0Xd, 0xf.fp7, 0123 ]");
  cp = fast_json_print_string (json, v, 0);
  if (strcmp (cp, "[3,13,2040.0,83]") != 0) {
    fprintf (stderr, "Unexpected value\n");
    exit (1);
  }
  fast_json_release_print_value (json, cp);
  fast_json_value_free (json, v);

  v =
    fast_json_parse_string2 (json,
			     "[ { \"a\":1 }, -1.0, \"s\", true, null ]");
  n = fast_json_value_copy (json, v);
  cp = fast_json_print_string (json, n, 0);
  if (strcmp (cp, "[{\"a\":1},-1.0,\"s\",true,null]") != 0) {
    fprintf (stderr, "Unexpected value\n");
    exit (1);
  }
  fast_json_release_print_value (json, cp);
  fast_json_value_free (json, v);
  fast_json_value_free (json, n);

  v =
    fast_json_parse_string2 (json,
			     "[ { \"a\":1 }, -1.0, \"s\", true, null ]");
  n = fast_json_value_copy (json, v);
  if (fast_json_value_equal (v, n) == 0) {
    fprintf (stderr, "equal failed\n");
    exit (1);
  }
  fast_json_value_free (json, v);
  v =
    fast_json_parse_string2 (json,
			     "[ { \"a\":1 }, -1.0, \"s\", true, false ]");
  if (fast_json_value_equal (v, n) != 0) {
    fprintf (stderr, "equal failed\n");
    exit (1);
  }
  fast_json_value_free (json, v);
  fast_json_value_free (json, n);

  fast_json_options (json, FAST_JSON_SORT_OBJECTS);
  v = fast_json_parse_string2 (json, "{ \"c\":3, \"b\":2, \"a\":1 }");
  cp = fast_json_print_string (json, v, 0);
  if (strcmp (cp, "{\"a\":1,\"b\":2,\"c\":3}") != 0) {
    fprintf (stderr, "Unexpected value\n");
    exit (1);
  }
  fast_json_release_print_value (json, cp);
  fast_json_value_free (json, v);

  fast_json_options (json, FAST_JSON_ALLOW_OCT_HEX);
  v = fast_json_parse_string2 (json, "[ 0x3, 0Xd, 0xf.fp7, 0123 ]");
  cp = fast_json_print_string (json, v, 0);
  if (strcmp (cp, "[3,13,2040.0,83]") != 0) {
    fprintf (stderr, "Unexpected value\n");
    exit (1);
  }
  fast_json_release_print_value (json, cp);
  fast_json_value_free (json, v);

  fast_json_options (json, FAST_JSON_INF_NAN);
  e = fast_json_calc_crc_string (json, "{\"name\": \"abc\"}", &i);
  if (e != FAST_JSON_OK || i != 0x22721824) {
    fprintf (stderr, "Wrong sumcheck\n");
    exit (1);
  }
  e =
    fast_json_calc_crc_string (json,
			       "[null, nan, nan(123), false, inf, infinity ]",
			       &i);
  if (e != FAST_JSON_OK || i != 0x03623a1f) {
    fprintf (stderr, "Wrong sumcheck\n");
    exit (1);
  }
  e =
    fast_json_calc_crc_string (json,
			       "[+1, 0, 123, -nan, -nan(123), -inf, -infinity ]",
			       &i);
  if (e != FAST_JSON_OK || i != 0xd443c028) {
    fprintf (stderr, "Wrong sumcheck\n");
    exit (1);
  }
  e =
    fast_json_calc_crc_string (json, "[12.34, 12.34e+3, true, [], {} ]", &i);
  if (e != FAST_JSON_OK || i != 0x423becf1) {
    fprintf (stderr, "Wrong sumcheck\n");
    exit (1);
  }

  fast_json_options (json, FAST_JSON_ALLOW_OCT_HEX);
  e = fast_json_calc_crc_string (json, "[ 0x3, 0Xd, 0xf.fp7, 0123 ]", &i);
  if (e != FAST_JSON_OK || i != 0x2baf1ec8) {
    fprintf (stderr, "Wrong sumcheck\n");
    exit (1);
  }

  fast_json_options (json, FAST_JSON_NO_EOF_CHECK);
  v = fast_json_parse_string (json, "[true][false]");
  cp = fast_json_print_string (json, v, 0);
  if (strcmp (cp, "[true]") != 0) {
    fprintf (stderr, "Wrong data\n");
    exit (1);
  }
  fast_json_release_print_value (json, cp);
  fast_json_value_free (json, v);

  fast_json_options (json, 0);
  if (stat (TEST1_FILE, &st) < 0) {
    fprintf (stderr, "Cannot open: '%s'\n", TEST1_FILE);
    exit (1);
  }
  cp = (char *) malloc (st.st_size + 1);
  fd = open (TEST1_FILE, O_RDONLY);
  read (fd, cp, st.st_size);
  close (fd);
  cp[st.st_size] = '\0';
  v = fast_json_parse_string (json, cp);
  n = fast_json_parse_string_len (json, cp, strlen (cp));
  if (fast_json_value_equal (v, n) == 0) {
    fprintf (stderr, "equal failed\n");
    exit (1);
  }
  fast_json_value_free (json, n);
  fast_json_value_free (json, v);
  free (cp);

  fast_json_options (json, FAST_JSON_NO_EOF_CHECK);
  v = fast_json_parse_string2 (json, "[true][false]");
  cp = fast_json_print_string (json, v, 0);
  if (strcmp (cp, "[true]") != 0) {
    fprintf (stderr, "Wrong data\n");
    exit (1);
  }
  fast_json_release_print_value (json, cp);
  fast_json_value_free (json, v);

  fast_json_options (json, 0);
  if (stat (TEST1_FILE, &st) < 0) {
    fprintf (stderr, "Cannot open: '%s'\n", TEST1_FILE);
    exit (1);
  }
  cp = (char *) malloc (st.st_size + 1);
  fd = open (TEST1_FILE, O_RDONLY);
  read (fd, cp, st.st_size);
  close (fd);
  cp[st.st_size] = '\0';
  v = fast_json_parse_string2 (json, cp);
  n = fast_json_parse_string_len (json, cp, strlen (cp));
  if (fast_json_value_equal (v, n) == 0) {
    fprintf (stderr, "equal failed\n");
    exit (1);
  }
  fast_json_value_free (json, n);

  fp = fopen (TEST1_FILE, "r");
  n = fast_json_parse_file (json, fp);
  if (fast_json_value_equal (v, n) == 0) {
    fprintf (stderr, "equal failed\n");
    exit (1);
  }
  fast_json_value_free (json, n);
  fclose (fp);

  n = fast_json_parse_file_name (json, TEST1_FILE);
  if (fast_json_value_equal (v, n) == 0) {
    fprintf (stderr, "equal failed\n");
    exit (1);
  }
  fast_json_value_free (json, n);

  fd = open (TEST1_FILE, O_RDONLY);
  n = fast_json_parse_fd (json, fd);
  if (fast_json_value_equal (v, n) == 0) {
    fprintf (stderr, "equal failed\n");
    exit (1);
  }
  fast_json_value_free (json, n);
  close (fd);

  fast_json_calc_crc_string (json, cp, &i);

  fast_json_calc_crc_string_len (json, cp, strlen (cp), &j);
  if (i != j) {
    fprintf (stderr, "crc failed\n");
    exit (1);
  }
  free (cp);

  fp = fopen (TEST1_FILE, "r");
  fast_json_calc_crc_file (json, fp, &j);
  if (i != j) {
    fprintf (stderr, "crc failed\n");
    exit (1);
  }
  fclose (fp);

  fast_json_calc_crc_file_name (json, TEST1_FILE, &j);
  if (i != j) {
    fprintf (stderr, "crc failed\n");
    exit (1);
  }

  fd = open (TEST1_FILE, O_RDONLY);
  fast_json_calc_crc_fd (json, fd, &j);
  if (i != j) {
    fprintf (stderr, "crc failed\n");
    exit (1);
  }
  close (fd);

  fp = fopen ("/tmp/file1.json", "w");
  fast_json_print_file (json, v, fp, 1);
  fclose (fp);

  fast_json_print_file_name (json, v, "/tmp/file2.json", 1);

  fd = open ("/tmp/file3.json", O_CREAT | O_WRONLY | O_TRUNC, 0666);
  fast_json_print_fd (json, v, fd, 1);
  close (fd);

  fast_json_value_free (json, v);

  fast_json_options (json, FAST_JSON_NO_EOF_CHECK);
  fp = fopen (TEST2_FILE, "r");
  n = fast_json_parse_file (json, fp);
  cp = fast_json_print_string (json, n, 0);
  if (strcmp (cp, "42") != 0) {
    fprintf (stderr, "read noeof failed. 4\n");
    exit (1);
  }
  fast_json_release_print_value (json, cp);
  fast_json_value_free (json, n);
  n = fast_json_parse_file (json, fp);
  cp = fast_json_print_string (json, n, 0);
  if (strcmp (cp, "true") != 0) {
    fprintf (stderr, "read noeof failed. true\n");
    exit (1);
  }
  fast_json_release_print_value (json, cp);
  fast_json_value_free (json, n);
  n = fast_json_parse_file (json, fp);
  if (n != NULL) {
    fprintf (stderr, "read noeof failed. extra data\n");
    exit (1);
  }
  if (fast_json_parser_error (json) != FAST_JSON_NO_DATA_ERROR) {
    fprintf (stderr, "read noeof failed: %s\n",
	     fast_json_error_str (fast_json_parser_error (json)));
    exit (1);
  }
  fclose (fp);

  n = fast_json_parse_string (json, multi_str);
  cp = fast_json_print_string (json, n, 0);
  if (strcmp (cp, "42") != 0) {
    fprintf (stderr, "read noeof failed. 4\n");
    exit (1);
  }
  fast_json_release_print_value (json, cp);
  fast_json_value_free (json, n);
  i = fast_json_parser_position (json);
  n = fast_json_parse_string (json, &multi_str[i]);
  cp = fast_json_print_string (json, n, 0);
  if (strcmp (cp, "true") != 0) {
    fprintf (stderr, "read noeof failed. true\n");
    exit (1);
  }
  fast_json_release_print_value (json, cp);
  fast_json_value_free (json, n);
  i += fast_json_parser_position (json);
  n = fast_json_parse_string (json, &multi_str[i]);
  if (n != NULL) {
    fprintf (stderr, "read noeof failed. extra data\n");
    exit (1);
  }
  if (fast_json_parser_error (json) != FAST_JSON_NO_DATA_ERROR) {
    fprintf (stderr, "read noeof failed: %s\n",
	     fast_json_error_str (fast_json_parser_error (json)));
    exit (1);
  }

  n = fast_json_parse_string2 (json, multi_str);
  cp = fast_json_print_string (json, n, 0);
  if (strcmp (cp, "42") != 0) {
    fprintf (stderr, "read noeof failed. 4\n");
    exit (1);
  }
  fast_json_release_print_value (json, cp);
  fast_json_value_free (json, n);
  i = fast_json_parser_position (json);
  n = fast_json_parse_string2 (json, &multi_str[i]);
  cp = fast_json_print_string (json, n, 0);
  if (strcmp (cp, "true") != 0) {
    fprintf (stderr, "read noeof failed. true\n");
    exit (1);
  }
  fast_json_release_print_value (json, cp);
  fast_json_value_free (json, n);
  i += fast_json_parser_position (json);
  n = fast_json_parse_string2 (json, &multi_str[i]);
  if (n != NULL) {
    fprintf (stderr, "read noeof failed. extra data\n");
    exit (1);
  }
  if (fast_json_parser_error (json) != FAST_JSON_NO_DATA_ERROR) {
    fprintf (stderr, "read noeof failed: %s\n",
	     fast_json_error_str (fast_json_parser_error (json)));
    exit (1);
  }

  fd = open (TEST2_FILE, O_RDONLY);
  n = fast_json_parse_fd (json, fd);
  cp = fast_json_print_string (json, n, 0);
  if (strcmp (cp, "42") != 0) {
    fprintf (stderr, "read noeof failed. 4\n");
    exit (1);
  }
  fast_json_release_print_value (json, cp);
  fast_json_value_free (json, n);
  i = fast_json_parser_position (json);
  lseek (fd, i, SEEK_SET);
  n = fast_json_parse_fd (json, fd);
  cp = fast_json_print_string (json, n, 0);
  if (strcmp (cp, "true") != 0) {
    fprintf (stderr, "read noeof failed. true\n");
    exit (1);
  }
  fast_json_release_print_value (json, cp);
  fast_json_value_free (json, n);
  i += fast_json_parser_position (json);
  lseek (fd, i, SEEK_SET);
  n = fast_json_parse_fd (json, fd);
  if (n != NULL) {
    fprintf (stderr, "read noeof failed. extra data\n");
    exit (1);
  }
  if (fast_json_parser_error (json) != FAST_JSON_NO_DATA_ERROR) {
    fprintf (stderr, "read noeof failed: %s\n",
	     fast_json_error_str (fast_json_parser_error (json)));
    exit (1);
  }
  close (fd);

  fast_json_options (json, 0);

  str[0] = '[';
  str[1] = '"';
  str[2] = 0xDFu;		/* 2 byte utf8 */
  str[3] = 0xB0u;
  str[4] = 0xEFu;		/* 3 byte utf8 */
  str[5] = 0xBBu;
  str[6] = 0xBFu;
  str[7] = 0xF3u;		/* 4 byte utf8 */
  str[8] = 0x90u;
  str[9] = 0xA0u;
  str[10] = 0xb0u;
  str[11] = '"';
  str[12] = ']';
  str[13] = '\0';

  n = fast_json_parse_string (json, str);
  cp = fast_json_print_string (json, n, 0);
  if (strcmp (cp, "[\"\\u07F0\\uFEFF\\uDB02\\uDC30\"]") != 0) {
    fprintf (stderr, "utf8 error '%s'\n", cp);
    exit (1);
  }
  fast_json_release_print_value (json, cp);
  fast_json_value_free (json, n);

  str[0] = '[';
  str[1] = '"';
  str[2] = 0xE0u;
  str[3] = 0x80u;
  str[4] = 0x80u;
  str[5] = '"';
  str[6] = ']';
  str[7] = '\0';
  fast_json_parse_string (json, str);
  if (fast_json_parser_error (json) != FAST_JSON_UNICODE_ERROR) {
    fprintf (stderr, "utf8 error expected: %s\n",
	     fast_json_error_str (fast_json_parser_error (json)));
    exit (1);
  }

  j = 10000;
  cp = (char *) malloc (j * 10);
  np = cp;
  for (i = 0; i < j; i++) {
    *np++ = '[';
  }
  *np++ = '1';
  for (i = 0; i < j; i++) {
    *np++ = ']';
  }
  *np++ = '\0';
  n = fast_json_parse_string (json, cp);
  if (n == NULL) {
    fprintf (stderr, "Large array nesting not worked. %s\n",
	     fast_json_error_str (fast_json_parser_error (json)));
    exit (1);
  }
  fast_json_value_free (json, n);

  np = cp;
  for (i = 0; i < j; i++) {
    *np++ = '{';
    *np++ = '"';
    *np++ = 'a';
    *np++ = '"';
    *np++ = ':';
  }
  *np++ = '[';
  *np++ = '1';
  *np++ = ']';
  for (i = 0; i < j; i++) {
    *np++ = '}';
  }
  *np++ = '\0';
  n = fast_json_parse_string (json, cp);
  if (n == NULL) {
    fprintf (stderr, "Large object nesting not worked. %s\n",
	     fast_json_error_str (fast_json_parser_error (json)));
    exit (1);
  }
  fast_json_value_free (json, n);
  free (cp);

#ifndef WIN
  setlocale (LC_ALL, "nl_NL.UTF-8");
  localeconv(); /* will normally be called in parser */
#if USE_FAST_CONVERT
  fast_dtoa (12.34, PREC_DBL_NR, str);
#else
  snprintf (str, sizeof(str), "%g", 12.34);
#endif
  if (strchr (str, ',') == NULL) {
    fprintf (stderr, "Locale does not work '%s'\n", str);
    exit (1);
  }
  {
    double dval;
#if USE_FAST_CONVERT
    dval = fast_strtod (str, NULL);
#else
    dval = strtod (str, NULL);
#endif
    if (dval != 12.34) {
      fprintf (stderr, "Locale does not work '%g'\n", dval);
      exit (1);
    }
  }
  n = fast_json_parse_string (json, "12.34");
  if (n == NULL) {
    fprintf (stderr, "Locale1 does not work\n");
    exit (1);
  }
  fast_json_value_free (json, n);
  n = fast_json_parse_string2 (json, "12.34");
  if (n == NULL) {
    fprintf (stderr, "Locale2 does not work");
    exit (1);
  }
  fast_json_value_free (json, n);
#endif

  fast_json_free (json);

  if (malloc_size != 0) {
    fprintf (stderr, "Memory usage: %" PRId64 ", Max usage: %" PRId64
	     ", Malloc %" PRId64 ", Realloc %" PRId64
	     ", Free %" PRId64 "\n",
	     malloc_size, malloc_max_size,
	     malloc_n_malloc, malloc_n_realloc, malloc_n_free);
  }
  return 0;
}
