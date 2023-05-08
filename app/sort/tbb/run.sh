#!/bin/bash

g++-13 tbb.cpp -O3 -std=c++23 -march=native -ltbb -lpthread -o tbb
./tbb
