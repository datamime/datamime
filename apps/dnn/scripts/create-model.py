#!/usr/bin/env python
# Adapted from PyTorch Examples (https://github.com/pytorch/examples)
# Heavily modified by Hyun Ryong Lee (hrlee@csail.mit.edu)
import argparse
import os
import sys
import random
import psutil
import time

import torch
import torch.nn as nn
import torch.utils.data
import torchvision.datasets as datasets
import torchvision.transforms as transforms
import torchvision.models as models
from torchsummary import summary

print(torch.__config__.parallel_info())

model_names = sorted(name for name in models.__dict__
    if name.islower() and not name.startswith("__")
    and callable(models.__dict__[name]))

parser = argparse.ArgumentParser(description='PyTorch ImageNet Inference')
parser.add_argument('-d', '--data', metavar='DIR',
                    default='/path/to/imagenet/dataset',
                    help='path to imagenet dataset')
parser.add_argument('-A', '--arch', metavar='ARCH', default='resnet18',
                    choices=model_names,
                    help='model architecture: ' +
                        ' | '.join(model_names) +
                        ' (default: resnet18)')
parser.add_argument('-b', '--batch-size', default=256, type=int,
                    metavar='N',
                    help='mini-batch size (default: 256)')
parser.add_argument('-S', '--summary', action='store_true',
                    help='Print out the model summary and exit.')
parser.add_argument('-r', '--serialize', action='store_true',
                    help='Serialize model and exit.')
parser.add_argument('-g', '--generate-model', action='store_true',
                    help='Generate a model based on input params. Overrides -A.')
parser.add_argument('-p', '--perpetual', action='store_true',
                    help='Run inference over validation data in a perpetual loop')
parser.add_argument('-P', '--model-path', default="",
                    help='path where you wish to save the generated model')
parser.add_argument('-e', '--even-interval', action='store_true',
                    help='place maxpool and strided layers at even intervals.')


# Arguments to be passed by the optimizer to construct the custom model
parser.add_argument('-c', '--conv-layers', type=int, default=1,
                    help='Number of conv blocks')
parser.add_argument('-t', '--strided-conv-layers', type=int, default=0,
                    help='Number of 2-stride conv blocks')
parser.add_argument('-m', '--maxpool-layers', type=int, default=1,
                    help='Number of Max maxpool blocks')
parser.add_argument('-f', '--fc-layers', type=int, default=1,
                    help='Number of fc/linear layers')
parser.add_argument('-o', '--init-channels', type=int, default=64,
                    help='Number of output channels of the first layer')
parser.add_argument('-a', '--activation', default='relu',
                    help='Type of activation function')
parser.add_argument('-s', '--seed', default='ryanlee',
                    help='Seed to initialize network weights with')

class GeneratedModel(nn.Module):

    def __init__(self, conv_layers, strided_conv_layers, maxpool_layers,
                 fc_layers, c_out_init, activation, even_interval):
        super(GeneratedModel, self).__init__()

        # First, determine the overall architecture
        # For now, this just means the order of hidden layers
        self.layer_order = []

        if even_interval:
            # We position strided and maxpool layers at even intervals between the
            # conv layers instead of randomly in the hopes that it makes the
            # dataset space more "smooth"
            mpl_interval = conv_layers / maxpool_layers
            scl_interval = 0
            if  strided_conv_layers > 0:
                scl_interval = conv_layers / strided_conv_layers

            l = 0
            while l < conv_layers:
                self.layer_order.append('c')
                if l % mpl_interval == 0:
                    self.layer_order.append('p')
                if l % scl_interval == 0:
                    self.layer_order.append('t')
                l += 1
        else:
            # Arrange layers randomly. This will induce more variability at
            # the cost of losing dataset "smoothness"
            for c in range(conv_layers):
                self.layer_order.append('c')
            for t in range(strided_conv_layers):
                self.layer_order.append('t')
            for p in range(maxpool_layers):
                self.layer_order.append('p')

            random.shuffle(self.layer_order)

        print("Layer order: ", self.layer_order, "Followed by {} FC layers".format(fc_layers))

        # Given the order, generate the layer objects
        c_in, h_in, w_in = 3, 224, 224 # input dimensions (#channels, height, width)
        c_out = c_out_init # Double this every time the input dimension
                           # is halved by the maxpool layer.
        self.layers = nn.ModuleList()
        self.layers.append(nn.Conv2d(c_in, c_out, 3, padding = 1))
        self.layers.append(nn.ReLU())
        for i, l in enumerate(self.layer_order):
            c_in = c_out
            if l == 'c':
                self.layers.append(nn.Conv2d(c_in, c_out, 3, padding = 1))
                self.layers.append(nn.ReLU())
            elif l == 't':
                self.layers.append(nn.Conv2d(c_in, c_out, 3, stride=2,  padding=1))
                self.layers.append(nn.ReLU())
                h_in = (h_in - 3 + 2*1) // 2 + 1 # output fm = (W - F + 2P) / S + 1
                w_in = (w_in - 3 + 2*1) // 2 + 1
            elif l == 'p':
                self.layers.append(nn.MaxPool2d(2, stride=2, padding=0))
                c_out = c_out * 2
                h_in = h_in // 2
                w_in = w_in // 2
                self.layers.append(nn.Conv2d(c_in, c_out, 3, padding=1))
                self.layers.append(nn.ReLU())

        c_in = c_out
        in_features = int(c_in * h_in * w_in)
        out_features = 2048
        self.fc_init_in_features = in_features
        self.last_layers = nn.ModuleList()
        for i in range(fc_layers):
            if i < fc_layers - 1:
                self.last_layers.append(nn.Linear(in_features, out_features))
                in_features = out_features
                out_features = 2048
                self.last_layers.append(nn.ReLU())
            else: # Last layer
                self.last_layers.append(nn.Linear(in_features, 1000))
                self.last_layers.append(nn.Softmax(dim=1))

    def forward(self, x):
        for i, l in enumerate(self.layers):
            x = l(x)

        x = x.view(-1, self.fc_init_in_features)
        for i, f in enumerate(self.last_layers):
            x = f(x)

        return x


def accuracy(output, target, topk=(1,)):
    """Computes the accuracy over the k top predictions for the specified values of k"""
    with torch.no_grad():
        maxk = max(topk)
        batch_size = target.size(0)

        _, pred = output.topk(maxk, 1, True, True)
        pred = pred.t()
        correct = pred.eq(target.view(1, -1).expand_as(pred))

        res = []
        for k in topk:
            correct_k = correct[:k].reshape(-1).float().sum(0, keepdim=True)
            res.append(correct_k.mul_(100.0 / batch_size))
        return res

def evaluate(model, val_loader, perpetual=False):
    firsttime = True

    ovr_acc1, ovr_acc5 = 0, 0
    batches = 0
    model.eval()
    with torch.inference_mode():
        while firsttime or perpetual :
            firsttime = False
            for i, (images, target) in enumerate(val_loader):
                if i == 0:
                    # By now, all DataLoader threads must have begun, so we
                    # can safely communicate to partthyme that the main thread
                    # is ready to be profiled. If we do this too early,
                    # the DataLoader threads will also be pinned to Core 0
                    # when partthyme pins the main thread!
                    cur_pid = psutil.Process().pid
                    with open("/tmp/dnn-aas.pid", "w") as pid_fh:
                        pid_fh.write(f"{cur_pid}")

                start = time.time()
                output = model(images)
                end = time.time()
                acc1, acc5 = accuracy(output, target, topk=(1, 5))
                print("Top1 = {}, Top5 = {}".format(acc1[0], acc5[0]))
                print("Elapsed time: {}s".format(end - start))
                ovr_acc1 += acc1[0]
                ovr_acc5 += acc5[0]
                batches += 1

    print("Overall Top1 = {}, Top5 = {}".format(ovr_acc1/batches, ovr_acc5/batches))


def main():
    args = parser.parse_args()
    random.seed(args.seed)
    valdir = os.path.join(args.data, 'val')
    normalize = transforms.Normalize(mean=[0.485, 0.456, 0.406],
                                     std=[0.229, 0.224, 0.225])

    # Construct model if not pre-defined
    model = None
    if not args.generate_model:
        model = models.__dict__[args.arch](pretrained=True)
    else:
        model = GeneratedModel(args.conv_layers, args.strided_conv_layers,
                    args.maxpool_layers,
                    args.fc_layers, args.init_channels, args.activation,
                    args.even_interval)

    # Print Keras-style
    if args.summary:
        summary(model, (3, 224, 224))
        sys.exit(0)

    if args.serialize:
        # Script the model
        sm = torch.jit.script(model)
        if args.generate_model:
            sm.save(os.path.join(args.model_path, "custom_model.pt"))
        else:
            sm.save(os.path.join(args.model_path, "{}_model.pt".format(args.arch)))
        sys.exit(0)

    # Load datasets
    val_dataset = datasets.ImageFolder(
        valdir,
        transforms.Compose([
            transforms.Resize(256),
            transforms.CenterCrop(224),
            transforms.ToTensor(),
            normalize,
        ]))

    val_loader = torch.utils.data.DataLoader(
        val_dataset, batch_size=args.batch_size,
        num_workers=4, drop_last=True)

    evaluate(model, val_loader, args.perpetual)
    os.remove("/tmp/dnn-aas.pid")

if __name__ == '__main__':
    main()
