.PHONY: all
all: bin/memgrem bin/test bin/inject.so

obj/%.o: src/%.c
	clang -fPIC -c -I include -o $@ $^

obj/main.o: src/main.c
obj/string_list.o: src/string_list.c
obj/subject.o: src/subject.c
obj/injectable.o: src/injectable.c

bin/memgrem: obj/main.o obj/string_list.o obj/subject.o
	clang -fPIC -I include -o $@ $^

bin/test: obj/test.o
	clang -fPIC -I include -o $@ $^

bin/inject.so: obj/injectable.o
	clang -shared -fPIC -I include -o $@ $^

.PHONY: clean
clean:
	rm -rf obj/* bin/*
