In what follows  ${HEAPMETADATA} names the top level of the HeapMetadata
repository.

The malloc subdirectory of this directory contains our modified globc malloc.

To build and run this malloc you do the following steps.


1.  Make sure you have extracted the glibc git submodule:

```
cd ${HEAPMETADATA}/Variants/glibc; git submodule update --remote
```

2. Build the puppy.

```
mkdir ${HEAPMETADATA}/Variants/glibc-build
cd ${HEAPMETADATA}/Variants/glibc-build
../glibc/configure  --prefix=${HEAPMETADATA}/Variants/glibc-install
# now would be a good time to comment out line 4 in config.h in the
# glibc-build
make
make install
```

Note that we are not going to do a `make install` so the prefix is just
for the build product's sake.

3. Do the mocking bird thing:

```
make update
```
I was hoping to ue a symbolic link here, but that did not seem to work (due
to references to ../.. stuff in the malloc dir).

4. Rebuild the new stuff.

```
make build
```

5. When developing 3. and 4. can be combine by doing:

```
make
```

6. Testing. Once you have built and installed glibc as described in 2.
You need to do ssome additional work to be able to compile the test
suite in 
```
../glibc_tests
```
In particular you need to add some linux headers/libs to sysroot
to get the replay stuff to compile.
```
cd ../../Variants/glibc-install/include
ln -s /usr/include/linux . 
ln -s /usr/include/x86_64-linux-gnu/asm .
ln -s /usr/include/asm-generic .
cd ../lib
ln -s /lib/x86_64-linux-gnu/libgcc_s.so.1 .
```
Once this is done you should be able to compile the tests (located `src/glibc_tests`):
```
cd ../glibc_tests
make
```
You can double check by making sure the libraries are corrent:
```
ldd replay
```

7. As part of the workflow you can run the tests with 
```
make test
```

For guidance:

[testing glibc builds](https://sourceware.org/glibc/wiki/Testing/Builds)
