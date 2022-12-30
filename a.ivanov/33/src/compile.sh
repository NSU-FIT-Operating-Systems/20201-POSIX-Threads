#!/bin/bash
clang++ -Wall -pedantic -fsanitize=address -std=c++2a main.cpp proxy_main.cpp utils/socket_operations.cpp utils/io_operations.cpp utils/log.cpp proxy_worker/proxy_worker.cpp -lpthread -o ../build/proxy
echo "Program build/proxy compiled"

