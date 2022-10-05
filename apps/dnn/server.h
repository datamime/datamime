#ifndef __SERVER_H
#define __SERVER_H

#include <atomic>
#include <vector>

#include <torch/script.h> // One-stop header.

class Server {
    private:
        static unsigned long numReqsToProcess;
        static volatile std::atomic_ulong numReqsProcessed;

        torch::jit::script::Module model;

        int id;

        void _run();
        void processRequest();

    public:
        Server(torch::jit::script::Module model);
        ~Server();

        static void* run(void* v);
        static void init(unsigned long _numReqsToProcess, unsigned numServers);
        static void fini();
};

#endif
