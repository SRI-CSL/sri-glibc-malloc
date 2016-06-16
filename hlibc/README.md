In what follows  ${HEAPMETADATA} names the top level of the HeapMetadata
repository.

The malloc subdirectory of this directory contains our modified glibc `malloc`.

To build and run this malloc you do the following steps.


## 1.  Make sure you have extracted the glibc git submodule:

```
cd ${HEAPMETADATA}/Variants/glibc
git submodule update --init
```
I (qua iam) am currently using `317b199b4aff8cfa27f2302ab404d2bb5032b9a4`.
At some stage we should use a tag.

## 2. Build the puppy.

```
mkdir ${HEAPMETADATA}/Variants/glibc-build
cd ${HEAPMETADATA}/Variants/glibc-build
../glibc/configure  --prefix=${HEAPMETADATA}/Variants/glibc-install
```
Now would be a **very good time** to comment out line 4 in `config.h` in the
`glibc-build`, this will enable us to build our version of malloc with 
*no optimization*.
Note that to test we are going to do a `make install` so the prefix is 
**very important**.
```
make -j
make install
```

## 3. Do the mocking bird thing:

Now in the `${HEAPMETADATA}/src/hlibc` directory do:

```
make init
make update
```
This copies our malloc sources over to the glibc sources in the
`Variants` area.

## 4. Rebuild the new stuff.

Now in the `${HEAPMETADATA}/src/hlibc` directory do:

```
make build
```

## 5. Developing

After doing 3 once you can combine the rest when developing by doing:

```
make
```
in the `${HEAPMETADATA}/src/hlibc` directory.


## 6. Testing. 

Once you have built and installed glibc as described in 2.
You need to do some additional work to be able to compile the test
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
Once this is done you should be able to compile the tests (located `${HEAPMETADATA}/src/glibc_tests`):
```
cd ../glibc_tests
make
```
You can double check by making sure the libraries are correct:
```
ldd replay
```
Try them out with:
```
make run
```

To do the cryptominisat tests in ../../tests/cryptominisat you will
also need to move more libraries into the build directory. 
Ask Ian for details, or look on shaman.


## 7. Testing Workflow

As part of the workflow you can run the tests with 
```
make test
in the `${HEAPMETADATA}/src/glibc` directory.
```


For guidance:

[testing glibc builds](https://sourceware.org/glibc/wiki/Testing/Builds)
