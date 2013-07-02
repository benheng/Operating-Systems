CFLAG := -O0 -fbuiltin -g
target = client
source = client.c
object = $(patsubst %.c,%.o,$(source))

# Naming our Phony Targets
.PHONY: clean all client

all: $(target)

client: client.o
	cc $(CFLAG) -o client $(object)

$(object): $(source)

clean:
	rm $(object) $(target)
