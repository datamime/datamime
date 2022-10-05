#include "bench.h"
#include "../macros.h"
#include "request.h"
#include "tbench_client.h"
#include "../util.h"
#include "getopt.h"

#include <cstring>
#include <cassert>

/*******************************************************************************
 * Class Definitions
 *******************************************************************************/
class Client {
    private:
        struct WorkloadDesc {
            ReqType type;
            double frequency;
        };

        static unsigned g_txn_workload_mix[5];
        static unsigned long seed;
        static Client* singleton;

        std::vector<WorkloadDesc> workload;
        util::fast_random randgen;

        Client() : randgen(seed)
        {
            fprintf(stderr, "Workload frequencies: %f,%f,%f,%f,%f\n",
                static_cast<double>(g_txn_workload_mix[0]),
                static_cast<double>(g_txn_workload_mix[1]),
                static_cast<double>(g_txn_workload_mix[2]),
                static_cast<double>(g_txn_workload_mix[3]),
                static_cast<double>(g_txn_workload_mix[4]));
            for (size_t i = 0; i < ARRAY_NELEMS(g_txn_workload_mix); ++i) {
                WorkloadDesc w = { .type = static_cast<ReqType>(i),
                    .frequency = static_cast<double>(g_txn_workload_mix[i]) / 100.0 };
                workload.push_back(w);
            }
        }

    public:
        static void init( uint32_t fq_neworder,
                    uint32_t fq_payment,
                    uint32_t fq_delivery,
                    uint32_t fq_orderstatus,
                    uint32_t fq_stocklevel) {
            g_txn_workload_mix[0] = fq_neworder;
            g_txn_workload_mix[1] = fq_payment;
            g_txn_workload_mix[2] = fq_delivery;
            g_txn_workload_mix[3] = fq_orderstatus;
            g_txn_workload_mix[4] = fq_stocklevel;
            singleton = new Client();
        }

        static Client* getSingleton() { return singleton; }

        Request getReq() {
            Request req;

            double d = randgen.next_uniform();
            for (size_t i = 0; i < workload.size(); ++i) {
                if (((i + 1) == workload.size()) ||
                        (d < workload[i].frequency)) {
                    req.type = static_cast<ReqType>(i);
                    break;
                }

                d -= workload[i].frequency;
            }

            return req;
        }
};

/*******************************************************************************
 * Global State
 *******************************************************************************/
unsigned long Client::seed = 23984543;
unsigned Client::g_txn_workload_mix[] = {45, 43, 4, 4, 4}; // Default TPC-C values
Client* Client::singleton = nullptr;

/*******************************************************************************
 * API
 *******************************************************************************/
void tBenchClientInit() {

    std::string wltype = getOpt<std::string>("WORKLOAD", "");
    uint32_t fq_neworder, fq_payment, fq_delivery, fq_orderstatus, fq_stocklevel;
    printf("WORKLOAD=%s\n", wltype.c_str());
    if (wltype == "tpcc") {
        fprintf(stderr, "Executing TPCC workload\n");
        fq_neworder = getOpt<uint32_t>("FQ_NEWORDER", 45);
        fq_payment = getOpt<uint32_t>("FQ_PAYMENT", 43);
        fq_delivery = getOpt<uint32_t>("FQ_DELIVERY", 4);
        fq_orderstatus = getOpt<uint32_t>("FQ_ORDERSTATUS", 4);
        fq_stocklevel = getOpt<uint32_t>("FQ_STOCKLEVEL", 4);

        assert(fq_neworder + fq_payment + fq_delivery + fq_orderstatus + fq_stocklevel == 100);

        Client::init(fq_neworder,
            fq_payment,
            fq_delivery,
            fq_orderstatus,
            fq_stocklevel);
    } else if (wltype == "bid") {
        // Equivalent to 100% frequency in first txn since there is only one txn.
        fprintf(stderr, "Executing BID workload\n");
        Client::init(100, 0, 0, 0, 0);
    }
}

size_t tBenchClientGenReq(void* data) {
    Request req = Client::getSingleton()->getReq();
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
    memcpy(data, reinterpret_cast<const void*>(&req), sizeof(req));
#pragma GCC diagnostic pop
    return sizeof(req);
}
