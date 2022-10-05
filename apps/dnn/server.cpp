#include <cstring>
#include <iostream>
#include <sstream>

#include <assert.h>
#include <unistd.h>

#include "server.h"
#include "tbench_server.h"

#include <torch/script.h>
#include <torch/torch.h>

using namespace std;

unsigned long Server::numReqsToProcess = 0;
volatile atomic_ulong Server::numReqsProcessed(0);

Server::Server(torch::jit::script::Module model) : model(model) {
    model.eval();
    torch::InferenceMode no_grad;
}

Server::~Server() {
}

void Server::_run() {
    tBenchServerThreadStart();

    while (numReqsProcessed < numReqsToProcess) {
       processRequest();
       ++numReqsProcessed;
    }
}

void Server::processRequest() {
    //const unsigned MAX_DATA_LEN = 1000000;
    //char* data = new char[MAX_DATA_LEN];
    void* dataPtr;

    size_t len = tBenchRecvReq(&dataPtr);
    //memcpy(reinterpret_cast<void*>(data), dataPtr, len);
	string data_as_string = string(reinterpret_cast<char*>(dataPtr), len-1);
    stringstream data_ss;
    data_ss << data_as_string;
    torch::Tensor datainTensor;
    torch::load(datainTensor, data_ss);
    //delete[] data;

    //c10::InferenceMode guard;
    std::vector<torch::jit::IValue> inputs;
    inputs.push_back(datainTensor);
    auto output = model.forward(inputs).toTensor();
    auto prediction= output.argmax(1);

    // Serialize and respond back to client
    std::stringstream rstream;
    torch::save(prediction, rstream);
    std::string rstr = rstream.str();
    unsigned resLen = rstr.length();
    char* res = new char[resLen];
    strncpy(res, rstr.c_str(), resLen);

    tBenchSendResp(reinterpret_cast<void*>(res), resLen);
    delete[] res;
}

void* Server::run(void* v) {
    Server* server = static_cast<Server*> (v);
    server->_run();
    return NULL;
}

void Server::init(unsigned long _numReqsToProcess, unsigned numServers) {
    numReqsToProcess = _numReqsToProcess;
}

void Server::fini() {
}
