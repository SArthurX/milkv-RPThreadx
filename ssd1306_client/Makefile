# Makefile for SSD1306 Client Library

CC = ../../host-tools/gcc/riscv64-linux-musl-x86_64/bin/riscv64-unknown-linux-musl-gcc
CFLAGS = -Wall -O2 -I.
LDFLAGS =

# Mailbox-based Library (legacy)
LIB_NAME = libssd1306_client.a
LIB_SRC = ssd1306_client.c ssd1306_draw.c
LIB_OBJ = $(LIB_SRC:.c=.o)

# RPMsg-based Library (new)
RPMSG_LIB_NAME = libssd1306_rpmsg_client.a
RPMSG_LIB_SRC = ssd1306_rpmsg_client.c ssd1306_draw.c
RPMSG_LIB_OBJ = ssd1306_rpmsg_client.o ssd1306_draw.o

# Test programs
TEST_NAME = test_ssd1306
TEST_SRC = test_ssd1306.c


.PHONY: all clean install rpmsg mailbox

all: $(RPMSG_LIB_NAME) $(TEST_NAME)

mailbox: $(LIB_NAME)

rpmsg: $(RPMSG_LIB_NAME) 

$(LIB_NAME): $(LIB_OBJ)
	ar rcs $@ $^
	@echo "Built library: $@"

$(RPMSG_LIB_NAME): $(RPMSG_LIB_OBJ)
	ar rcs $@ $^
	@echo "Built RPMsg library: $@"

$(TEST_NAME): test_ssd1306.o $(RPMSG_LIB_NAME)
	$(CC) $(CFLAGS) -o $@ $< -L. -lssd1306_rpmsg_client $(LDFLAGS)
	@echo "Built test program: $@"

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o $(LIB_NAME) $(RPMSG_LIB_NAME) $(TEST_NAME) $(TEST_RPMSG_NAME) $(TEST_STRING_NAME) $(INTERACTIVE_NAME)

install: $(RPMSG_LIB_NAME)
	@echo "Installing RPMsg library and headers..."
	@mkdir -p $(DESTDIR)/usr/lib
	@mkdir -p $(DESTDIR)/usr/include
	cp $(RPMSG_LIB_NAME) $(DESTDIR)/usr/lib/
	cp ssd1306_rpmsg_client.h ssd1306_draw.h $(DESTDIR)/usr/include/
	@echo "Installation complete"

