


# Overview

This software is a variant of the malloc subsystem of GNU's Standard C
library (GLibc).  It separates the metadata from the client memory for
increased security.  The software currently targets the x86_64 linux
architecture and has been tested mainly on Ubuntu 14.04. It is based
on glibc-2.23.


#  Building

Typing `make` at the toplevel should build an entire installation
of Glibc with our modifications in place. 

This will:

* download the glibc source in `./build/glibc` and `git checkout glibc-2.23`.
* configure the build in `./build/glibc-build`
* compile and install in `./build/glibc-install`


# Testing 

A very basic test can be done by doing `make check` at the top level.

### Using the testrun.sh script to test applications

In the simple case, your executable does not require any dynamic library other than glibc.
You can test our library as follows:
```
./build/glibc-build/testrun.sh /bin/echo "Boo!"
```
This example runs the `/bin/echo` program with argument `"Boo!"` using the
GLibc built in `./build/glibc-build`. In particular, the executable will use SRI's malloc.


If your executable relies on other dynamic libraries than glibc, then either add symbolic
links or copy the libraries into the  `./build/glibc-build` area. 
For example, `/bin/ls` on Ubuntu 14.04 requires four dynamic libraries. To run it with
our malloc implementation:

* link the required libraries into the `./build/glibc-build` directory:
```
cd ./build/glibc-build

ln -s /lib/x86_64-linux-gnu/libselinux.so.1 .
ln -s /lib/x86_64-linux-gnu/libacl.so.1 .
ln -s /lib/x86_64-linux-gnu/libpcre.so.3 .
ln -s /lib/x86_64-linux-gnu/libattr.so.1 .
```
* use the `testrun.sh` script:
```
cd ../../

./build/glibc-build/testrun.sh /bin/ls
```

More information about testing glibc builds can be found [here](https://sourceware.org/glibc/wiki/Testing/Builds).


### Using the mhooks and replay programs for debugging

We have developed another approach to testing and analysis. This technique uses the malloc
hooks to record (using the tool in `src/mhooks`) in a file the pattern 
of allocation of a particular program:
```
MHOOK=/tmp/mhook.out LD_PRELOAD=./mhook.so /bin/ls -la
```
This will produce a log of the allocations/deallocation/reallocation operations, that can be replayed (or analyzed).
To replay it one would (in `src/glibc_test`) do 
```
 ./replay /tmp/mhook.out
```
This will replay the pattern of allocation and return some statistics.
```
...
malloc   0.22  clocks per call
free   0.19  clocks per call
calloc   1.89  clocks per call
realloc  1.00  clocks per call
...
```
The replaying is currently only implemented for single threaded programs,
though in principle it could be extended to multithreaded programs. We have
also included a script `analysis/parse_data` that will summarize the pattern
of allocation in the hook file:
```
>./parse_data /tmp/mhook.out
../src/mhooks/mhook.out contains 405 mallocs
../src/mhooks/mhook.out contains 9 callocs
../src/mhooks/mhook.out contains 3 reallocs
../src/mhooks/mhook.out contains 295 frees
           2 3
           4 6
           8 30
          16 54
          32 230
          64 49
         128 15
         256 11
         512 7
        1024 9
        2048 1
       16384 1
       32768 1
```
The analysis consists of an overview and a log histogram of the allocations 
(3 of size < 2, 6 of size < 4, ...)

### Using gdb ...


# Design Details

We have attempted to make as few changes to the underlying
glibc/ptmalloc/dlmalloc algorithms in order to achieve our
goal.

The metadata for a client pointer is contained in a per arena hash table.
Access to this table is protected by the same lock that protects access
to other aspects of the arena (such as the bins etc). So no additional
synchronization overhead is incurred in accessing a pointer's metadata,
once the pointer's arena has been established.
The per arena hash table is an implementation of Dynamic hashing
by Per-Ake Larson (CACM April 1988 pp 446-457), supported underneath
by a custom pool allocator that relies on mmapped regions.

Determining which arena a client pointer belongs to is done by
a *lock-free* algorithm that keeps track of the underlying
regions that are under our control.


* Things that we could elaborate on: 
  * Pointers to the files in question?
  * Maintaining the important glibc invariant (no adjacent free chunks).
  * Mmapped memory also has metadata, which we store in the main arena.
  * Chunks no longer overlap.
  * Minimum size increased so as to avoid messing with the fenceposts.
  * Memory exhaustion robustness
  

# Testing Regime

We have tested our prototype on a set of applications that
make heavy use of dynamic-memory allocation. Our primary tests include
the Yices regression tests, benchmarks for Cryptominisat, a
multi-threaded Boolean SAT solver, and the SPEC CPU 2006 integer
benchmark suite.  


# Performance Measurements

We ran the SPEC CPU 2006 integer benchmark suite. In this table we present the 
average runtime over 8 iterations, as well the overhead percentage.
```
               sri-glibc   glibc     % Overhead
400.perlbench    387       335       15.52 
401.bzip2        458       451       1.55 
403.gcc          295       291       1.37 
429.mcf          536       518       3.47 
445.gobmk        427       428       -0.23 
456.hmmer        397       397       0.0 
458.sjeng        490       490       0.0 
462.libquantum   342       342       0.0 
464.h264ref      510       509       0.2 
471.omnetpp      547       385       42.08 
473.astar        450       442       1.81 
483.xalancbmk    364       323       12.69 
```
Note the these benchmarks are single threaded, and so are not a complete picture.
Determining that a pointer belongs to the `main_arena` is faster than 
determining that it is either mmapped or belongs to a non main arena.

We would be very interested to hear of some multithreaded benchmarks that we could
include.

# Possible Improvements

* It needs to be fully assimilated into glibc, for example the atomics we use are
not the glibc versions.

* It should be brought upto date with the more recent changes in glibc's malloc.

* The lock free hash table is probably not as polished as it could be.

* The minimun chunksize could drop, provided the fencepost code was rewritten.

* Understanding the omnetpp slow down could be illuminating.

* Multithreaded benchmarking would be nice, and hopefully not too embarrasing.

# Acknowledgements

This document summarizes the research performed under Darpa Contract
Number N66001-15-C-4061 by SRI International, and presents the
project's results. The project started in August 2015 and was
completed in August 2016. The Principal Investigator for this project
was Drew Dean, until his departure in July 2016. Ian A. Mason took
over as PI after Drew Dean left. The co-investigators were Bruno
Dutertre (SRI) and Dan Wallach (Rice University).
