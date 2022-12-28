#!/bin/bash
clang -Wall -pedantic -fsanitize=address lab1.c -lpthread -o lab1
clang -Wall -pedantic -fsanitize=address lab2.c -lpthread -o lab2

