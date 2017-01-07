CC=gcc
CFLAGS= -Wall -L. -pg
AR=ar
ARFLAGS= rv
LIBJP=libjp.a
LIBS= -ljp
RM=rm -f
TEST=test

libjp.a: json_parser.o
	${AR} ${ARFLAGS} ${LIBJP} $^

test: test.o libjp.a
	${CC} -o $@ $^ ${CFLAGS} ${LIBS}

json_parser.o: json_parser.c
	${CC} -c $< ${CFLAGS}

test.o: test.c
	${CC} -c $^ ${CFLAGS}

.PHONY: clean
clean:
	${RM} ${LIBJP} ${TEST} *.o
