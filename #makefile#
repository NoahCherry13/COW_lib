override CFLAGS := -Wall -Werror -std=gnu99 -pedantic -O0 -g -pthread $(CFLAGS)
override LDLIBS := -pthread $(LDLIBS)

testfiles =./test

tls.o: tls.c

test: test.o tls.o

.PHONY: clean

testprogs: $(testfiles) 

clean:
	rm -f *.o $(testfiles)
