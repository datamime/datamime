#%%
import torch
print(*torch.__config__.show().split("\n"), sep="\n")
#%%
import time
class Timer(object):
    """A simple timer."""
    def __init__(self):
        self.total_time = 0.
        self.calls = 0
        self.start_time = 0.
        self.diff = 0.
        self.average_time = 0.

    def tic(self):
        # using time.time instead of time.clock because time time.clock
        # does not normalize for multithreading
        self.start_time = time.time()

    def toc(self, average=True):
        self.diff = time.time() - self.start_time
        self.total_time += self.diff
        self.calls += 1
        self.average_time = self.total_time / self.calls
        if average:
            return self.average_time
        else:
            return self.diff

    def clear(self):
        self.total_time = 0.
        self.calls = 0
        self.start_time = 0.
        self.diff = 0.
        self.average_time = 0.

_t = {'mkl': Timer(),
      'mkldnn': Timer()}
#%%

import torch
from torchvision import models
with torch.no_grad():
    net = models.resnet50(True)
    net.eval()
    batch = torch.rand(1, 3,224,224)

    print(torch.get_num_threads())

    _t['mkl'].tic()
    for i in range(10):
        net(batch)
    _t['mkl'].toc()

    from torch.utils import mkldnn as mkldnn_utils
    net = models.resnet50(True)
    net.eval()
    net = mkldnn_utils.to_mkldnn(net)
    batch = torch.rand(1, 3,224,224)
    batch = batch.to_mkldnn()

    _t['mkldnn'].tic()
    for i in range(10):
        net(batch)
    _t['mkldnn'].toc()

print(f"time: {_t['mkl'].average_time}s")
print(f"time: {_t['mkldnn'].average_time}s")
