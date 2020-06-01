TARGET = cifar-server
TEST_TARGET = testapp

CFLAGS += -Wall -Wextra --std=gnu99 -g -O0 -D_GNU_SOURCE -MMD

SRCS = \
	bmp.c \
	handler.c \
	http_request.c \
	http_response.c \
	io.c \
	resources.c \
	server.c \
	stringbuilder.c \
	stringutils.c

ALL_SRCS = $(SRCS) main.c tests.c

all: $(TARGET) $(TEST_TARGET)

$(TARGET): main.o $(SRCS:%.c=%.o)
	$(CC) $(CFLAGS) -o $@ $^
	echo "OPTIMISATIONS ARE TURNED OFF, PLEASE ENABLE THEM AGAIN (-O2) AFTER EVERYTHING IS DONE"

$(TEST_TARGET): tests.o $(SRCS:%.c=%.o)
	$(CC) $(CFLAGS) -o $@ $^

.PHONY: test clean

test: $(TEST_TARGET)
	./$(TEST_TARGET)

clean:
	rm -f -- $(TARGET) $(TEST_TARGET) $(ALL_SRCS:%.c=%.o) $(ALL_SRCS:%.c=%.d)

-include $(ALL_SRCS:%.c=%.d)
