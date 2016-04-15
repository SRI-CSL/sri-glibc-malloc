#ifndef __MAGED_INTERNALS_H__
#define __MAGED_INTERNALS_H__

#include <signal.h>
#include <sys/mman.h>
#include <stdio.h>
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <stdint.h>

#include "atomic.h"
#include "queue.h"

struct Descriptor;
typedef struct Descriptor descriptor;
struct Procheap;
typedef struct Procheap procheap;

#define TYPE_SIZE	8
#define PTR_SIZE	sizeof(void*)
#define HEADER_SIZE	(TYPE_SIZE + PTR_SIZE)

#define LARGE		0
#define SMALL		1

#define	PAGESIZE	4096
#define SBSIZE		(16 * PAGESIZE)
#define DESCSBSIZE	(1024 * sizeof(descriptor))
#define SB_ALIGNMENT    SBSIZE

#define ACTIVE		0
#define FULL		1
#define PARTIAL		2
#define EMPTY		3

#define	MAXCREDITS	64    // 2^(bits for credits in active)
#define GRANULARITY	16    // sri: for x86_64 alignment we require 16 NOT 8

// sri: bigger than this we mmap. code is not yet 
// parametric in this. change it and the code needs
// changing too.
#define MAX_BLOCK_SIZE  2048  
                              

/* We need to squeeze this in 64-bits, but conceptually
 * this is the case:
 *	descriptor* DescAvail;
 */
typedef struct {
  uintptr_t 	DescAvail;
  uint64_t      tag;
} descriptor_queue;

/* Superblock descriptor structure. We bumped avail and count 
 * to 24 bits to support larger superblock sizes. */
typedef struct {
	unsigned long long 	avail:24,count:24, state:2, tag:14;
} anchor;

struct Descriptor {
	struct queue_elem_t	lf_fifo_queue_padding;
	volatile anchor		Anchor;
	descriptor*		Next;
	void*			sb;		// pointer to superblock
	procheap*		heap;		// pointer to owner procheap
	unsigned int		sz;		// block size
	unsigned int		maxcount;	// superblock size / sz
};

typedef struct {
	lf_fifo_queue_t		Partial;	// initially empty
	unsigned int		sz;		// block size
	unsigned int		sbsize;		// superblock size
} sizeclass;

typedef struct {
  uintptr_t	ptr;
  uint64_t      credits;
} active;

struct Procheap {
	volatile active		Active;		// initially NULL
	volatile descriptor*	Partial;	// initially NULL
	sizeclass*		sc;		// pointer to parent sizeclass
};

static sizeclass _sizeclasses[MAX_BLOCK_SIZE / GRANULARITY];

static inline void init_sizeclasses(void){
  int i;
  const int length = MAX_BLOCK_SIZE / GRANULARITY;
  for(i = 0; i < length; i++){ 
    //_sizeclasses[i].Partial = ??
    _sizeclasses[i].sz = GRANULARITY * (i + 1);
    _sizeclasses[i].sbsize = SBSIZE;
  }
}

/* This is large and annoying, but it saves us from needing an 
 * initialization routine. */
static sizeclass sizeclasses[MAX_BLOCK_SIZE / GRANULARITY] =
  {
    {LF_FIFO_QUEUE_STATIC_INIT, 16, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 32, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 48, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 64, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 80, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 96, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 112, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 128, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 144, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 160, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 176, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 192, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 208, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 224, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 240, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 256, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 272, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 288, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 304, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 320, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 336, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 352, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 368, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 384, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 400, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 416, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 432, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 448, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 464, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 480, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 496, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 512, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 528, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 544, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 560, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 576, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 592, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 608, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 624, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 640, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 656, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 672, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 688, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 704, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 720, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 736, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 752, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 768, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 784, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 800, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 816, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 832, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 848, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 864, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 880, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 896, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 912, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 928, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 944, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 960, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 976, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 992, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 1008, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 1024, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 1040, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 1056, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 1072, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 1088, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 1104, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 1120, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 1136, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 1152, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 1168, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 1184, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 1200, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 1216, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 1232, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 1248, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 1264, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 1280, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 1296, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 1312, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 1328, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 1344, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 1360, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 1376, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 1392, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 1408, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 1424, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 1440, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 1456, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 1472, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 1488, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 1504, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 1520, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 1536, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 1552, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 1568, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 1584, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 1600, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 1616, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 1632, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 1648, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 1664, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 1680, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 1696, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 1712, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 1728, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 1744, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 1760, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 1776, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 1792, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 1808, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 1824, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 1840, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 1856, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 1872, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 1888, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 1904, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 1920, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 1936, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 1952, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 1968, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 1984, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 2000, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 2016, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, 2032, SBSIZE},
    {LF_FIFO_QUEUE_STATIC_INIT, MAX_BLOCK_SIZE, SBSIZE},
  };

static inline long min(long a, long b)
{
  return a < b ? a : b;
}

static inline long max(long a, long b)
{
  return a > b ? a : b;
}

#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif

#endif	/* __MAGED_INTERNALS_H__ */
