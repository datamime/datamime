cd memcached-1.6.12
autoreconf -f -i
./configure
make

cd ../mutilate
scons

cd ../mutilate-twtr
scons
