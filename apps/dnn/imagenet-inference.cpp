// C++ API to interface with Tailbench query generator
// Heavily modified from examples by PyTorch and

#include <torch/script.h> // One-stop header.
#include <torch/torch.h>

#include <iostream>
#include <memory>
#include <iomanip>
#include <chrono>

#include "imagefolder_dataset.h"

#include <string_view>

template <typename T>
constexpr auto type_name() {
  std::string_view name, prefix, suffix;
#ifdef __clang__
  name = __PRETTY_FUNCTION__;
  prefix = "auto type_name() [T = ";
  suffix = "]";
#elif defined(__GNUC__)
  name = __PRETTY_FUNCTION__;
  prefix = "constexpr auto type_name() [with T = ";
  suffix = "]";
#elif defined(_MSC_VER)
  name = __FUNCSIG__;
  prefix = "auto __cdecl type_name<";
  suffix = ">(void)";
#endif
  name.remove_prefix(prefix.size());
  name.remove_suffix(suffix.size());
  return name;
}

using dataset::ImageFolderDataset;

int main(int argc, const char* argv[]) {
    if (argc != 3) {
        std::cerr << "usage: imagenet-inference <path-to-exported-script-module> <path-to-imagenet-dataset>\n";
        return -1;
    }

    // Device
    auto cuda_available = torch::cuda::is_available();
    torch::Device device(cuda_available ? torch::kCUDA : torch::kCPU);
    std::cout << (cuda_available ? "CUDA available. Using GPU." : "Using CPU.") << '\n';

    const std::string imagenet_path = std::string(argv[2]);

    auto val_dataset = ImageFolderDataset(imagenet_path, ImageFolderDataset::Mode::VAL, {224, 224})
        .map(torch::data::transforms::Normalize<>({0.485, 0.456, 0.406}, {0.229, 0.224, 0.225}))
        .map(torch::data::transforms::Stack<>());

    // Number of samples in the testset
    auto num_val_samples = val_dataset.size().value();

    int batch_size = 256;
    auto val_loader = torch::data::make_data_loader<torch::data::samplers::SequentialSampler>(
        std::move(val_dataset), batch_size);

    //std::shared_ptr<torch::jit::script::Module> model;
    torch::jit::script::Module model;
    try {
        // Deserialize the ScriptModule from a file using torch::jit::load().
        //model = std::make_shared<torch::jit::script::Module>(torch::jit::load(argv[1]));
        model = torch::jit::load(argv[1]);
    }
    catch (const c10::Error& e) {
        std::cerr << "error loading the model\n";
        return -1;
    }

    // Test the model
    model.to(device);
    model.eval();
    torch::InferenceMode no_grad;

    double running_loss = 0.0;
    size_t num_correct_ovr = 0;

    std::chrono::time_point<std::chrono::system_clock> start, end;
	auto it = (*val_loader).begin();
	++it;
	auto datatest = it->data;
	//std::cout << type_name<decltype(it)>() << std::endl;
	//std::cout << type_name<decltype(it->data)>() << std::endl;
	//std::cout << it->data << std::endl;
    for (const auto& batch : *val_loader) {
        std::cout << "Evaluating batch" << std::endl;
        auto data = batch.data.to(device);
        auto target = batch.target.to(device);

        std::vector<torch::jit::IValue> inputs;
        inputs.push_back(data);
        start = std::chrono::system_clock::now();
        auto output = model.forward(inputs).toTensor();
        end = std::chrono::system_clock::now();

        auto loss = torch::nn::functional::cross_entropy(output, target);
        running_loss += loss.item<double>() * data.size(0);

        auto prediction = output.argmax(1);
        size_t num_correct = prediction.eq(target).sum().item<int64_t>();
        std::cout << "Top1: " << float(num_correct) / batch_size << std::endl;
        num_correct_ovr += num_correct;

        std::chrono::duration<double> elapsed_seconds = end - start;
        std::cout << "Elapsed time: " << elapsed_seconds.count() << "s" << std::endl;
    }

    std::cout << "Testing finished!\n";

    auto test_accuracy = static_cast<double>(num_correct_ovr) / num_val_samples;
    auto test_sample_mean_loss = running_loss / num_val_samples;

    std::cout << "Testset - Loss: " << test_sample_mean_loss << ", Accuracy: " << test_accuracy << '\n';
}
