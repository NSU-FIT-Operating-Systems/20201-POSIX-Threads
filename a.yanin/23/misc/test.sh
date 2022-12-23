#!/usr/bin/env sh

N=1000

delta \
	<(seq $N \
		| awk '{ for (i = 0; i < $1; ++i) { printf "x" }; printf "\n"; }' \
		| build/sleeplist \
		| awk '{ print length(); }') \
       	<(seq $N)
