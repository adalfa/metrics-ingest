CC      = gcc
CFLAGS  = -Wall -Wextra -O2
LIBS    = -lhiredis
TARGET  = perfparse

all: $(TARGET)

$(TARGET): perfparse.c
	$(CC) $(CFLAGS) -o $@ $< $(LIBS)

test: $(TARGET)
	@bash test.sh

clean:
	rm -f $(TARGET)

.PHONY: all test clean
