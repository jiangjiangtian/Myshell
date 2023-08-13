CC = gcc
CFLAGS = -g
OBJECTS = built_in_command.o
FILES = myint myspin mysplit mystop

ALL: myshell $(FILES)

myshell: myshell.c built_in_command.o
	$(CC) $(CFLAGS) $< -o myshell $(OBJECTS)

built_in_command: built_in_command.c built_in_command.h