export DYNINSTAPI_RT_LIB=/usr/lib/libdyninstAPI_RT.so

g++ -O3 -o test/test.out test/coro.cpp test/test.cpp \
    /usr/lib/gcc/x86_64-linux-gnu/4.7/libgomp.a \
    -pthread -fopenmp \
    -lboost_coroutine -lboost_system -lboost_thread --std=c++11

g++ src/patch.cpp -fno-operator-names -Ijitasm-0.7.1 \
    -ldyninstAPI \
    -lsymtabAPI \
    -linstructionAPI \
    -lpcontrol \
    -lcommon \
    -lparseAPI \
    -lpatchAPI \
    -lstackwalk \
    -ldynDwarf \
    -ldwarf -lelf -ldl -lboost_system -lboost_thread --std=c++11

./a.out
./test/test_patched.out