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
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#ifndef WIN
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#endif
#include "fast_json.h"

#define RAND_IA         __UINT64_C(0x5851F42D4C957F2D)
#define RAND_IC         __UINT64_C(0x14057B7EF767814F)

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

static unsigned int count = 1000;
static unsigned int reuse = 0;
static unsigned int options = 0;
static unsigned int print_nice = 0;

#ifndef WIN
static int sockets[2];

static void *
sender (void *data)
{
  unsigned int i;
  FAST_JSON_TYPE json;
  FAST_JSON_DATA_TYPE v = (FAST_JSON_DATA_TYPE) data;

  json = fast_json_create (NULL, NULL, NULL);
  if (json == NULL) {
    fprintf (stderr, "Cannot create json\n");
    exit (1);
  }
  fast_json_max_reuse (json, reuse);
  fast_json_options (json, options);
  for (i = 0; i < count; i++) {
    fast_json_print_fd (json, v, sockets[0], print_nice);
  }
  fast_json_free (json);
  return NULL;
}

static void *
receiver (void *data)
{
  unsigned int i;
  FAST_JSON_TYPE json;
  FAST_JSON_DATA_TYPE v = (FAST_JSON_DATA_TYPE) data;
  FAST_JSON_DATA_TYPE n;

  json = fast_json_create (NULL, NULL, NULL);
  if (json == NULL) {
    fprintf (stderr, "Cannot create json\n");
    exit (1);
  }
  fast_json_max_reuse (json, reuse);
  fast_json_options (json, options | FAST_JSON_NO_EOF_CHECK);
  for (i = 0; i < count; i++) {
    if (i == 0) {
      n = fast_json_parse_fd (json, sockets[1]);
    }
    else {
      n = fast_json_parse_next (json);
    }
    if (n == NULL) {
      fprintf (stderr, "read failed (%u): '%s' '%s' %lu %lu %lu\n",
	       i,
	       fast_json_error_str (fast_json_parser_error (json)),
	       fast_json_parser_error_str (json),
	       (unsigned long) fast_json_parser_line (json),
	       (unsigned long) fast_json_parser_column (json),
	       (unsigned long) fast_json_parser_position (json));
      exit (1);
    }
    if (fast_json_value_equal (v, n) == 0) {
      fprintf (stderr, "equal failed\n");
      exit (1);
    }
    fast_json_value_free (json, n);
  }
  fast_json_free (json);
  return NULL;
}
#endif

static double
get_time (void)
{
#if WIN
  struct timeval tv;

  gettimeofday (&tv, NULL);
  return ((double) tv.tv_sec) * 1e9 + ((double) tv.tv_usec) * 1e3;
#else
  struct timespec curtime;

  clock_gettime (CLOCK_REALTIME, &curtime);
  return ((double) curtime.tv_sec) * 1e9 + ((double) curtime.tv_nsec);
#endif
}

int
main (int argc, char **argv)
{
  unsigned int i;
  FAST_JSON_TYPE json;
  FAST_JSON_DATA_TYPE o;
  char *s;
  double start;
  double end;
  unsigned int check_alloc = 0;
  unsigned int fast_string = 0;
  unsigned int parse_time = 0;
  unsigned int print_time = 0;
#ifndef WIN
  unsigned int stream_time = 0;
#endif
  unsigned int print = 0;
  unsigned int print_help = 0;
  char *name = NULL;

  for (i = 1; i < argc; i++) {
    if (strncmp (argv[i], "--count=", strlen ("--count=")) == 0) {
      count = strtoul (&argv[i][strlen ("--count=")], NULL, 0);
    }
    else if (strncmp (argv[i], "--reuse=", strlen ("--reuse=")) == 0) {
      reuse = strtoul (&argv[i][strlen ("--reuse=")], NULL, 0);
    }
    else if (strcmp (argv[i], "--print_time") == 0) {
      print_time = 1;
    }
    else if (strcmp (argv[i], "--parse_time") == 0) {
      parse_time = 1;
    }
#ifndef WIN
    else if (strcmp (argv[i], "--stream_time") == 0) {
      stream_time = 1;
    }
#endif
    else if (strcmp (argv[i], "--hex") == 0) {
      options |= FAST_JSON_ALLOW_OCT_HEX;
    }
    else if (strcmp (argv[i], "--infnan") == 0) {
      options |= FAST_JSON_INF_NAN;
    }
    else if (strcmp (argv[i], "--big") == 0) {
      options |= FAST_JSON_BIG_ALLOC;
    }
    else if (strcmp (argv[i], "--no_duplicate") == 0) {
      options |= FAST_JSON_NO_DUPLICATE_CHECK;
    }
    else if (strcmp (argv[i], "--check_alloc") == 0) {
      check_alloc = 1;
    }
    else if (strcmp (argv[i], "--fast_string") == 0) {
      fast_string = 1;
    }
    else if (strcmp (argv[i], "--print") == 0) {
      print = 1;
    }
    else if (strcmp (argv[i], "--nice") == 0) {
      print_nice = 1;
    }
    else if (strcmp (argv[i], "--unicode_escape") == 0) {
      options |= FAST_JSON_PRINT_UNICODE_ESCAPE;
    }
    else if (name == NULL && argv[i][0] != '-') {
      name = argv[i];
    }
    else {
      print_help = 1;
      break;
    }
  }
  if (print_help) {
    printf ("Usage: %s [options] [filename]\n", argv[0]);
    printf ("Options:\n");
    printf ("--count=n         Run count times (default %u)\n", count);
    printf ("--reuse=n         Use object reuse\n");
    printf ("--print_time:     Run print time test\n");
    printf ("--parse_time:     Run parse time test\n");
#ifndef WIN
    printf ("--stream_time:    Run stream time test\n");
#endif
    printf ("--hex:            Allow oct and hex numbers\n");
    printf ("--infnan:         Allow inf and nan\n");
    printf ("--big:            Use big allocs\n");
    printf ("--no_duplicate:   Do not check duplicate object names\n");
    printf ("--check_alloc:    Check allocs\n");
    printf ("--fast_string:    Use fast string parser\n");
    printf ("--print:          Print result\n");
    printf ("--nice:           Print result with spaces and newlines\n");
    printf ("--unicode_escape: Print unicode escape instead of utf8\n");
    exit (0);
  }
  if (print_time == 0 && parse_time == 0
#ifndef WIN
      && stream_time == 0
#endif
    ) {
    print_time = 1;
    parse_time = 1;
#ifndef WIN
    stream_time = 1;
#endif
  }
  if (count == 0) {
    count = 1000;
  }

  if (check_alloc) {
    json = fast_json_create (my_malloc, my_realloc, my_free);
  }
  else {
    json = fast_json_create (NULL, NULL, NULL);
  }
  fast_json_max_reuse (json, reuse);
  fast_json_options (json, options);

  if (name) {
    o = fast_json_parse_file_name (json, name);
    if (o == NULL) {
      printf ("Error: %s '%s' at %lu:%lu:%lu\n",
	      fast_json_error_str (fast_json_parser_error (json)),
	      fast_json_parser_error_str (json),
	      (unsigned long) fast_json_parser_line (json),
	      (unsigned long) fast_json_parser_column (json),
	      (unsigned long) fast_json_parser_position (json));
      exit (1);
    }
  }
  else {
    uint64_t r = 1234567890;
    FAST_JSON_DATA_TYPE a;
    FAST_JSON_DATA_TYPE n;
    char str[10];

    o = fast_json_create_object (json);

    a = fast_json_create_array (json);
    for (i = 0; i < 1000; i++) {
      n = fast_json_create_null (json);
      fast_json_add_array (json, a, n);
    }
    fast_json_add_object (json, o, "null", a);

    a = fast_json_create_array (json);
    for (i = 0; i < 1000; i++) {
      n = fast_json_create_boolean_value (json, (i & 1) ? 0 : 1);
      fast_json_add_array (json, a, n);
    }
    fast_json_add_object (json, o, "bool", a);

    a = fast_json_create_array (json);
    for (i = 0; i < 1000; i++) {
      r = r * RAND_IA + RAND_IC;
      n = fast_json_create_integer_value (json, (int64_t) r);
      fast_json_add_array (json, a, n);
    }
    fast_json_add_object (json, o, "int", a);

    a = fast_json_create_array (json);
    for (i = 0; i < 1000; i++) {
      union
      {
	uint64_t ul;
	double d;
      } td;
      r = r * RAND_IA + RAND_IC;
      td.ul = r;
      n = fast_json_create_double_value (json, td.d);
      fast_json_add_array (json, a, n);
    }
    fast_json_add_object (json, o, "double", a);

    a = fast_json_create_array (json);
    for (i = 0; i < 1000; i++) {
      sprintf (str, "%u", i);
      n = fast_json_create_string (json, str);
      fast_json_add_array (json, a, n);
    }
    fast_json_add_object (json, o, "string", a);
  }

  if (print) {
    s = fast_json_print_string (json, o, print_nice);
    fast_json_value_free (json, o);
    o = fast_json_parse_string (json, s);
    fast_json_release_print_value (json, s);
    s = fast_json_print_string (json, o, print_nice);
    fast_json_value_free (json, o);
    printf ("%s", s);
    fast_json_release_print_value (json, s);
    fast_json_free (json);
    return 0;
  }

  if (print_time) {
    start = get_time ();
    for (i = 0; i < count; i++) {
      s = fast_json_print_string (json, o, print_nice);
      fast_json_release_print_value (json, s);
    }
    end = get_time ();
    printf ("print     %12.9f s\n", (end - start) / count / 1e9);
  }
  if (parse_time) {
    s = fast_json_print_string (json, o, print_nice);
    start = get_time ();
    for (i = 0; i < count; i++) {
      if (fast_string) {
	o = fast_json_parse_string2 (json, s);
      }
      else {
	o = fast_json_parse_string (json, s);
      }
      if (o == NULL) {
	printf ("Error: %s '%s' at %lu:%lu:%lu\n",
		fast_json_error_str (fast_json_parser_error (json)),
		fast_json_parser_error_str (json),
		(unsigned long) fast_json_parser_line (json),
		(unsigned long) fast_json_parser_column (json),
		(unsigned long) fast_json_parser_position (json));
	break;
      }
      fast_json_value_free (json, o);
    }
#ifndef WIN
    if (stream_time) {
      o = fast_json_parse_string2 (json, s);
    }
    else
#endif
    {
      o = NULL;
    }
    fast_json_release_print_value (json, s);
    end = get_time ();
    printf ("parse     %12.9f s\n", (end - start) / count / 1e9);
  }

#ifndef WIN
  if (stream_time) {
    pthread_t rt;
    pthread_t st;

    socketpair (AF_UNIX, SOCK_STREAM, 0, &sockets[0]);
    start = get_time ();
    pthread_create (&st, NULL, sender, o);
    pthread_create (&rt, NULL, receiver, o);
    pthread_join (st, NULL);
    pthread_join (rt, NULL);
    end = get_time ();
    fprintf (stderr, "stream:   %12.9f s\n", (end - start) / count / 1e9);
    close (sockets[0]);
    close (sockets[1]);
  }
#endif

  fast_json_value_free (json, o);
  fast_json_free (json);

  if (check_alloc) {
    fprintf (stderr, "Memory usage: %" PRId64 ", Max usage: %" PRId64
	     ", Malloc %" PRId64 ", Realloc %" PRId64
	     ", Free %" PRId64 "\n",
	     malloc_size, malloc_max_size,
	     malloc_n_malloc, malloc_n_realloc, malloc_n_free);
  }

  return 0;
}
