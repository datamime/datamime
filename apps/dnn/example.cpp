#include <sstream>
#include <iostream>

#include <torch/script.h>
#include <torch/torch.h>

const int MAX_REQ_BYTES = 1 << 20; // 1 MB
const int MAX_RESP_BYTES = 1 << 20; // 1 MB

enum ResponseType { RESPONSE, ROI_BEGIN, FINISH };

struct Request {
    uint64_t id;
    uint64_t genNs;
    size_t len;
    char data[MAX_REQ_BYTES];
};

struct Response {
    ResponseType type;
    uint64_t id;
    uint64_t svcNs;
    size_t len;
    char data[MAX_RESP_BYTES];
};

std::string convertToString(char* a, size_t size)
{
    int i;
    std::string s = "";
    for (i = 0; i < size; i++) {
        s = s + a[i];
    }
    return s;
}

int main() {
    Request* req = new Request;

    at::Tensor tensor = torch::ones({1, 1});
    std::stringstream stream;
    torch::save(tensor, stream);

    //std::stringstream stream("This is a test");
    size_t len = stream.str().size();
    printf("%d\n", len);
    memcpy(&req->data, reinterpret_cast<const void*>(stream.str().c_str()), len+1);
    char* buffer = new char[len+1000];
    memcpy(reinterpret_cast<void*>(buffer), reinterpret_cast<void*>(&req->data), len+1);
    buffer[len] = '\0';

    std::stringstream stream2;
    std::string s(buffer, len);
    printf("%d\n", s.size());
    stream2 << s;
    //std::cout << stream2.str() << std::endl;
    at::Tensor tensor2;
    torch::load(tensor2, stream2);
    std::cout << tensor2 << std::endl;
}

