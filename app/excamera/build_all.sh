cd alfalfa

./autogen.sh
./configure
make -j $(nproc)
sudo make install

cd ../bin
./prepare.sh

cd ../samples
./prepare.sh
