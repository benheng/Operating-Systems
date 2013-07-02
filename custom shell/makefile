CFLAG := -O0 -fbuiltin -g
target = bsh
source = bsh.c
object = $(patsubst %.c,%.o,$(source))

# Naming our Phony Targets
.PHONY: clean all bsh

all: $(target)

bsh: bsh.o
    cc $(CFLAG) -o bsh $(object)

$(object): $(source)

clean:
    rm $(object) $(target)
