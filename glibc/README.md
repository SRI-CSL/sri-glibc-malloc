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
../glibc/configure  --prefix=/usr
make
```

Note that we are not going to do a `make install` so the prefix is just
for the build product's sake.

3. Do the mocking bird thing:

```
cd ${HEAPMETADATA}/Variants/glibc
rm -rf malloc
cp -r ${HEAPMETADATA}/src/glibc/malloc malloc
```
I was hoping to ue a symbolic link here, but that did not seem to work (due
to references to ../.. stuff in the malloc dir).

4. Rebuild the new stuff.

```
cd ${HEAPMETADATA}/Variants/glibc-build
make
```


This recipe is untested. For guidance:

[testing glibc builds](https://sourceware.org/glibc/wiki/Testing/Builds)
