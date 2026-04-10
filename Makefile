CC        = gcc
HIREDIS   = /usr/local/src/antirez-hiredis-d5d8843
CFLAGS    = -Wall -Wextra -O2 -I$(HIREDIS)
LIBS      = $(HIREDIS)/libhiredis.a
TARGET    = perfparse

all: $(TARGET)

$(TARGET): perfparse.c
	$(CC) $(CFLAGS) -o $@ $< $(LIBS)

test: $(TARGET)
	@bash test.sh

clean:
	rm -f $(TARGET)

.PHONY: all test clean
