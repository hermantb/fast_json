/* Copyright 2019 Herman ten Brugge
 *
 * Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
 * http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
 * <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
 * option. This file may not be copied, modified, or distributed
 * except according to those terms.
 */

/*
Json syntax description. See RFC 4627. Only UTF-8 is implemented.

	json
	    element

	value
	    object
	    array
	    string
	    number
	    "true"
	    "false"
	    "null"

	object
	    '{' ws '}'
	    '{' members '}'

	members
	    member
	    member ',' members

	member
	    ws string ws ':' element

	array
	    '[' ws ']'
	    '[' elements ']'

	elements
	    element
	    element ',' elements

	element
	    ws value ws

	string
	    '"' characters '"'

	characters
	    ""
	    character characters

	character
	    '0020' . '10ffff' - '"' - '\'
	    '\' escape

	escape
	    '"'
	    '\'
	    '/'
	    'b'
	    'f'
	    'n'
	    'r'
	    't'
	    'u' hex hex hex hex

	hex
	    digit
	    'A' . 'F'
	    'a' . 'f'

	number
	    integer fraction exponent

	integer
	    digit
	    onenine digits
	    '-' digit
	    '-' onenine digits

	digits
	    digit
	    digit digits

	digit
	    '0'
	    onenine

	onenine
	    '1' . '9'

	fraction
	    ""
	    '.' digits

	exponent
	    ""
	    'E' sign digits
	    'e' sign digits

	sign
	    ""
	    '+'
	    '-'

	ws
	    ""
	    '0020' ws
	    '000D' ws
	    '000A' ws
	    '0009' ws
*/

#ifndef FAST_JSON_H
#define FAST_JSON_H

#include <inttypes.h>

#if defined (__cplusplus)
extern "C"
{
#endif

/** Define fast json int_type */
  typedef uint64_t fast_json_int_64;

/** define fast json print integer format */
#define	FAST_JSON_FMT_INT		PRId64

/** End of file character */
#define	FAST_JSON_EOF			(-1)

/** Do not check for loops */
#define	FAST_JSON_NO_CHECK_LOOP		(0x001)

/** During parsing convert all number to doubles */
#define	FAST_JSON_PARSE_INT_AS_DOUBLE	(0x002)

/** Allow (+)/(-)inf(inity) and (+)/(-)nan([a-z][0-9]_) and (+)number */
#define	FAST_JSON_INF_NAN		(0x004)

/** Allow octal and hex integer numbers and hex floating point numbers */
#define	FAST_JSON_ALLOW_OCT_HEX		(0x008)

/** Sort object names when printing */
#define	FAST_JSON_SORT_OBJECTS		(0x010)

/** Do not check for eof (allows multiple data) */
#define	FAST_JSON_NO_EOF_CHECK		(0x020)

/** Use bigger allocs for objects */
#define	FAST_JSON_BIG_ALLOC		(0x040)

/** Print unicode escape characters instead of UTF8 */
#define	FAST_JSON_PRINT_UNICODE_ESCAPE	(0x080)

/** Do not reject duplicate object names */
#define	FAST_JSON_NO_DUPLICATE_CHECK	(0x100)

/** Json value type */
  typedef enum fast_json_value_enum
  {
    FAST_JSON_OBJECT,
    FAST_JSON_ARRAY,
    FAST_JSON_INTEGER,
    FAST_JSON_DOUBLE,
    FAST_JSON_STRING,
    FAST_JSON_BOOLEAN,
    FAST_JSON_NULL
  } FAST_JSON_VALUE_TYPE;

/** Json error type */
  typedef enum fast_json_error_enum_type
  {
    FAST_JSON_OK,
    FAST_JSON_MALLOC_ERROR,
    FAST_JSON_COMMENT_ERROR,
    FAST_JSON_NUMBER_ERROR,
    FAST_JSON_CONTROL_CHARACTER_ERROR,
    FAST_JSON_ESCAPE_CHARACTER_ERROR,
    FAST_JSON_UTF8_ERROR,
    FAST_JSON_UNICODE_ERROR,
    FAST_JSON_UNICODE_ESCAPE_ERROR,
    FAST_JSON_STRING_START_ERROR,
    FAST_JSON_STRING_END_ERROR,
    FAST_JSON_VALUE_ERROR,
    FAST_JSON_ARRAY_END_ERROR,
    FAST_JSON_OBJECT_SEPERATOR_ERROR,
    FAST_JSON_OBJECT_END_ERROR,
    FAST_JSON_PARSE_ERROR,
    FAST_JSON_NO_DATA_ERROR,
    FAST_JSON_INDEX_ERROR,
    FAST_JSON_LOOP_ERROR
  } FAST_JSON_ERROR_ENUM;

/** Json data type. All values are returned in this type. */
  typedef struct fast_json_data_struct *FAST_JSON_DATA_TYPE;

/** Json type. Needed for almost all calls. */
  typedef struct fast_json_struct *FAST_JSON_TYPE;

/** Override malloc, realloc and free calls */
  typedef void *(*fast_json_malloc_type) (size_t);
  typedef void *(*fast_json_realloc_type) (void *, size_t);
  typedef void (*fast_json_free_type) (void *);

/** User get character function */
  typedef int (*fast_json_getc_func) (void *user_data);

/** User put string function */
  typedef int (*fast_json_puts_func) (void *user_data, const char *str,
				      unsigned int len);

/**
 * \b Description
 *
 * Create a json object.
 *
 * \param  malloc_fn Pointer to user malloc function. Can be NULL.
 * \param  realloc_fn Pointer to user realloc function. Can be NULL.
 * \param  free_fn Pointer to user free function. Can be NULL.
 * \returns Json object to use in almost all calls.
 */
  extern FAST_JSON_TYPE fast_json_create (fast_json_malloc_type malloc_fn,
					  fast_json_realloc_type realloc_fn,
					  fast_json_free_type free_fn);

/**
 * \b Description
 * 
 * Free json object.
 *
 * \param json Json object from \ref fast_json_create.
 */
  extern void fast_json_free (FAST_JSON_TYPE json);

/**
 * \b Description
 *
 * Set number of objects to save and reuse.
 *
 * \param json Json object from \ref fast_json_create.
 * \param n Number of objects to reuse.
 * \return Error if json is not valid.
 */
  extern FAST_JSON_ERROR_ENUM fast_json_max_reuse (FAST_JSON_TYPE json,
						   size_t n);

/**
 * \b Description
 *
 * Set parser options.
 *
 * \see FAST_JSON_NO_CHECK_LOOP
 * \see FAST_JSON_PARSE_INT_AS_DOUBLE
 * \see FAST_JSON_INF_NAN
 * \see FAST_JSON_ALLOW_OCT_HEX
 * \see FAST_JSON_SORT_OBJECTS
 * \see FAST_JSON_NO_EOF_CHECK
 * \see FAST_JSON_BIG_ALLOC
 * \see FAST_JSON_PRINT_UNICODE_ESCAPE
 * \see FAST_JSON_NO_DUPLICATE_CHECK
 *
 * \param json Json object from \ref fast_json_create.
 * \param value Parse options.
 * \return Error if json is not valid.
 */
  extern FAST_JSON_ERROR_ENUM fast_json_options (FAST_JSON_TYPE json,
						 unsigned int value);

/**
 * \b Description
 *
 * Get parser options.
 *
 * \param json Json object from \ref fast_json_create.
 * \return Options set with \ref fast_json_options
 */
  extern unsigned int fast_json_get_options (FAST_JSON_TYPE json);

/**
 * \b Description
 *
 * Return line number.
 *
 * \param json Json object from \ref fast_json_create.
 * \return Line number of last read.
 */
  extern size_t fast_json_parser_line (FAST_JSON_TYPE json);

/**
 * \b Description
 *
 * Return column number.
 *
 * \param json Json object from \ref fast_json_create.
 * \return Column of last read.
 */
  extern size_t fast_json_parser_column (FAST_JSON_TYPE json);

/**
 * \b Description
 *
 * Return position.
 *
 * \param json Json object from \ref fast_json_create.
 * \return Position of last read.
 */
  extern size_t fast_json_parser_position (FAST_JSON_TYPE json);

/**
 * \b Description
 *
 * Return error enum in case of error. \see FAST_JSON_ERROR_ENUM.
 *
 * \param json Json object from \ref fast_json_create.
 * \return Enum with error information.
 *
 */
  extern FAST_JSON_ERROR_ENUM fast_json_parser_error (FAST_JSON_TYPE json);

/**
 * \b Description
 *
 * Convert error enum to string.
 *
 * \param error Error from \ref fast_json_parser_error.
 * \return String with error information.
 */
  extern const char *fast_json_error_str (FAST_JSON_ERROR_ENUM error);

/**
 * \b Description
 *
 * Return error string in case of error.
 *
 * \param json Json object from \ref fast_json_create.
 * \return String with error information.
 */
  extern const char *fast_json_parser_error_str (FAST_JSON_TYPE json);

/**
 * \b Description
 *
 * Parse a string to json type.
 *
 * \param json Json object from \ref fast_json_create.
 * \param json_str String to parse.
 * \return Parsed data or NULL in case of error.
 */
  extern FAST_JSON_DATA_TYPE fast_json_parse_string (FAST_JSON_TYPE json,
						     const char *json_str);
/**
 * \b Description
 *
 * Parse a string with length to json type.
 *
 * \param json Json object from \ref fast_json_create.
 * \param json_str String to parse.
 * \param len String length to parse.
 * \return Parsed data or NULL in case of error.
 */
  extern FAST_JSON_DATA_TYPE fast_json_parse_string_len (FAST_JSON_TYPE json,
							 const char *json_str,
							 size_t len);

/**
 * \b Description
 *
 * Parse a FILE to json type.
 *
 * \param json Json object from \ref fast_json_create.
 * \param fp File to read from.
 * \return Parsed data or NULL in case of error.
 */
  extern FAST_JSON_DATA_TYPE fast_json_parse_file (FAST_JSON_TYPE json,
						   FILE * fp);

/**
 * \b Description
 *
 * Parse a file name to json type.
 *
 * \param json Json object from \ref fast_json_create.
 * \param name File name to read from.
 * \return Parsed data or NULL in case of error.
 */
  extern FAST_JSON_DATA_TYPE fast_json_parse_file_name (FAST_JSON_TYPE json,
							const char *name);

/**
 * \b Description
 *
 * Parse a file descriptor to json type.
 *
 * \param json Json object from \ref fast_json_create.
 * \param fd File descriptor to read from.
 * \return Parsed data or NULL in case of error.
 */
  extern FAST_JSON_DATA_TYPE fast_json_parse_fd (FAST_JSON_TYPE json, int fd);

/**
 * \b Description
 *
 * Parse a file descriptor to json type.
 *
 * \param json Json object from \ref fast_json_create.
 * \param getc User getc function.
 * \param user_data User data supplied to getc function.
 * \return Parsed data or NULL in case of error.
 */
  extern FAST_JSON_DATA_TYPE fast_json_parse_user (FAST_JSON_TYPE json,
						   fast_json_getc_func getc,
						   void *user_data);

/**
 * \b Description
 *
 * Parse the next part of json data.
 *
 * \param json Json object from \ref fast_json_create.
 * \return Parsed data or NULL in case of error.
 */
  extern FAST_JSON_DATA_TYPE fast_json_parse_next (FAST_JSON_TYPE json);

/**
 * \b Description
 *
 * Parse a string to json type. This is a faster version.
 *
 * \param json Json object from \ref fast_json_create.
 * \param json_str String to parse.
 * \return Parsed data or NULL in case of error.
 */
  extern FAST_JSON_DATA_TYPE fast_json_parse_string2 (FAST_JSON_TYPE json,
						      const char *json_str);

/**
 * \b Description
 *
 * Parse the next part of json data.
 *
 * \param json Json object from \ref fast_json_create.
 * \return Parsed data or NULL in case of error.
 */
  extern FAST_JSON_DATA_TYPE fast_json_parse_string2_next (FAST_JSON_TYPE
							   json);

/**
 * \b Description
 *
 * Compare a json type.
 *
 * \param value1 Json data1 to compare.
 * \param value2 Json data 2 to compare.
 * \return 1 if equal. 0 if not equal.
 */
  extern int fast_json_value_equal (FAST_JSON_DATA_TYPE value1,
				    FAST_JSON_DATA_TYPE value2);

/**
 * \b Description
 *
 * Copy a json type.
 *
 * \param json Json object from \ref fast_json_create.
 * \param value Json data to copy from.
 * \return Copied data or NULL in case of error.
 */
  extern FAST_JSON_DATA_TYPE fast_json_value_copy (FAST_JSON_TYPE json,
						   FAST_JSON_DATA_TYPE value);

/**
 * \b Description
 *
 * Free a json type.
 *
 * \param json Json object from \ref fast_json_create.
 * \param value Value to free.
 */
  extern void fast_json_value_free (FAST_JSON_TYPE json,
				    FAST_JSON_DATA_TYPE value);

/**
 * \b Description
 *
 * Print a json type as string.
 *
 * \param json Json object from \ref fast_json_create.
 * \param value Value to print.
 * \param nice Use spaces, tabs and new line if not equal 0.
 * \return String with printed value or NULL if error occured.
 */
  extern char *fast_json_print_string (FAST_JSON_TYPE json,
				       FAST_JSON_DATA_TYPE value,
				       unsigned int nice);

/**
 * \b Description
 *
 * Print a json type as string. You can use:
 * fast_json_print_string_len (json, value, NULL, 0, nice)
 * to calulate the string size first and then do the call
 * again with correct size.
 *
 * \param json Json object from \ref fast_json_create.
 * \param value Value to print.
 * \param str Value to print to.
 * \param len Size of string.
 * \param nice Use spaces, tabs and new line if not equal 0.
 * \return Length of data printed including null character or -1 if error occured.
 */
  extern int fast_json_print_string_len (FAST_JSON_TYPE json,
					 FAST_JSON_DATA_TYPE value,
					 char *str, size_t len,
					 unsigned int nice);

/**
 * \b Description
 *
 * Print a json type into a FILE.
 *
 * \param json Json object from \ref fast_json_create.
 * \param value Value to print.
 * \param fp FILE to print to.
 * \param nice Use spaces, tabs and new line if not equal 0.
 * \return -1 is an error ocurred else 0.
 */
  extern int fast_json_print_file (FAST_JSON_TYPE json,
				   FAST_JSON_DATA_TYPE value,
				   FILE * fp, unsigned int nice);

/**
 * \b Description
 *
 * Print a json type into a file name.
 *
 * \param json Json object from \ref fast_json_create.
 * \param value Value to print.
 * \param name File name to print to.
 * \param nice Use spaces, tabs and new line if not equal 0.
 * \return -1 is an error ocurred else 0.
 */
  extern int fast_json_print_file_name (FAST_JSON_TYPE json,
					FAST_JSON_DATA_TYPE value,
					const char *name, unsigned int nice);

/**
 * \b Description
 *
 * Print a json type into a file descriptor.
 *
 * \param json Json object from \ref fast_json_create.
 * \param value Value to print.
 * \param fd File descriptor to print to.
 * \param nice Use spaces, tabs and new line if not equal 0.
 * \return -1 is an error ocurred else 0.
 */
  extern int fast_json_print_fd (FAST_JSON_TYPE json,
				 FAST_JSON_DATA_TYPE value,
				 int fd, unsigned int nice);

/**
 * \b Description
 *
 * Print a json type into a file descriptor.
 *
 * \param json Json object from \ref fast_json_create.
 * \param value Value to print.
 * \param puts User puts functions.
 * \param user_data User data supplied to puts function.
 * \param nice Use spaces, tabs and new line if not equal 0.
 * \return -1 is an error ocurred else 0.
 */
  extern int fast_json_print_user (FAST_JSON_TYPE json,
				   FAST_JSON_DATA_TYPE value,
				   fast_json_puts_func puts,
				   void *user_data, unsigned int nice);
/**
 * \b Description
 *
 * Free the json printed string.
 *
 * \param json Json object from \ref fast_json_create.
 * \param str String from \ref fast_json_print_string.
 */
  extern void fast_json_release_print_value (FAST_JSON_TYPE json, char *str);

/**
 * \b Description
 *
 * Create json null type.
 *
 * \param json Json object from \ref fast_json_create.
 * \return Json null type or NULL of error occured.
 */
  extern FAST_JSON_DATA_TYPE fast_json_create_null (FAST_JSON_TYPE json);

/**
 * \b Description
 *
 * Create json true type.
 *
 * \param json Json object from \ref fast_json_create.
 * \return Json true type or NULL of error occured.
 */
  extern FAST_JSON_DATA_TYPE fast_json_create_true (FAST_JSON_TYPE json);

/**
 * \b Description
 *
 * Create json false type.
 *
 * \param json Json object from \ref fast_json_create.
 * \return Json false type or NULL of error occured.
 */
  extern FAST_JSON_DATA_TYPE fast_json_create_false (FAST_JSON_TYPE json);

/**
 * \b Description
 *
 * Create json boolean type.
 *
 * \param json Json object from \ref fast_json_create.
 * \param value Input boolean value.
 * \return Json false/true type or NULL of error occured.
 */
  extern FAST_JSON_DATA_TYPE fast_json_create_boolean_value (FAST_JSON_TYPE
							     json,
							     unsigned int
							     value);

/**
 * \b Description
 *
 * Create json integer type.
 *
 * \param json Json object from \ref fast_json_create.
 * \param value Integer value.
 * \return Json integer type or NULL of error occured.
 */
  extern FAST_JSON_DATA_TYPE fast_json_create_integer_value (FAST_JSON_TYPE
							     json,
							     fast_json_int_64
							     value);

/**
 * \b Description
 *
 * Create json double type.
 *
 * \param json Json object from \ref fast_json_create.
 * \param value Double value
 * \return Json double type or NULL of error occured.
 */
  extern FAST_JSON_DATA_TYPE fast_json_create_double_value (FAST_JSON_TYPE
							    json,
							    double value);

/**
 * \b Description
 *
 * Create json string type.
 *
 * \param json Json object from \ref fast_json_create.
 * \param value String value.
 * \return Json string type or NULL of error occured.
 */
  extern FAST_JSON_DATA_TYPE fast_json_create_string (FAST_JSON_TYPE json,
						      const char *value);

/**
 * \b Description
 *
 * Create json array type.
 *
 * \param json Json object from \ref fast_json_create.
 * \return Json array type or NULL of error occured.
 */
  extern FAST_JSON_DATA_TYPE fast_json_create_array (FAST_JSON_TYPE json);

/**
 * \b Description
 *
 * Create json object type.
 *
 * \param json Json object from \ref fast_json_create.
 * \return Json object type or NULL of error occured.
 */
  extern FAST_JSON_DATA_TYPE fast_json_create_object (FAST_JSON_TYPE json);

/**
 * \b Description
 *
 * Create json array with booleans.
 *
 * \param json Json object from \ref fast_json_create.
 * \param numbers Boolean array.
 * \param len Size of array.
 * \return Json boolean array type or NULL of error occured.
 */
  extern FAST_JSON_DATA_TYPE fast_json_create_boolean_array (FAST_JSON_TYPE
							     json,
							     const unsigned
							     int *numbers,
							     size_t len);

/**
 * \b Description
 *
 * Create json array with integers.
 *
 * \param json Json object from \ref fast_json_create.
 * \param numbers Integer array.
 * \param len Size of array.
 * \return Json integer array type or NULL of error occured.
 */
  extern FAST_JSON_DATA_TYPE fast_json_create_integer_array (FAST_JSON_TYPE
							     json,
							     const
							     fast_json_int_64
							     * numbers,
							     size_t len);

/**
 * \b Description
 *
 * Create json array with doubles.
 *
 * \param json Json object from \ref fast_json_create.
 * \param numbers Double array.
 * \param len Size of array.
 * \return Json double array type or NULL of error occured.
 */
  extern FAST_JSON_DATA_TYPE fast_json_create_double_array (FAST_JSON_TYPE
							    json,
							    const double
							    *numbers,
							    size_t len);

/**
 * \b Description
 *
 * Create json array with strings.
 *
 * \param json Json object from \ref fast_json_create.
 * \param strings String array.
 * \param len Size of array.
 * \return Json string array type or NULL of error occured.
 */
  extern FAST_JSON_DATA_TYPE fast_json_create_string_array (FAST_JSON_TYPE
							    json,
							    const char
							    **strings,
							    size_t len);

/**
 * \b Description
 *
 * Add value to end of array.
 *
 * \param json Json object from \ref fast_json_create.
 * \param array Json array.
 * \param value Value to add at end.
 * \return Enum with error information.
 */
  extern FAST_JSON_ERROR_ENUM fast_json_add_array (FAST_JSON_TYPE json,
						   FAST_JSON_DATA_TYPE array,
						   FAST_JSON_DATA_TYPE value);

/**
 * \b Description
 *
 * Add value to end of object.
 *
 * \param json Json object from \ref fast_json_create.
 * \param object Json object.
 * \param name Name of value.
 * \param value Value to add at end.
 * \return Enum with error information.
 */
  extern FAST_JSON_ERROR_ENUM fast_json_add_object (FAST_JSON_TYPE json,
						    FAST_JSON_DATA_TYPE
						    object, const char *name,
						    FAST_JSON_DATA_TYPE
						    value);

/**
 * \b Description
 *
 * Patch array at index with value.
 *
 * \param json Json object from \ref fast_json_create.
 * \param array Json array.
 * \param value Value to patch.
 * \param index Index of array to patch.
 * \return Enum with error information.
 */
  extern FAST_JSON_ERROR_ENUM fast_json_patch_array (FAST_JSON_TYPE json,
						     FAST_JSON_DATA_TYPE
						     array,
						     FAST_JSON_DATA_TYPE
						     value, size_t index);
/**
 * \b Description
 *
 * Insert array at index with value. (Slow for large arrays)
 *
 * \param json Json object from \ref fast_json_create.
 * \param array Json array.
 * \param value Value to insert.
 * \param index Index to insert value.
 * \return Enum with error information.
 */
  extern FAST_JSON_ERROR_ENUM fast_json_insert_array (FAST_JSON_TYPE json,
						      FAST_JSON_DATA_TYPE
						      array,
						      FAST_JSON_DATA_TYPE
						      value, size_t index);

/**
 * \b Description
 *
 * Remove value in array at index. (Slow for large arrays)
 *
 * \param json Json object from \ref fast_json_create.
 * \param array Json array.
 * \param index Index to remove value.
 * \return Enum with error information.
 */
  extern FAST_JSON_ERROR_ENUM fast_json_remove_array (FAST_JSON_TYPE json,
						      FAST_JSON_DATA_TYPE
						      array, size_t index);

/**
 * \b Description
 *
 * Patch object at index with value.
 *
 * \param json Json object from \ref fast_json_create.
 * \param object Json object.
 * \param value Value to patch.
 * \param index Index of object to patch.
 * \return Enum with error information.
 */
  extern FAST_JSON_ERROR_ENUM fast_json_patch_object (FAST_JSON_TYPE json,
						      FAST_JSON_DATA_TYPE
						      object,
						      FAST_JSON_DATA_TYPE
						      value, size_t index);

/**
 * \b Description
 *
 * Insert object at index with value. (Slow for large objects)
 *
 * \param json Json object from \ref fast_json_create.
 * \param object Json object.
 * \param name Name of value.
 * \param value Value to insert.
 * \param index Index to insert value.
 * \return Enum with error information.
 */
  extern FAST_JSON_ERROR_ENUM fast_json_insert_object (FAST_JSON_TYPE json,
						       FAST_JSON_DATA_TYPE
						       object,
						       const char *name,
						       FAST_JSON_DATA_TYPE
						       value, size_t index);

/**
 * \b Description
 *
 * Remove value in object at index. (Slow for large objects)
 *
 * \param json Json object from \ref fast_json_create.
 * \param object Json object.
 * \param index Index to remove value. 
 * \return Enum with error information.
 */
  extern FAST_JSON_ERROR_ENUM fast_json_remove_object (FAST_JSON_TYPE json,
						       FAST_JSON_DATA_TYPE
						       object, size_t index);

/**
 * \b Description
 *
 * Get json type.
 *
 * \param data Json data type.
 * \return Json type.
 */
  extern FAST_JSON_VALUE_TYPE fast_json_get_type (FAST_JSON_DATA_TYPE data);

/**
 * \b Description
 *
 * Get json array size.
 *
 * \param data Json array data type.
 * \return Array size.
 */
  extern size_t fast_json_get_array_size (FAST_JSON_DATA_TYPE data);

/**
 * \b Description
 *
 * Get json array value at index.
 *
 * \param data Json array data type.
 * \param index Index in array.
 * \return Json data at index.
 */
  extern FAST_JSON_DATA_TYPE fast_json_get_array_data (FAST_JSON_DATA_TYPE
						       data, size_t index);

/**
 * \b Description
 *
 * Get json object size.
 *
 * \param data Json object data type.
 * \return Object size.
 */
  extern size_t fast_json_get_object_size (FAST_JSON_DATA_TYPE data);

/**
 * \b Description
 *
 * Get json object name at index.
 *
 * \param data Json object data type.
 * \param index Index in object.
 * \return Name of json data at index.
 */
  extern char *fast_json_get_object_name (FAST_JSON_DATA_TYPE data,
					  size_t index);
/**
 * \b Description
 *
 * Get json object value at index.
 *
 * \param data Json object data type.
 * \param index Index in object.
 * \return Json data at index.
 */
  extern FAST_JSON_DATA_TYPE fast_json_get_object_data (FAST_JSON_DATA_TYPE
							data, size_t index);
/**
 * \b Description
 *
 * Get json object value by name.
 *
 * \param data Json object data type.
 * \param name Name to seach for.
 * \return Json data at index.
 */
  extern FAST_JSON_DATA_TYPE fast_json_get_object_by_name (FAST_JSON_DATA_TYPE
							   data,
							   const char *name);

/**
 * \b Description
 *
 * Get json integer.
 *
 * \param data Json integer data type.
 * \return Integer value.
 */
  extern fast_json_int_64 fast_json_get_integer (FAST_JSON_DATA_TYPE data);

/**
 * \b Description
 *
 * Get json double.
 *
 * \param data Json double data type.
 * \return Double value.
 */
  extern double fast_json_get_double (FAST_JSON_DATA_TYPE data);

/**
 * \b Description
 *
 * Get json string.
 *
 * \param data Json string data type.
 * \return String value.
 */
  extern char *fast_json_get_string (FAST_JSON_DATA_TYPE data);

/**
 * \b Description
 *
 * Get json boolean.
 *
 * \param data Json boolean data type.
 * \return Boolean value.
 */
  extern unsigned int fast_json_get_boolean (FAST_JSON_DATA_TYPE data);

/**
 * \b Description
 *
 * Set json integer.
 *
 * \param data Json integer data type.
 * \param value New value.
 * \return Enum with error information.
 */
  extern FAST_JSON_ERROR_ENUM fast_json_set_integer (FAST_JSON_DATA_TYPE data,
						     fast_json_int_64 value);
/**
 * \b Description
 *
 * Set json double.
 *
 * \param json Json object from \ref fast_json_create.
 * \param data Json double data type.
 * \param value New value.
 * \return Enum with error information.
 */
  extern FAST_JSON_ERROR_ENUM fast_json_set_double (FAST_JSON_TYPE json,
						    FAST_JSON_DATA_TYPE data,
						    double value);

/**
 * \b Description
 *
 * Set json string.
 *
 * \param json Json object from \ref fast_json_create.
 * \param data Json string data type.
 * \param value New value.
 * \return Enum with error information.
 */
  extern FAST_JSON_ERROR_ENUM fast_json_set_string (FAST_JSON_TYPE json,
						    FAST_JSON_DATA_TYPE data,
						    const char *value);

/**
 * \b Description
 *
 * Set json boolean.
 *
 * \param data Json boolean data type.
 * \param value New value.
 * \return Enum with error information.
 */
  extern FAST_JSON_ERROR_ENUM fast_json_set_boolean_value (FAST_JSON_DATA_TYPE
							   data,
							   unsigned int
							   value);

/**
 * \b Description
 *
 * Generate json crc from string.
 *
 * \param json Json object from \ref fast_json_create.
 * \param str String to generate crc from.
 * \param crc Returned crc.
 * \return Enum with error information.
 */
  extern FAST_JSON_ERROR_ENUM fast_json_calc_crc_string (FAST_JSON_TYPE json,
							 const char *str,
							 unsigned int *crc);

/**
 * \b Description
 *
 * Generate json crc from string with length.
 *
 * \param json Json object from \ref fast_json_create.
 * \param str String to generate crc from.
 * \param len Length of string.
 * \param crc Returned crc.
 * \return Enum with error information.
 */
  extern FAST_JSON_ERROR_ENUM fast_json_calc_crc_string_len (FAST_JSON_TYPE
							     json,
							     const char *str,
							     size_t len,
							     unsigned int
							     *crc);

/**
 * \b Description
 *
 * Generate json crc from FILE.
 *
 * \param json Json object from \ref fast_json_create.
 * \param fp FILE to generate crc from.
 * \param crc Returned crc.
 * \return Enum with error information.
 */
  extern FAST_JSON_ERROR_ENUM fast_json_calc_crc_file (FAST_JSON_TYPE json,
						       FILE * fp,
						       unsigned int *crc);

/**
 * \b Description
 *
 * Generate json crc from file name.
 *
 * \param json Json object from \ref fast_json_create.
 * \param name File name to generate crc from.
 * \param crc Returned crc.
 * \return Enum with error information.
 */
  extern FAST_JSON_ERROR_ENUM fast_json_calc_crc_file_name (FAST_JSON_TYPE
							    json,
							    const char *name,
							    unsigned int
							    *crc);

/**
 * \b Description
 *
 * Generate json crc from file descriptor.
 *
 * \param json Json object from \ref fast_json_create.
 * \param fd File descriptor to generate crc from.
 * \param crc Returned crc.
 * \return Enum with error information.
 */
  extern FAST_JSON_ERROR_ENUM fast_json_calc_crc_fd (FAST_JSON_TYPE json,
						     int fd,
						     unsigned int *crc);

/**
 * \b Description
 *
 * Generate next crc of json data.
 *
 * \param json Json object from \ref fast_json_create.
 * \param crc Returned crc.
 * \return Enum with error information.
 */
  extern FAST_JSON_ERROR_ENUM fast_json_calc_crc_next (FAST_JSON_TYPE json,
						       unsigned int *crc);

#if defined (__cplusplus)
}
#endif

#endif
