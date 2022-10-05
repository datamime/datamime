#include "tbench_client.h"

#include <unistd.h>

#include <cstring>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include <torch/script.h> // One-stop header.
#include <torch/torch.h>

#include "imagefolder_dataset.h"
#include "getopt.h"
/*******************************************************************************
 * Class Definitions
 *******************************************************************************/
using dataset::ImageFolderDataset;

class ImagenetClient {
    private:
		torch::data::Iterator<torch::data::Example <>> it;
		torch::data::Iterator<torch::data::Example <>> begin;
		torch::data::Iterator<torch::data::Example <>> end;
		std::string imgBatch;
		std::vector<at::Tensor> imgTargets;

    public:
        ImagenetClient(torch::data::Iterator<torch::data::Example <>> begin,
            torch::data::Iterator<torch::data::Example <>> end)
        : it(begin), begin(begin), end(end) {
		}

        ~ImagenetClient() {}

        const std::string& getBatch() {
			auto imgBatchTensor = (*it).data;
			imgTargets.push_back((*it).target);
			std::stringstream ss;
			torch::save(imgBatchTensor, ss);
			imgBatch = ss.str();
			++it;
			if (it == end)
			    it = begin;
            return imgBatch;
        }
};

/*******************************************************************************
 * Global Data
 *******************************************************************************/
ImagenetClient* ic = nullptr;
std::unique_ptr<torch::data::StatelessDataLoader<torch::data::datasets::MapDataset<torch::data::datasets::MapDataset<dataset::ImageFolderDataset, torch::data::transforms::Normalize<> >, torch::data::transforms::Stack<torch::data::Example<> > >, torch::data::samplers::SequentialSampler>, std::default_delete<torch::data::StatelessDataLoader<torch::data::datasets::MapDataset<torch::data::datasets::MapDataset<dataset::ImageFolderDataset, torch::data::transforms::Normalize<> >, torch::data::transforms::Stack<torch::data::Example<> > >, torch::data::samplers::SequentialSampler> > > val_loader;

/*******************************************************************************
 * Liblat API
 *******************************************************************************/
void tBenchClientInit() {
    std::string imagenet_path = getOpt<std::string>("IMAGENET_PATH", "");
    auto val_dataset = ImageFolderDataset(imagenet_path, ImageFolderDataset::Mode::VAL, {224, 224})
        .map(torch::data::transforms::Normalize<>({0.485, 0.456, 0.406}, {0.229, 0.224, 0.225}))
        .map(torch::data::transforms::Stack<>());

    int batch_size = 1;
    val_loader = torch::data::make_data_loader<torch::data::samplers::SequentialSampler>(
        std::move(val_dataset), batch_size);

    ic = new ImagenetClient(val_loader->begin(), val_loader->end());
}

size_t tBenchClientGenReq(void* data) {
    auto batch = ic->getBatch();
    size_t len = batch.size();
    memcpy(data, reinterpret_cast<const void*>(batch.c_str()), len + 1);
    return len + 1;
}
