#!/bin/bash
clang++ -Wall -pedantic -std=c++2a tests.cpp proxy_tests.cpp -lpthread -lgtest -lcurl -o tests
echo "Program tests compiled"

