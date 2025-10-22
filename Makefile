CC = gcc
CFLAGS = -g -Wall -Wextra -Wpedantic -Wmissing-declarations \
				 -Wmissing-prototypes -Wold-style-definition -Ofast -fno-omit-frame-pointer
LFLAGS = -lm -pthread 

BIN = mdu_competition
SRC = $(BIN).c thread_pool_competition.c stack_competition.c
INC = thread_pool_competition.h stack_competition.h
OBJ := $(SRC:%.c=%.o)

all: $(BIN)

$(BIN): $(OBJ) $(INC)
	$(CC) $(LFLAGS) -o $(BIN) $(OBJ)

$(OBJ): %.o:%.c $(INC)
	$(CC) $(CFLAGS) -c $< 

clean: 
	rm -rf $(BIN) $(OBJ)
