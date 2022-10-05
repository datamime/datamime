#include <iostream>
#include <atomic>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <fstream>
#include <sstream>
#include <stdlib.h>
#include "server.h"
#include "tbench_server.h"

#include <torch/script.h> // One-stop header.
#include <torch/torch.h>

using namespace std;

const unsigned long numRampUpReqs = 1000L;
const unsigned long numRampDownReqs = 1000L;
unsigned long numReqsToProcess = 1000L;
atomic_ulong numReqsProcessed(0);

inline void usage() {
    cerr << "db_integrated [-m <modulePath>] [-r <numRequests]" << endl;
}

inline void sanityCheckArg(string msg) {
    if (strcmp(optarg, "?") == 0) {
        cerr << msg << endl;
        usage();
        exit(-1);
    }
}

int main(int argc, char* argv[]) {
    string modulePath = "module";

    int c;
    string optString = "m:r:h";
    while ((c = getopt(argc, argv, optString.c_str())) != -1) {
        switch (c) {
            case 'm':
                sanityCheckArg("Missing modulePath");
                modulePath = optarg;
                break;

            case 'r':
                sanityCheckArg("Missing #reqs");
                numReqsToProcess = atol(optarg);
                break;

            case 'h':
                usage();
                exit(0);
                break;

            default:
                cerr << "Unknown option " << c << endl;
                usage();
                exit(-1);
                break;
        }
    }

    cout << "Using " << at::get_num_threads() << "threads" << endl;

    torch::jit::script::Module model;
    try {
        // Deserialize the ScriptModule from a file using torch::jit::load().
        model = torch::jit::load(modulePath);
    }
    catch (const c10::Error& e) {
        std::cerr << "error loading the model\n";
        return -1;
    }
    cout << "Successfully loaded model from " << modulePath << endl;

    tBenchServerInit(1);

    Server::init(numReqsToProcess, 1);
    Server** servers = new Server* [0];
    servers[0] = new Server(model);

    Server::run(servers[0]);

    tBenchServerFinish();

    Server::fini();

    return 0;
}
