#!/usr/bin/env python

import numpy as np
from scipy.special import zeta
from scipy.stats import zipf
import matplotlib.pyplot as plt

zipf_samples = []
with open("zipf.txt", 'r') as zf:
    for line in zf:
        zipf_samples.append(int(line))

zipf_samples = np.asarray(zipf_samples)
a = 4
n = 100000
count = np.bincount(zipf_samples)
k = np.arange(1, zipf_samples.max() + 1)
fig, ax = plt.subplots(1,1)
ax.bar(k, count[1:], alpha=0.5, label='sample count')
x = np.arange(zipf.ppf(0.01, a),
              zipf.ppf(0.9999999, a))
ax.plot(x, n * zipf.pmf(x, a), label='zipf pmf')
plt.semilogy()
plt.grid(alpha=0.4)
plt.legend()
plt.title(f'Zipf sample, a={a}, size={n}')
plt.show()
plt.savefig("zipf.pdf");
