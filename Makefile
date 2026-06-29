.PHONY: all
all: bin/memgrem bin/test bin/inject.so

obj/%.o: src/%.c
	clang -fPIC -c -I include -o $@ $^ -O2

bin/memgrem: obj/main.o obj/string_list.o obj/subject.o obj/utils.o
	clang -fPIC -I include -o $@ $^ -O2

bin/test: src/test.c
	clang -fPIC -I include -o $@ $^ -O0

bin/inject.so: obj/inject_main.o obj/inject_control.o obj/utils.o
	clang -shared -fPIC -I include -o $@ $^ /usr/lib/xed/libxed-ild.a

.PHONY: clean
clean:
	rm -rf obj/* bin/*
