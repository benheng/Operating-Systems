CFLAG := -O0 -fbuiltin -g
target = server
source = server.c
object = $(patsubst %.c,%.o,$(source))

# Naming our Phony Targets
.PHONY: clean all server

all: $(target)

server: server.o
	cc $(CFLAG) -o server $(object)

$(object): $(source)

clean:
	rm $(object) $(target)
