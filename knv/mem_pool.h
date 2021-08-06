// mem_pool.h
// A memory buffer pool based on ObjPool
//
// Shaneyu@tencent.com
//
// 2014-1-15	shaneyu 	Created
//

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <map>
#include <string>

#include "obj_pool.h"

#ifndef __UC_MEM_POOL__
#define __UC_MEM_POOL__

using namespace std;

class UcMemPool;
class UcMemManager;

class UcMem : public ObjBase
{
public:
	UcMem() : ObjBase(), alloc_sz(0), mem(NULL), pool(NULL)
	{
	}
	UcMem(unsigned long sz) : ObjBase(), pool(NULL)
	{
		mem = malloc(sz);
		if(mem)
		{
			alloc_sz = sz;
		}
		else
		{
			alloc_sz = 0;
		}
	}

	virtual ~UcMem()
	{
		if(mem) free(mem);
	}

	virtual void ReleaseObject()
	{
		// nothing to be released
	}

	operator void *() { return mem; }
	void *ptr() { return mem; }

	unsigned long GetAllocSize();

private:
	unsigned long alloc_sz;
	void *mem;

	friend class UcMemPool;
	friend class UcMemManager;
	UcMemPool *pool;
};

//
// the mem pool
//
class UcMemPool
{
public:
	UcMemPool() : sz_each(0), sz_total(0), sz_free(0), sz_max(0), pool(){ }
	~UcMemPool() {}

	UcMem *Alloc(bool &exceed_limit);
	void Free(UcMem *m);

	unsigned long Shrink(); // shrink memory for other pool, return the shrinked size
	unsigned long GetMemSize() { return sz_each; }
private:
	volatile unsigned long sz_each;  // size of each mem
	volatile unsigned long sz_total; // current total size
	volatile unsigned long sz_free;  // current free size
	volatile unsigned long sz_max;   // max total size
	ObjPool<UcMem> pool;

friend class UcMemManager;
};

inline unsigned long UcMem::GetAllocSize()
{
	return pool? pool->GetMemSize() : alloc_sz;
}

class UcMemManager
{
public:
	static UcMem *Alloc(unsigned long sz);
	static void Free(UcMem *m);

	static void SetMaxSize(unsigned long sz) { GetInstance()->sz_max = sz; }

private:
	static UcMemManager *GetInstance();
	UcMemManager(unsigned long max_sz):sz_max(max_sz) {}
	UcMemPool *GetPool(int magic);
	unsigned long Shrink(int magic);

private:
	unsigned long sz_max;
};


#endif
