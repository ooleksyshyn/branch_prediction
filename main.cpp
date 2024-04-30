#include <fstream>
#include <sstream>
#include <iostream>

#include "if_counter.h"

#include "benchmark_tao.h"
#include "cppref_example.h"
#include "lint.h"

int main(int, char**) {

    benchmartTaoJson();

    // benchmarkCpprefExample();

    // benchmartLongIntegerExample();

    IfCounter::instance().printStats();

    return 0;
}
