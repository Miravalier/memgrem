.PHONY: all
all: bin/memgrem bin/test bin/inject.so

obj/%.o: src/%.c
	clang -fPIC -c -I include -o $@ $^

bin/memgrem: obj/main.o obj/string_list.o obj/subject.o obj/utils.o obj/gval.o
	clang -fPIC -I include -o $@ $^

bin/test: obj/test.o
	clang -fPIC -I include -o $@ $^ -O0

bin/inject.so: obj/inject_main.o obj/inject_control.o obj/utils.o obj/gval.o
	clang -shared -fPIC -I include -o $@ $^ /usr/lib/xed/libxed-ild.a

.PHONY: clean
clean:
	rm -rf obj/* bin/*
