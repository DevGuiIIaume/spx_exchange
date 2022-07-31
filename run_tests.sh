#!/bin/bash

# Trigger all the test cases with this script
echo "##########################"
echo "### Running e2e tests! ###"
echo "##########################"
echo ""

# Compile C program
make

# Initialise counting variable
count=0
SUB='trader'

# Assume all ".in" and ".out" files are located in a separate `tests/E2E/*/` directory
for folder in `ls -d tests/E2E/*/ | sort -V`; do

    name=$(basename "$folder")

    echo Running $name.

    expected_file=tests/E2E/$name/*.out

    if [[ "$name" == *"$SUB"* ]]; then
        ./spx_exchange products.txt ./spx_test_trader ./spx_trader $folder/test.in | diff - $expected_file || echo "Test $name: failed!"
    else
        ./spx_exchange products.txt ./spx_test_trader ./spx_test_trader $folder/test.in | diff - $expected_file || echo "Test $name: failed!"
    fi

    count=$((count+1))

done

echo ""
echo "Finished running $count E2E tests!"
echo ""

# Run unit-tests
gcc -Wall -Werror -Wvla -O0 -std=c11 -g -D TESTING -D UNIT_TEST -c spx_exchange.c -o tests/spx_exchange.o
gcc -Wall -Werror -Wvla -O0 -std=c11 -g -D TESTING  -lm -c tests/unit-tests.c -o tests/unit-tests.o
gcc tests/unit-tests.o tests/spx_exchange.o tests/libcmocka-static.a -lm -o tests/unit-tests
./tests/unit-tests