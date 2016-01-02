Our Code
========


-  `dsmalloc` our version of dnmalloc using our metatdata implementation
  
-  `linhash`  our version of the linear hash implementation of Per-Ake Larson 1988 CACM paper.

-  `lphash`  a stand alone amalgamation of linhash with a separate namespace. Used in the replay
implementations.

-  `metadata` our metadata implementation (used in dsmalloc and ...)

-  `mhooks`   for hooking and recording malloc calls in an binary application.

-  `replay`   for replaying the data obtained by using mhook (using the system malloc)

-  `dsreplay` for replaying the data obtained by using mhook (using a statically linked dsmalloc)

-  `ptmalloc2` something to compare `psmalloc` to.

-  `psmalloc2` our transformed version of `ptmalloc`.

-  `glibc` our glibc (head of repo) version.



