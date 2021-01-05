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
#else
#include <windows.h>
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

static int sockets[2];

/* Windows does not support read/write on socket so use recv/send */

static void *
sender (void *data)
{
  unsigned int i;
  unsigned int add_newline;
  FAST_JSON_TYPE json;
  FAST_JSON_DATA_TYPE v = (FAST_JSON_DATA_TYPE) data;

  json = fast_json_create (NULL, NULL, NULL);
  if (json == NULL) {
    fprintf (stderr, "Cannot create json\n");
    exit (1);
  }
  fast_json_max_reuse (json, reuse);
  fast_json_options (json, options);
  add_newline = (fast_json_get_type (v) != FAST_JSON_OBJECT &&
		 fast_json_get_type (v) != FAST_JSON_ARRAY &&
		 fast_json_get_type (v) != FAST_JSON_STRING);
  for (i = 0; i < count; i++) {
#ifndef WIN
    fast_json_print_fd (json, v, sockets[0], print_nice);
#else
    char *cp = fast_json_print_string (json, v, print_nice);

    send(sockets[0], cp, strlen (cp), 0);
    fast_json_release_print_value (json, cp);
#endif
    if (add_newline) {
#ifndef WIN
      write (sockets[0], "\n", 1);
#else
      send (sockets[0], "\n", 1, 0);
#endif
    }
  }
  fast_json_free (json);
  return NULL;
}

#ifdef WIN
static int u_pos = 0;
static int u_len = 0;
static char u_buffer[BUFSIZ];

static int
user_getc (void *user_data)
{
  if (u_pos >= u_len) {
    int len;
    int fd = *(int *) user_data;

    do {
      len = recv (fd, u_buffer, sizeof (u_buffer), 0);
    } while (len < 0 && GetLastError() == WSAEWOULDBLOCK);

    if (len <= 0) {
      return -1;
    }
    u_len = len;
    u_pos = 0;
  }
  return u_buffer[u_pos++] & 0xFFu;
}
#endif

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
#ifndef WIN
      n = fast_json_parse_fd (json, sockets[1]);
#else
      n = fast_json_parse_user (json, user_getc, &sockets[1]);
#endif
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

#ifdef WIN
int
mysocketpair (int sp[2])
{
  struct usa {
    union
    {
      struct sockaddr sa;
      struct sockaddr_in sin;
    }
    u;
  } sa;
  int sock, ret = -1;
  int len = sizeof (sa.u.sa);

  (void) memset (&sa, 0, sizeof (sa));
  sa.u.sin.sin_family = AF_INET;
  sa.u.sin.sin_port = htons (0);
  sa.u.sin.sin_addr.s_addr = htonl (INADDR_LOOPBACK);

  if ((sock = socket (AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1) {
    fprintf (stderr, "mysocketpair: socket(): %d\n", (int) GetLastError());
  }
  else if (bind (sock, &sa.u.sa, len) != 0) {
    fprintf (stderr, "mysocketpair: bind(): %d\n", (int) GetLastError());
    (void) closesocket (sock);
  }
  else if (listen (sock, 1) != 0) {
    fprintf (stderr, "mysocketpair: listen(): %d\n", (int) GetLastError());
    (void) closesocket (sock);
  }
  else if (getsockname (sock, &sa.u.sa, &len) != 0) {
    fprintf (stderr, "mysocketpair: getsockname(): %d\n", (int) GetLastError());
    (void) closesocket (sock);
  }
  else if ((sp[0] = socket (AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1) {
    fprintf (stderr, "mysocketpair: socket(): %d\n", (int) GetLastError());
    (void) closesocket (sock);
  }
  else if (connect (sp[0], &sa.u.sa, len) != 0) {
    fprintf (stderr, "mysocketpair: connect(): %d\n", (int) GetLastError());
    (void) closesocket (sock);
    (void) closesocket (sp[0]);
  }
  else if ((sp[1] = accept (sock, &sa.u.sa, &len)) == -1) {
    fprintf (stderr, "mysocketpair: accept(): %d\n", (int) GetLastError());
    (void) closesocket (sock);
    (void) closesocket (sp[0]);
  }
  else {
    /* Success */
    ret = 0;
    (void) closesocket (sock);
  }

  return (ret);}
#endif

static uint64_t n_object = 0;
static uint64_t n_array = 0;
static uint64_t n_integer = 0;
static uint64_t n_double = 0;
static uint64_t n_string = 0;
static uint64_t n_boolean = 0;
static uint64_t n_null = 0;

static void
count_items (FAST_JSON_DATA_TYPE v)
{
  switch (fast_json_get_type (v)) {
  case FAST_JSON_OBJECT:
    {
      size_t i;
      size_t n = fast_json_get_object_size (v);

      n_object++;
      for (i = 0; i < n; i++) {
	count_items (fast_json_get_object_data (v, i));
      }
    }
    break;
  case FAST_JSON_ARRAY:
    {
      size_t i;
      size_t n = fast_json_get_array_size (v);

      n_array++;
      for (i = 0; i < n; i++) {
	count_items (fast_json_get_array_data (v, i));
      }
    }
    break;
  case FAST_JSON_INTEGER:
    n_integer++;
    break;
  case FAST_JSON_DOUBLE:
    n_double++;
    break;
  case FAST_JSON_STRING:
    n_string++;
    break;
  case FAST_JSON_BOOLEAN:
    n_boolean++;
    break;
  case FAST_JSON_NULL:
    n_null++;
    break;
  }
}

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
  double total;
  double len;
  double start;
  double end;
  unsigned int check_alloc = 0;
  unsigned int fast_string = 0;
  unsigned int parse_time = 0;
  unsigned int print_time = 0;
  unsigned int stream_time = 0;
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
    else if (strcmp (argv[i], "--stream_time") == 0) {
      stream_time = 1;
    }
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
    else if (strcmp (argv[i], "--no_comment") == 0) {
      options |= FAST_JSON_NO_COMMENT;
    }
    else if (strcmp (argv[i], "--allow_json5") == 0) {
      options |= FAST_JSON_ALLOW_JSON5;
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
    printf ("--stream_time:    Run stream time test\n");
    printf ("--hex:            Allow oct and hex numbers\n");
    printf ("--infnan:         Allow inf and nan\n");
    printf ("--big:            Use big allocs\n");
    printf ("--no_duplicate:   Do not check duplicate object names\n");
    printf ("--no_comment:     Do not allow comments\n");
    printf ("--allow_json5:    Allow json5\n");
    printf ("--check_alloc:    Check allocs\n");
    printf ("--fast_string:    Use fast string parser\n");
    printf ("--print:          Print result\n");
    printf ("--nice:           Print result with spaces and newlines\n");
    printf ("--unicode_escape: Print unicode escape instead of utf8\n");
    exit (0);
  }
  if (print_time == 0 && parse_time == 0 && stream_time == 0
    ) {
    print_time = 1;
    parse_time = 1;
    stream_time = 1;
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

  count_items (o);
  printf
    ("obj: %lu, arr %lu, int %lu, dbl %lu, str %lu, bool %lu, null %lu\n",
     (unsigned long) n_object, (unsigned long) n_array,
     (unsigned long) n_integer, (unsigned long) n_double,
     (unsigned long) n_string, (unsigned long) n_boolean,
     (unsigned long) n_null);
  total =
    n_object + n_array + n_integer + n_double + n_string + n_boolean + n_null;

  s = fast_json_print_string (json, o, print_nice);
  len = strlen (s);
  fast_json_release_print_value (json, s);

  if (print_time) {
    start = get_time ();
    for (i = 0; i < count; i++) {
      s = fast_json_print_string (json, o, print_nice);
      fast_json_release_print_value (json, s);
    }
    end = get_time ();
    printf ("print     %12.9f s, %10.0f chars/s, %10.0f items/s\n",
	    (end - start) / count / 1e9, len * count * 1e9 / (end - start),
	    total * count * 1e9 / (end - start));
  }
  if (parse_time) {
    s = fast_json_print_string (json, o, print_nice);
    fast_json_value_free (json, o);
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
    if (stream_time) {
      o = fast_json_parse_string2 (json, s);
    }
    else {
      o = NULL;
    }
    fast_json_release_print_value (json, s);
    end = get_time ();
    printf ("parse     %12.9f s, %10.0f chars/s, %10.0f items/s\n",
	    (end - start) / count / 1e9, len * count * 1e9 / (end - start),
	    total * count * 1e9 / (end - start));
  }

  if (stream_time) {
#ifndef WIN
    pthread_t rt;
    pthread_t st;

    socketpair (AF_UNIX, SOCK_STREAM, 0, &sockets[0]);
    start = get_time ();
    pthread_create (&st, NULL, sender, o);
    pthread_create (&rt, NULL, receiver, o);
    pthread_join (st, NULL);
    pthread_join (rt, NULL);
    end = get_time ();
    printf ("stream:   %12.9f s, %10.0f chars/s, %10.0f items/s\n",
	    (end - start) / count / 1e9, len * count * 1e9 / (end - start),
	    total * count * 1e9 / (end - start));
    close (sockets[0]);
    close (sockets[1]);
#else
    HANDLE rt;
    HANDLE st;
    int err;
    WSADATA wsaData;

    /* Initialize Winsock */
    err = WSAStartup(MAKEWORD(2,2), &wsaData);
    if (err != 0) {
      fprintf (stderr, "WSAStartup(): %d\n", (int) GetLastError());
      exit (1);
    }

    mysocketpair (&sockets[0]);
    start = get_time ();
    st = CreateThread(NULL, 8192, (LPTHREAD_START_ROUTINE) sender, o, 0, NULL);
    rt = CreateThread(NULL, 8192, (LPTHREAD_START_ROUTINE) receiver, o, 0, NULL);
    WaitForSingleObject(st, INFINITE);
    WaitForSingleObject(rt, INFINITE);
    CloseHandle(st);
    CloseHandle(rt);
    end = get_time ();
    printf ("stream:   %12.9f s, %10.0f chars/s, %10.0f items/s\n",
	    (end - start) / count / 1e9, len * count * 1e9 / (end - start),
	    total * count * 1e9 / (end - start));
    closesocket (sockets[0]);
    closesocket (sockets[1]);
    WSACleanup();
#endif
  }

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
