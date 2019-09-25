NAME=mbat
SRC=main.c
OBJ=$(SRC:.c=.o)
CFLAGS=-Wall -Werror `pkg-config --cflags xcb`
LDFLAGS=`pkg-config --libs xcb`

LD=$(CC)
RM=rm -fr

all: $(NAME)

$(NAME): $(OBJ)
	$(LD) $(LDFLAGS) -o $(NAME) $(OBJ)

clean:
	$(RM) $(OBJ) $(NAME)

re: clean all
