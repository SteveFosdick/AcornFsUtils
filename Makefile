CXX      = g++
CXXFLAGS = -g -Wall
CFLAGS	= -g -Wall

all: afsls afstree test-fs test-find

afsls: afsls.o  acorn-fs.o acorn-adfs.o

afstree: afstree.o acorn-fs.o acorn-adfs.o

test-fs: test-fs.o acorn-fs.o acorn-adfs.o

test-find: test-find.o acorn-fs.o acorn-adfs.o

adfscp: adfscp.o AcornADFS.o AcornFS.o DiskImgIOlinear.o DiskImgIO.o
	$(CXX) -o adfscp adfscp.o AcornADFS.o AcornFS.o DiskImgIOlinear.o DiskImgIO.o
