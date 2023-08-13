CC = gcc
OBJECTS = main.o input.o compute.o
power: $(OBJECTS)
	$(CC) $(OBJECTS) -o power -lm

main.o: main.c main.h compute.h input.h

input.o: input.c input.h

compute.o: compute.c compute.h

clean:
	rm *.o power
