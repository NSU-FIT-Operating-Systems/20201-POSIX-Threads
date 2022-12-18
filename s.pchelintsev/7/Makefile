run:
	gcc cpy.c dirs.c ../common/*.c -I ../common/ -lm -lpthread -Wall -Wpedantic -O3 -o lab7
	./lab7

debugrun:
	clang cpy.c dirs.c ../common/*.c -I ../common/ -g -lm -lpthread -Wall -Wpedantic -fsanitize=address -o lab7
	./lab7