run:
	gcc cpy.c ../common/*.c -I ../common/ -lm -lpthread -Wall -Wpedantic -O3 -o lab7
	./lab7

debugrun:
	clang cpy.c ../common/*.c -I ../common/ -O3 -g -lm -lpthread -Wall -Wpedantic -o lab7
	./lab7

valgrind:
	rm test_cpy/ -R -f
	clang cpy.c ../common/*.c -I ../common/ -O3 -g -lm -lpthread -Wall -Wpedantic -o lab7
	valgrind --tool=memcheck --leak-check=full --log-file="val.txt" --max-threads=5600 ./lab7
	find test_cpy/ | wc -l
	find test/ | wc -l