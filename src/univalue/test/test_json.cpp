// Test program that can be called by the JSON test suite at
// https://github.com/nst/JSONTestSuite.
//
// It reads JSON input from stdin and exits with code 0 if it can be parsed
// successfully. It also pretty prints the parsed JSON value to stdout.

#include <iostream>
#include <string>
#include "univalue.h"

//using namespace std;

int main(int argc, char *argv[]) {
    UniValue val;
    if (val.read(std::string(istreambuf_iterator<char>(cin),
                             istreambuf_iterator<char>()))) {
        std::cout << val.write(1 /* prettyIndent */, 4 /* indentLevel */) << std::endl;
        return 0;
    } else {
        std::cerr << "JSON Parse Error." << std::endl;
        return 1;
    }
}
