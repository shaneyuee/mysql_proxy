#include "atomic_counter_array.h"

static char* GetShm(int iKey, size_t iSize, int iFlag)
{
	int iShmID;
	char* sShm;
	char sErrMsg[50];
	int iRet;

	if ((iShmID = shmget(iKey, iSize, iFlag)) < 0) {
		sprintf(sErrMsg, "shmget %d %llu", iKey, (uint64_t)iSize);
		perror(sErrMsg);
		return NULL;
	}
	if ((sShm = (char *)shmat(iShmID, NULL ,0)) == (char *) -1) {
		perror("shmat");
		return NULL;
	}
	
	return sShm;
}

static int GetShm3(void **pstShm, int iShmID, size_t iSize, int iFlag)
{
	char* sShm;

	if (!(sShm = GetShm(iShmID, iSize, iFlag & (~IPC_CREAT)))) {
		if (!(iFlag & IPC_CREAT)) return -1;
		if (!(sShm = GetShm(iShmID, iSize, iFlag))) return -1;
		
		*pstShm = sShm;
		return 1;
	}
	*pstShm = sShm;
	return 0;
}

int atomic_counter_array_init(atomic_counter * pstCounter, key_t  shmkey, uint32_t iVal)
{
    int iRet;
    void * ptr;
    if (pstCounter == NULL)
    {
        return -1;
    }
    if ( (iRet = GetShm3(&ptr, shmkey, sizeof(atomic_counter_t), 0666|IPC_CREAT)) < 0)
    {
        return -20;
    }
    pstCounter->ptr = (atomic_counter_t *)ptr;
    if (iRet == 1)  /*新建了共享内存*/
    {
        memset(ptr, 0, sizeof(atomic_counter_t));
        (pstCounter)->ptr->dwCounter[pstCounter->ptr->cIdx] = iVal;
    }
    return 0;
}
int atomic_counter_array_inc_current_slot(atomic_counter * pstCounter)
{
    if (pstCounter == NULL)
    {
        return -1;
    }
    asm volatile( "lock; incl %0" : "+m" (pstCounter->ptr->dwCounter[pstCounter->ptr->cIdx]));
    return 0;
}
extern void __cmpxchg_wrong_size(void);

/*
 * Atomic compare and exchange.  Compare OLD with MEM, if identical,
 * store NEW in MEM.  Return the initial value in MEM.  Success is
 * indicated by comparing RETURN with OLD.
 */
#define cmpxchg(ptr, old, new)            	     		\
({								\
	uint32_t __ret;	        				\
	uint32_t __old = (old);					\
	uint32_t __new = (new);					\
	volatile uint32_t *__ptr = (volatile uint32_t *)(ptr);	\
	asm volatile("lock; cmpxchgl %2,%1"			\
		     : "=a" (__ret), "+m" (*__ptr)		\
		     : "r" (__new), "0" (__old)			\
		     : "memory");				\
	__ret;							\
})


int atomic_counter_array_dec_current_slot(atomic_counter * pstCounter)
{
    uint32_t c, old, dec;
    if (pstCounter == NULL)
    {
        return -1;
    }

    // 这里实际上是dec_if_positive，应该使用cmpxchg保证原子操作--shaneyu
    c = pstCounter->ptr->dwCounter[pstCounter->ptr->cIdx];
    for (;;)
    {
        dec = c - 1;
        if (dec == (uint32_t)-1) // c is zero, return
            break;
        old = cmpxchg(&pstCounter->ptr->dwCounter[pstCounter->ptr->cIdx], c, dec);
        if (old == c) // value is not changed by others, operation successful
            break;
        c = old; // someone is accessing the counter, do it again
    }
    return 0;
}

int atomic_counter_array_set_lastest_slot(atomic_counter * pstCounter, uint32_t val)
{
    if (pstCounter == NULL)
    {
        return -1;
    }
    (pstCounter->ptr->dwCounter[(pstCounter->ptr->cIdx + 4) % 6]) = val;
    return 0;
}

int atomic_counter_array_get_five_slots(atomic_counter * pstCounter, uint32_t *pval)
{
    int i;
    if (pstCounter == NULL || pval == NULL)
    {
        return -1;
    }
    for(i=0; i < 5; ++i)
	    *pval += pstCounter->ptr->dwCounter[(pstCounter->ptr->cIdx + i ) % 6];
    return 0;
}

int atomic_counter_array_get_current_slot(atomic_counter * pstCounter, uint32_t *pval)
{
    if (pstCounter == NULL || pval == NULL)
    {
        return -1;
    }
    *pval = (pstCounter->ptr->dwCounter[pstCounter->ptr->cIdx]) ;
    return 0;
}

int atomic_counter_array_add_current_slot(atomic_counter * pstCounter, uint32_t val)
{
    if (pstCounter == NULL)
    {
        return -1;
    }
    __asm__ __volatile__( "lock ; addl %1,%0" :"+m" (pstCounter->ptr->dwCounter[pstCounter->ptr->cIdx]) :"ir" (val));
    return 0;
}
