#!/bin/bash
#clang -Wall -pedantic -fsanitize=address server.c socket_operations.c io_operations.c -o build/server
#echo "Program server compiled successfully"
#clang -Wall -pedantic -fsanitize=address client.c socket_operations.c io_operations.c socks_messages.c -o build/client
#echo "Program client compiled successfully"
clang++ -Wall -pedantic -fsanitize=address -std=c++2a main.cpp single_thread_proxy.cpp socket_operations.cpp io_operations.cpp -o build/proxy
echo "Program proxy compiled successfully"

