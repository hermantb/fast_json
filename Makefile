OPTIONS = -g -O3 -Wall
CC = gcc

# -fsanitize=address,pointer-compare,pointer-subtract,leak,undefined
# -fsanitize-address-use-after-scope
# export ASAN_OPTIONS='detect_invalid_pointer_pairs=2'
# export LSAN_OPTIONS=''

# .a files are used here to make things simpler to run. Normally the .so version should be used.

all: fast_json_benchmark fast_json_test libfast_json.a libfast_json.so

allf: all fast_json_benchmarkf fast_json_testf

fast_json_benchmark: fast_json_benchmark.c libfast_json.a
	${CC} ${OPTIONS} fast_json_benchmark.c libfast_json.a -o fast_json_benchmark

fast_json_test: fast_json_test.c libfast_json.a
	${CC} ${OPTIONS} fast_json_test.c libfast_json.a -o fast_json_test

libfast_json.a: fast_json.h fast_json.c
	${CC} ${OPTIONS} -c fast_json.c
	rm -f libfast_json.a
	ar rv libfast_json.a fast_json.o
	rm fast_json.o

libfast_json.so: fast_json.h fast_json.c
	${CC} ${OPTIONS} -fPIC -shared -o libfast_json.so fast_json.c

libfast_jsonf.a: fast_json.h fast_json.c fast_convert/libfast_convert.a
	${CC} ${OPTIONS} -Ifast_convert -DUSE_FAST_CONVERT -c fast_json.c
	rm -f libfast_jsonf.a
	ar rv libfast_jsonf.a fast_json.o
	rm fast_json.o

fast_json_benchmarkf: fast_json_benchmark.c libfast_jsonf.a fast_convert/libfast_convert.a
	${CC} ${OPTIONS} fast_json_benchmark.c libfast_jsonf.a fast_convert/libfast_convert.a -o fast_json_benchmarkf

fast_json_testf: fast_json_test.c libfast_jsonf.a fast_convert/libfast_convert.a
	${CC} ${OPTIONS} fast_json_test.c libfast_jsonf.a fast_convert/libfast_convert.a -o fast_json_testf

fast_convert/libfast_convert.a: fast_convert
	cd fast_convert; make

fast_convert:
	git clone https://github.com/hermantb/fast_convert.git

test: fast_json_test
	./fast_json_test

testf: fast_json_testf
	./fast_json_testf

benchmark: fast_json_benchmark
	./fast_json_benchmark

benchmarkf: fast_json_benchmarkf
	./fast_json_benchmarkf

large_bench: sf-city-lots-json fast_json_benchmark
	./fast_json_benchmark --count=1 sf-city-lots-json/citylots.json
	./fast_json_benchmark --count=1 --fast_string sf-city-lots-json/citylots.json 
	./fast_json_benchmark --count=1 --big sf-city-lots-json/citylots.json
	./fast_json_benchmark --count=1 --fast_string --big sf-city-lots-json/citylots.json

large_benchf: sf-city-lots-json fast_json_benchmarkf
	./fast_json_benchmarkf --count=1 sf-city-lots-json/citylots.json
	./fast_json_benchmarkf --count=1 --fast_string sf-city-lots-json/citylots.json 
	./fast_json_benchmarkf --count=1 --big sf-city-lots-json/citylots.json
	./fast_json_benchmarkf --count=1 --fast_string --big sf-city-lots-json/citylots.json

sf-city-lots-json:
	git clone https://github.com/zemirco/sf-city-lots-json.git

doc: fast_json.h README.md
	doxygen

clean:
	rm -rf fast_json_benchmark fast_json_test fast_json_benchmarkf fast_json_testf libfast_json.a libfast_json.so libfast_jsonf.a doc

realclean: clean
	rm -rf fast_convert sf-city-lots-json
