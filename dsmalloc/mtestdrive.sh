#!/bin/sh

#iam says: all the export LD_LIBRARY_PATH makes me think this ain't gonna work on a mac.

run_mtest1(){
    (
	export LD_LIBRARY_PATH="."
	# echo "./t-test1 $1 $2 $3 $4 $5"
	./mt-test1 $1 $2 $3 $4 $5 >/dev/null
	if [ $? -ne 0 ]
	then
	    echo "FAIL   : ./mt-test1 $1 $2 $3 $4 $5"
	    exit $?
        fi
	echo "SUCCESS: ./mt-test1 $1 $2 $3 $4 $5"
    )
    return $?
}

run_test2(){
    (
	export LD_LIBRARY_PATH="."
	# echo "./t-test1 $1 $2 $3 $4 $5"
	./t-test2 $1 $2 $3 $4 $5 >/dev/null
	if [ $? -ne 0 ]
	then
	    echo "FAIL   : ./t-test2 $1 $2 $3 $4 $5"
	    exit $?
        fi
	echo "SUCCESS: ./t-test2 $1 $2 $3 $4 $5"
    )
    return $?
}

run_test3(){
    (
	export LD_LIBRARY_PATH="."
	# echo "./t-test1 $1 $2 $3 $4 $5"
	./t-test3 $1 2>/dev/null
	if [ $? -eq 0 ]
	then
	    echo "FAIL   : ./t-test3 $1"
	    exit 1
        fi
	echo "SUCCESS: ./t-test3 $1"
	exit 0
    )
    return $?
}

run_test3a(){
    (
	export LD_LIBRARY_PATH="."
	# echo "./t-test1 $1 $2 $3 $4 $5"
	./t-test3 $1 2>/dev/null
	if [ $? -ne 3 ]
	then
	    echo "FAIL   : ./t-test3 $1"
	    exit 1
        fi
	echo "SUCCESS: ./t-test3 $1"
	exit 0
    )
    return $?
}

run_mtest1

if [ $? -eq 0 ]
then
    run_mtest1 30 12 3000 150000 400
fi

if [ $? -eq 0 ]
then
    run_mtest1 1 1 4000 10000000 1000
fi

