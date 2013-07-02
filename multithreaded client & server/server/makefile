CFLAG := -O0 -fbuiltin -g
THREAD = -pthread
target = server
source = server.c
object = $(patsubst %.c,%.o,$(source))

# Naming our Phony Targets
.PHONY: clean all server

all: $(target)

server: server.o
	gcc $(CFLAG) -o server $(object) $(THREAD)

$(object): $(source)

clean:
	rm $(object) $(target)
