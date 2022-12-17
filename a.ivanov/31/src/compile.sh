#!/bin/bash
clang++ -Wall -pedantic -fsanitize=address -std=c++2a main.cpp single_thread_proxy.cpp socket_operations.cpp io_operations.cpp -o ../build/proxy
echo "Program build/proxy compiled"

