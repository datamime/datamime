/*
* Using derivations from:
* https://jasoncrease.medium.com/rejection-sampling-the-zipf-distribution-6b359792cffa
* The basic idea is to use rejection sampling, which asserts that given a PDF f(x)
* that we want to sample from (but cannot do so directly),
* we need a bounding PDF b(x) such that f(x) <= a*b(x) for all x for some constant a.
* Then, if we sample with b(x) and accept the sample with probability f(x) / a*b(X)
* (by choosing a sample from uniform distribution U ~ (0, 1) and seeing if it is <= to the above),
* the resulting accepted samples form the PDF f(x).
*
* In our case, we choose a b(x) that has a tight bound for efficiency. Since
* we cannot directly sample from b(x), we sample from b(x) using inverse
* transform sampling (I suggest searching the internet for why using this technique
* directly to sample f(x) is harder than it seems).
*
* I hand-calculated to verify the bounding function b(x) and the value of t which
* makes the integral of b(x) equal 1 (i.e., make it a proper PDF), and then
* calculated the inverse invb(u) as well.
*/

#include <cmath>
#include <cassert>
#include "genzipf.h"

ZipfSampler::ZipfSampler()
    : dis(0.0, 1.0), _N(100), _skew(2) {
    _t   = (pow(_N, 1 - _skew) - _skew) / (1 - _skew);
}

ZipfSampler::ZipfSampler(unsigned long N, double skew)
    : dis(0.0, 1.0), _N(N), _skew(skew) {
    _t   = (pow(N, 1 - skew) - skew) / (1 - skew);
}

void ZipfSampler::setParams(unsigned long N, double skew) {
    _N = N;
    _skew = skew;
    _t   = (pow(N, 1 - skew) - skew) / (1 - skew);
}

unsigned long ZipfSampler::getSample() {
    while (true) {
        double xRand = dis(randEngine);
        double invB = _bInvCdf(xRand);
        assert(invB > 0);
        unsigned long sampleX = (unsigned long)(invB + 1); // Round up to an integer since zipfian is a discrete distribution.
        double yRand = dis(randEngine);
        double ratioTop = pow(sampleX, -_skew); // f(x) = x^(-s), which in this case equals zipf * harmonic constant
        double ratioBottom = sampleX <= 1 ? 1  / _t : pow(invB, -_skew)  / _t; // t * b(x)
        double rat = (ratioTop) / (ratioBottom * _t);

        if (yRand < rat)
            return sampleX;
    }
}

double ZipfSampler::_bInvCdf(double p) {
    if (p * _t <= 1)
        return p * _t;
    else
        return pow((p * _t) * (1 - _skew) + _skew, 1 / (1 - _skew) );
}

