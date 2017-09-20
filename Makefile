OBJ1=v4l2
OBJ2=v4l2_2

all:$(OBJ1) $(OBJ2)

$(OBJ1): $(OBJ1).o sock.o
	gcc $(OBJ1).o sock.o -o $(OBJ1)

$(OBJ1).o: $(OBJ1).c
	gcc -c $(OBJ1).c

sock.o:sock.c sock.h
	gcc -c sock.c

$(OBJ2): $(OBJ2).o
	gcc $(OBJ2).o -o $(OBJ2)

$(OBJ2).o: $(OBJ2).c
	gcc -c $(OBJ2).c

clean:
	rm -rf *.o $(OBJ1) $(OBJ2)
