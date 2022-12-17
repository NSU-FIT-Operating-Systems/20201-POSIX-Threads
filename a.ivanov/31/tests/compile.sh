#!/bin/bash
clang++ -Wall -pedantic -fsanitize=address -std=c++2a tests.cpp proxy_tests.cpp -lgtest -lcurl -o ../build/tests
echo "Program build/tests compiled"

