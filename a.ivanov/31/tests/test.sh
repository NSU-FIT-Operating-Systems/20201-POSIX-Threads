#!/bin/bash
curl -x "http://localhost:5000" www.ccfit.nsu.ru/~rzheutskiy/test_files/100mb.dat --output tests/proxy_100_1
curl -x "http://localhost:5000" www.ccfit.nsu.ru/~rzheutskiy/test_files/100mb.dat --output tests/proxy_100_2
curl -x "http://localhost:5000" www.ccfit.nsu.ru/~rzheutskiy/test_files/100mb.dat --output tests/proxy_100_3


