CC = gcc
EXTRA_OPTS = -Wfatal-errors -Wextra -pedantic-errors
CFLAGS = -g -Wall $(EXTRA_OPTS)
OPTS=
ARGS=./50_routers_150_edges ./test_commands

ruterdrift: ruterdrift.c
	$(CC) $(CFLAGS) -o $@ $<

run: ruterdrift
	./ruterdrift $(ARGS)

valgrind: ruterdrift
	valgrind ./ruterdrift $(ARGS)

valgrind-all: ruterdrift
	valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes $(OPTS) ./ruterdrift $(ARGS)

clean:
	rm -f ruterdrift
