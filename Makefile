CXX      = g++
CXXFLAGS = -g -Wall
CFLAGS	= -g -Wall

all: afsls afstree afscp afschk test-fs test-find

afsls: afsls.o  acorn-fs.o acorn-adfs.o

afstree: afstree.o acorn-fs.o acorn-adfs.o

afscp: afscp.o acorn-fs.o acorn-adfs.o

afschk: afschk.o  acorn-fs.o acorn-adfs.o

test-fs: test-fs.o acorn-fs.o acorn-adfs.o

test-find: test-find.o acorn-fs.o acorn-adfs.o

acorn-fs.o acorn-adfs.o: acorn-fs.h acorn-fs.c
