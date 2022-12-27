lab: lab25.c main.c
	gcc lab25.c main.c -lpthread -Wall -Wpedantic -o lab25

original_names: lab25.c main.c
	gcc lab25.c main.c -lpthread -Wall -Wpedantic -DPROPER_NAMES -o lab25

debug: lab25.c main.c
	gcc lab25.c main.c -lpthread -Wall -Wpedantic -DDEBUG -fsanitize=address -o lab25