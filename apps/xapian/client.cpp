#include "getopt.h"
#include "genzipf.h"
#include "tbench_client.h"

#include <unistd.h>

#include <cstring>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <vector>
#include <algorithm>

/*******************************************************************************
 * Class Definitions
 *******************************************************************************/
class TermSet {
    private:
        pthread_mutex_t lock;
        std::vector<std::pair<int, std::string>> terms;
        double skew;
        bool isUniform;
        ZipfSampler zs;
        std::default_random_engine randEngine;
        std::uniform_int_distribution<unsigned long> termGen;
    public:
        TermSet(std::string termsFile, double skew) : skew(skew) {
            pthread_mutex_init(&lock, NULL);
            isUniform = skew == 0 ? true : false;

            std::ifstream fin(termsFile);
            if (fin.fail()) {
                std::cerr << "Error opening terms file " << termsFile << std::endl;
                exit(-1);
            }

            const unsigned MAX_TERM_LEN = 128;
            const unsigned MAX_DIGITS = 24;
            std::string term, freq;
            unsigned long termCount = 0;
            while (true) {
                std::getline(fin, term, ',');
                if (term.size() >= MAX_TERM_LEN) {
                    std::cerr << "Term #" << termCount << " too big" << std::endl;
                    exit(-1);
                }

                std::getline(fin, freq);
                if (freq.size() >= MAX_DIGITS) {
                    std::cerr << "Term #" << termCount << " frequency too big"
                              << std::endl;
                    exit(-1);
                }

                ++termCount;

                terms.push_back(std::make_pair(std::stoi(freq), term));
                if (fin.eof()) break;
            }

            fin.close();
            std::sort(terms.begin(), terms.end(),
                [] (const std::pair<int, std::string>& lhs, const std::pair<int, std::string>& rhs) {
                    return lhs.first > rhs.first;
                });
            zs.setParams((unsigned long)termCount, skew);
            termGen = std::uniform_int_distribution<unsigned long>(0, \
                    termCount - 1);
        }

        ~TermSet() {}

        void acquireLock() { pthread_mutex_lock(&lock); }

        void releaseLock() { pthread_mutex_unlock(&lock); }

        const std::string& getTerm() {
            acquireLock();
            unsigned long idx = isUniform ? termGen(randEngine) : zs.getSample() - 1;
            releaseLock();
            return terms[idx].second;
        }
};

/*******************************************************************************
 * Global Data
 *******************************************************************************/
TermSet* termSet = nullptr;

/*******************************************************************************
 * Liblat API
 *******************************************************************************/
void tBenchClientInit() {
    std::string termsFile = getOpt<std::string>("TBENCH_TERMS_FILE", "terms.in");
    double skew = getOpt<double>("TBENCH_ZIPF_SKEW", 2.0);
    termSet = new TermSet(termsFile, skew);
}

size_t tBenchClientGenReq(void* data) {
    // I could modify the search term distribution here.
    std::string term = termSet->getTerm();
    size_t len = term.size();

    memcpy(data, reinterpret_cast<const void*>(term.c_str()), len + 1);

    return len + 1;
}
