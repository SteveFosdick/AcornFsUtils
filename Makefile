CXX      = g++
CXXFLAGS = -g -Wall
CFLAGS	= -O2 -Wall

LIB_MODULES = acorn-fs.o acorn-adfs.o acorn-dfs.o

all: afsls afstree afscp afschk

afsls: afsls.o $(LIB_MODULES)

afstree: afstree.o $(LIB_MODULES)

afscp: afscp.o $(LIB_MODULES)

afschk: afschk.o  $(LIB_MODULES)

*.o: acorn-fs.h
