#include <iostream>
#include <fstream>
#include "genzipf.h"

using namespace std;

int main() {
    auto z = ZipfSampler(100, 4);
    ofstream of("zipf.txt");
    for (int i = 0; i < 100000; i++) {
        of << z.getSample() << endl;
    }
}
