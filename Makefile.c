CC=gcc
CFLAGS=-I.
DEPS = myhttpd.h
OBJ = myhttpd_main.o myhttpd_queuing.o myhttpd_sched.o myhttpd_worker.o

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

myhttpd: $(OBJ)
	gcc -o $@ $^ $(CFLAGS) -lpthread

.PHONY: clean

clean:
	rm -f *.o myhttpd