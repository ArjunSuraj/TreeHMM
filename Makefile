# from https://jayrambhia.wordpress.com/2012/06/25/configuring-boostpython-and-hello-boost/
# note: IST's boost 1.70.0 has only support for python 2.7,
#  so `module load python/2.7.13-gpu`
#PYTHON_VERSION = 2.7
#PYTHON_INCLUDE = /usr/include/python$(PYTHON_VERSION)
PYTHON_VERSION = 3.9
L_PYTHON_VERSION = 3.8
PYTHON_INCLUDE = /usr/local/packages/apps/conda/include/python$(PYTHON_VERSION)
#PYTHON_LIB_CONFIG = /usr/lib/python$(PYTHON_VERSION)/config
#PYTHON_LIB_CONFIG = /usr/lib/python$(PYTHON_VERSION)/config-x86_64-linux-gnu/
PYTHON_LIB_CONFIG = /usr/local/packages/apps/conda/lib/python$(PYTHON_VERSION)/config-$(PYTHON_VERSION)-x86_64-linux-gnu/
#PYTHON_LIB_CONFIG = /mnt/nfs/clustersw/Debian/stretch/python/3.6.9/lib/
 
# location of the Boost Python include files and library
 
# default BOOST on IST cluster in 1.62.0, only >1.63.0 has numpy support
# with `module load boost`, IST provides version 1.70.0
# use `module display boost` to find these include and library paths
#BOOST_INC = /usr/include
#BOOST_INC = /mnt/nfs/clustersw/Debian/stretch/boost/1.70.0/include/
BOOST_INC = /data/acp20asl/.conda-sharc/pytorch/include
#BOOST_LIB = /usr/lib
#BOOST_LIB = /mnt/nfs/clustersw/Debian/stretch/boost/1.70.0/lib/
BOOST_LIB = /data/acp20asl/.conda-sharc/pytorch/lib

# IMP: `export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/mnt/nfs/clustersw/Debian/stretch/boost/1.70.0/lib/` in ~/.bashrc
 
# compile mesh classes
TARGET = EMBasins
 
$(TARGET).so: $(TARGET).o
	g++ -shared -Wl,--export-dynamic $(TARGET).o BasinModel.o TreeBasin.o -L$(BOOST_LIB) -lgsl -lgslcblas -lboost_python38 -lboost_numpy38  -L$(PYTHON_LIB_CONFIG) -lpython$(L_PYTHON_VERSION) -o $(TARGET).so
 
$(TARGET).o: $(TARGET).cpp
	g++ -std=c++11 -lrt -c -g -I/data/acp20asl/.conda-sharc/pytorch/include -fPIC -c BasinModel.cpp
	g++ -std=c++11 -lrt -c -g -I/data/acp20asl/.conda-sharc/pytorch/include -fPIC -c TreeBasin.cpp
	g++ -std=c++11 -lrt -c -g -I$(PYTHON_INCLUDE) -I$(BOOST_INC) -fPIC -c $(TARGET).cpp
