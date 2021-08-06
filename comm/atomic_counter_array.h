#ifndef _ATOMIC_COUNTER_H_INCLUDED_
#define _ATOMIC_COUNTER_H_INCLUDED_
/**
 * 一种原子的计数器?
 */
#include <stdlib.h>
#include <stdio.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <string.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#pragma pack(1)
typedef struct
{
    //key_t semkey; //用信号量互斥访问？ 
   volatile uint32_t dwCounter[6]; 
   volatile unsigned char cIdx;
   unsigned char reserved[43];
} atomic_counter_t;

typedef struct
{
    atomic_counter_t * ptr;
} atomic_counter;
#pragma pack(0)


int atomic_counter_array_init(atomic_counter * ppstCounter, key_t  shmkey, uint32_t iVal);
int atomic_counter_array_inc_current_slot(atomic_counter * pstCounter);
int atomic_counter_array_dec_current_slot(atomic_counter * pstCounter);
int atomic_counter_array_get_current_slot(atomic_counter * pstCounter, uint32_t *pval);
int atomic_counter_array_get_five_slots(atomic_counter * pstCounter, uint32_t *pval);
int atomic_counter_array_set_lastest_slot(atomic_counter * pstCounter, uint32_t val);
int atomic_counter_array_add_current_slot(atomic_counter * pstCounter, uint32_t val);

#ifdef __cplusplus
}
#endif

#endif
