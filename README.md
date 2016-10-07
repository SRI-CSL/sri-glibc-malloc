


# Overview

This software is a variant of the malloc subsystem of GNU's Standard C
library (GLibc).  It separates the metadata from the client memory for
increased security.  The software currently targets the x86_64 linux
architecture and has been tested mainly on Ubuntu 14.04.


#  Building

Typing `make` at the toplevel should build an entire installation
of Glibc with our modifications in place. 

This will:

* download the glibc source in `./build/glibc` and checkout XXX
* configure the build in `./build/glibc-build`
* compile and install in `./build/glibc-install`


# Testing 

A very basic test can be done by doing `make check` at the top level.

### Using the testrun.sh script to test applications.

* The simple case. 
```
./build/glibc-build/testrun.sh /bin/echo "Boo!"
```
should run the `/bin/echo` program with argument `"Boo!"` using the
built GLibc, in particular using SRI's malloc. This works as
long as your binary does not require an other library than GLibc.

* The not so simple case.

If your executable relies on other dynamic libraries, then they will
need to be linked/copied into the `./build/glibc-build` area. 

Example:
```
cd ./build/glibc-build

ln -s /lib/x86_64-linux-gnu/libselinux.so.1 .
ln -s /lib/x86_64-linux-gnu/libacl.so.1 .
ln -s /lib/x86_64-linux-gnu/libpcre.so.3 .
ln -s /lib/x86_64-linux-gnu/libattr.so.1 .

cd ../../

./build/glibc-build/testrun.sh /bin/ls
```



### Using the mhooks and replay programs to debug scenarios.

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



Pointers to the files in question?

Maintaining the important glibc invariant

Mmapped memory also has metadata, which we store in the main arena.

Chunks no longer overlap.

Minimum size increased so as to avoid messing with the fenceposts.

Psmalloc?

# Testing Regime

We have tested our prototype on a set of applications that
make heavy use of dynamic-memory allocation. Our primary tests include
the Yices regression tests, benchmarks for Cryptominisat, a
multi-threaded Boolean SAT solver, and the SPEC CPU 2006 integer
benchmark suite.  


# Performance Measurements

We ran the SPEC CPU 2006 integer benchmark suite. In this table we present the 
standard SPEC CPU results format, as well the overhead percentage.
```
dust sri-glibc (8 iterations)
                 Ref        Runtime    Ref Ratio  % Overhead
400.perlbench    9770        387       25.2       15.52 
401.bzip2        9650        458       21.1       1.55 
403.gcc          8050        295       27.3       1.37 
429.mcf          9120        536       17.0       3.47 
445.gobmk       10490        427       24.6       -0.23 
456.hmmer        9330        397       23.5       0.0 
458.sjeng       12100        490       24.7       0.0 
462.libquantum  20720        342       60.7       0.0 
464.h264ref     22130        510       43.4       0.2 
471.omnetpp      6250        547       11.4       42.08 
473.astar        7020        450       15.6       1.81 
483.xalancbmk    6900        364       19.0       12.69 

dust glibc (8 iterations)

400.perlbench    9770        335       29.2       - 
401.bzip2        9650        451       21.4       - 
403.gcc          8050        291       27.7       - 
429.mcf          9120        518       17.6       - 
445.gobmk       10490        428       24.5       - 
456.hmmer        9330        397       23.5       - 
458.sjeng       12100        490       24.7       - 
462.libquantum  20720        342       60.5       - 
464.h264ref     22130        509       43.5       - 
471.omnetpp      6250        385       16.2       - 
473.astar        7020        442       15.9       -
483.xalancbmk    6900        323       21.3       - 

```
Note the these benchmarks are single threaded, and so are not a complete picture.
We would be very interested to hear of some multithreaded benchmarks that we could
include.

# Possible Improvements


# Acknowledgements

This document summarizes the research performed under Darpa Contract
Number N66001-15-C-4061 by SRI International, and presents the
project's results. The project started in August 2015 and was
completed in August 2016. The Principal Investigator for this project
was Drew Dean, until his departure in July 2016. Ian A. Mason took
over as PI after Drew Dean left. The co-investigators were Bruno
Dutertre (SRI) and Dan Wallach (Rice University).
