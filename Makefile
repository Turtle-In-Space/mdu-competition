CC = gcc
CFLAGS = -g -Wall -Wextra -Wpedantic -Wmissing-declarations \
				 -Wmissing-prototypes -Wold-style-definition -O2 -fno-omit-frame-pointer
LFLAGS = -lm -pthread 

BIN = mdu_competition
SRC = src/$(BIN).c src/thread_pool_competition.c src/stack_competition.c
INC = include/
OBJ := $(SRC:%.c=%.o)

all: $(BIN)

$(BIN): $(OBJ) $(INC)
	$(CC) $(LFLAGS) -o $(BIN) $(OBJ)

$(OBJ): %.o:%.c $(INC)
	$(CC) $(CFLAGS) -I $(INC) -c $< -o $@ 

clean: 
	rm -rf $(BIN) $(OBJ)
