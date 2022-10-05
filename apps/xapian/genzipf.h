#ifndef __GENZIPF_H
#define __GENZIPF_H

#include <random>

class ZipfSampler {
	private:
	    std::default_random_engine randEngine;
	    std::uniform_real_distribution<double> dis;
	    unsigned long _N;
	    double _skew;
	    double _t;

        double _bInvCdf(double p);

	public:
        ZipfSampler();
        ZipfSampler(unsigned long N, double skew);
        void setParams(unsigned long N, double skew);
        unsigned long getSample();
};

#endif //__GENZIPF_H
