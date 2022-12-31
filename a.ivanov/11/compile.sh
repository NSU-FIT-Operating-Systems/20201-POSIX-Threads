#!/bin/bash
clang -Wall -pedantic -fsanitize=address main.c -lpthread -o lab11

