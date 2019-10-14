fast\_json
==========

A fast implementation of a json library. It it a C library for encoding/decoding and manipulating json data. It can use fast\_convert (https://github.com/hermantb/fast_convert.git) to print and parse integer and doubles faster.

Main features:

 * Simple to use.
 * Fast.
 * Full test suite.
 * Benchmark test.

## Compiling/Testing/Benchmarking.

<pre>
Just type 'make' to compile the library and testcode.
If you want to use fast_convert type 'make allf'.
To test the libary type 'make test' or 'make testf' for the faster version.
To run the benchmark type 'make benchmark' or 'make benchmarkf' for the faster version.
The run a larger benchmark type 'make large_bench' or 'make large_benchf' for the faster version.
The large benchmark runs with different options wich will be explained later.
The documentation (doxygen) can be geneted with 'make doc'.
If you want to clean things up you can do 'make clean'.
If you want to remove also the downloaded git repositories you can do 'make realclean'.
</pre>

If you want to use this library in your project you can include the header file '\#include \<fast\_json.h\>' and the compile with -lfast\_json (and optional -lfast\_convert for faster print and scan functions).

## RFC Conformance and some remarks.

 * The library is RFC 4627 (https://tools.ietf.org/html/rfc4627.html) compatible. Only UTF8 is implemented. So UTF-16LE, UTF-16BE, UTF-32LE or UTF-32BE are not implemented.
 * Strings are C-style strings with a null character at the end. All Unicode charecter from U+0000 through U+10FFFF are allowed. The value U+0000 stays always as "\u0000" in strings and can not be used as '\0'.
 * Integer numbers are 64 bits signed. There is support for octal and hexadecimal numbers. If an integer number (number without '.' or 'e') does not fit in a 64 integer type a double is used. Integer values should be printed with 'FAST\_JSON\_FMT\_INT'.
 * Real numbers are 64 bits IEEE doubles. There is special support for inf and nan and hex floating point.
 * The maximum nesting depth is tested for 10000 (See testcode). Perhaps larger values work. If you really need that much nesting you probably should redesign your json data. Also the stack size can be increaded with ulimit.
 * Objects will never be sorted. Order of object keys is always preserved.

## Special options.

There are several special options with this library.

 * FAST_JSON_NO_CHECK_LOOP		Disables loop checking if set.
 * FAST_JSON_PARSE_INT_AS_DOUBLE	Parse all integer values as double.
 * FAST_JSON_INF_NAN			Allow inf and nan.
 * FAST_JSON_ALLOW_OCT_HEX		Allow octal and hexadecimal integer numbers and floating pointe hex numbers.
 * FAST_JSON_SORT_OBJECTS		Sort object names during printing.
 * FAST_JSON_NO_EOF_CHECK		Disable the eof check. This allows multiple calls to parser to parse larger values. See testcode how this works.
 * FAST_JSON_BIG_ALLOC			Use big malloc's for json objects. This may require more memory but is faster.
 * FAST_JSON_PRINT_UNICODE_ESCAPE	Print unicode escape characters instead of UTF8.
 * FAST_JSON_NO_DUPLICATE_CHECK		Do not reject duplicate object names.

## API Documentation.

Run 'make doc' and then open 'doc/html/index.html' for the doxygen documentation. This will be updated in the future.

## Thread safety.

The library is not thead safe but it is fully reentrant. There are no global variables.
So if you want to share data in different threads you have to do your own locking.

## Memory allocation functions.

During creating of the main json data pointer you can specify your own alloc routines. See the benchmark and test code for an example.

## Benchmark

The benchmark options are: (Use ./fast_json_benchmark --help):

<pre>
Usage: ./fast_json_benchmark [options] [filename]
Options:
--count=n         Run count times (default 1000)
--reuse=n         Use object reuse
--print_time:     Run print time test
--parse_time:     Run parse time test
--stream_time:    Run stream time test
--hex:            Allow oct and hex numbers
--infnan:         Allow inf and nan
--big:            Use big allocs
--no_duplicate:   Do not check duplicate object names
--check_alloc:    Check allocs
--fast_string:    Use fast string parser
--print:          Print result
--nice:           Print result with spaces and newlines
--unicode_escape: Print unicode escape instead of utf8
</pre>

Default there is a simple internal test. Normally you supply a file to run the benchmark. If you do not supply --print_time, --parse_time or --stream_time all 3 benchmarks are run.

## License

Licensed under either of

 * Apache License, Version 2.0 ([LICENSE-APACHE](LICENSE-APACHE) or http://www.apache.org/licenses/LICENSE-2.0)
 * MIT license ([LICENSE-MIT](LICENSE-MIT) or http://opensource.org/licenses/MIT)

at your option.

### Contribution

Unless you explicitly state otherwise, any contribution intentionally submitted
for inclusion in fast\_convert by you, as defined in the Apache-2.0 license, shall be
dual licensed as above, without any additional terms or conditions.
