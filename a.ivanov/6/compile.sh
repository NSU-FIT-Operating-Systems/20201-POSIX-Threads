#!/bin/bash
clang -Wall -pedantic -fsanitize=address main.c strings.c thread_safe_list.c linked_list.c -lpthread -o lab23

