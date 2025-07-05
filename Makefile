TARGET = FATik

CC = gcc

CFLAGS = -Wall -Wextra -O2

SRC = src/fatman.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f $(TARGET)
