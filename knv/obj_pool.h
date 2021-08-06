// obj_pool.h
// A general-purpose object pool, muti-thread safe
//
// Shaneyu@tencent.com
//
// 2013-08-28	shaneyu 	Created
// 2013-10-12	shaneyu		Use pointer in set<> instead of object
// 2014-02-04   shaneyu		Use pointer to replace iterator
// 2014-03-15   shaneyu		Remove the use of vector; support multithreading
//
#include <stdint.h>
#include <string.h>

#include "obj_base.h"
extern "C"
{
#include "Attr_API.h"
}

#ifndef __UC_OBJ_POOL_R__
#define __UC_OBJ_POOL_R__

using namespace std;

//
// Cache pool for managing a collection of objects
//
// Restrictions on obj_type:
// 1, obj_type must contain 'curr' and 'next' members of int type.
// 2, obj_type must contain a ReleaseObject() member function, for reclaiming resources
//
template<class obj_type> class ObjPool
{
public:
	ObjPool() : obj_freelist(NULL) { }
	~ObjPool() { while(obj_freelist) { obj_type *next=(obj_type *)obj_freelist->next; delete obj_freelist; obj_freelist = next; } }

	obj_type *New(); // Create an object from pool
	int Delete(obj_type *obj); // Release an object to pool

	obj_type *New(obj_type *&first); // New an object from pool and insert it into a linked-list pointed to by first
	obj_type *NewFront(obj_type *&first); // New an object from pool and insert into the head of linked-list pointed to by first
	int Delete(obj_type *&first, obj_type *obj); // delete an object from a linked-list pointed to by first, and release to pool
	int Detach(obj_type *&first, obj_type *obj); // detach an object from a linked-list pointed to by first, the caller should relase it by calling Delete(obj) when no longer needed
	int DeleteAll(obj_type *&first); // delete the whole linked-list pointed to by first
	int AddToFreeList(obj_type *first);

private:
	volatile obj_type *obj_freelist; // a linked-list for maintaining released objects
};

//
// Implementations
//

#define CAS(mem, oldv, newv) __sync_bool_compare_and_swap(mem, oldv, newv)

template<class obj_type> inline obj_type *ObjPool<obj_type>::New()
{
	obj_type *n;
	volatile obj_type *o = obj_freelist;
	while(o)
	{
		n = (obj_type *)o->next;
		if(CAS(&obj_freelist, o, n))
		{
out:
			// insert into obj list
			o->next = NULL;
			o->prev = (void*)o;
			return (obj_type *)o;
		}
		o = obj_freelist;
	}

	Attr_API(390992, 1);
	try {
		o = new obj_type();
	}catch(...) {
		o = NULL;
	}
	if(o==NULL)
	{
		Attr_API(391020, 1);
		return NULL;
	}
	goto out;
}

template<class obj_type> inline int ObjPool<obj_type>::Delete(obj_type *obj)
{
	obj->ReleaseObject(); // NOTE: ReleaseObject() should not change prev/next pointers

	// put obj to free list
	while(true)
	{
		volatile obj_type *o = obj_freelist;
		obj->next = (void*)o;
		if(CAS(&obj_freelist, o, obj))
			return 0;
	}
	return 0;
}

template<class obj_type> inline obj_type *ObjPool<obj_type>::New(obj_type *&first)
{
	obj_type *n;
	volatile obj_type *o = obj_freelist;
	while(o)
	{
		n = (obj_type *)o->next;
		if(CAS(&obj_freelist, o, n))
		{
out:
			// insert into obj list
			o->next = NULL;
			if(first)
			{
				obj_type *lst = (obj_type *)first->prev; // must not be null
				lst->next = (void*)o;
				o->prev = lst;
				first->prev = (void*)o;
			}
			else
			{
				first = (obj_type *)o;
				o->prev = (void*)o;
			}
			return (obj_type *)o;
		}
		o = obj_freelist;
	}

	Attr_API(390992, 1);
	try {
		o = new obj_type();
	}catch(...) {
		o = NULL;
	}
	if(o==NULL)
	{
		Attr_API(391020, 1);
		return NULL;
	}
	goto out;

}

template<class obj_type> inline obj_type *ObjPool<obj_type>::NewFront(obj_type *&first)
{
	obj_type *n;
	volatile obj_type *o = obj_freelist;
	while(o)
	{
		n = (obj_type *)o->next;
		if(CAS(&obj_freelist, o, n))
		{
out:
			// insert into obj list
			o->next = first;
			if(first)
			{
				o->prev = first->prev;
				first->prev = (void*)o;
			}
			else
			{
				o->prev = (void*)o;
			}
			first = (obj_type *)o;
			return (obj_type *)o;
		}
		o = obj_freelist;
	}

	Attr_API(390992, 1);
	try {
		o = new obj_type();
	}catch(...) {
		o = NULL;
	}
	if(o==NULL)
	{
		Attr_API(391020, 1);
		return NULL;
	}
	goto out;

}



template<class obj_type> inline int ObjPool<obj_type>::Delete(obj_type *&first, obj_type *obj)
{
	if(obj==first)
	{
		if(obj->next) // new first
			((obj_type *)obj->next)->prev = first->prev;
		first = (obj_type *)obj->next;
	}
	else
	{
		obj_type *p = (obj_type *)obj->prev; // must not null
		obj_type *n = (obj_type *)obj->next;
		p->next = n;
		if(n)
			n->prev = p;
		else // obj is the last
			first->prev = p;
	}

	obj->ReleaseObject();

	// put obj to free list
	while(true)
	{
		volatile obj_type *o = obj_freelist;
		obj->next = (void*)o;
		if(CAS(&obj_freelist, o, obj))
			return 0;
	}

	return 0;
}

template<class obj_type> inline int ObjPool<obj_type>::Detach(obj_type *&first, obj_type *obj)
{
	if(obj==first)
	{
		if(obj->next) // new first
			((obj_type *)obj->next)->prev = first->prev;
		first = (obj_type *)obj->next;
	}
	else
	{
		obj_type *p = (obj_type *)obj->prev; // must not null
		obj_type *n = (obj_type *)obj->next;
		p->next = n;
		if(n)
			n->prev = p;
		else // obj is the last
			first->prev = p;
	}

	obj->prev = obj->next = NULL;
	return 0;
}

template<class obj_type> inline int ObjPool<obj_type>::AddToFreeList(obj_type *obj)
{
	// put objs in list to free list
	while(true)
	{
		volatile obj_type *o = obj_freelist;
		((obj_type *)obj->prev)->next = (void*)o;
		if(CAS(&obj_freelist, o, obj))
			return 0;
	}
	return 0;
}

template<class obj_type> inline int ObjPool<obj_type>::DeleteAll(obj_type *&first)
{
	obj_type *o = first;
	while(o)
	{
		o->ReleaseObject(); // NOTE: ReleaseObject() should not change prev/next pointers
		o = (obj_type *)o->next;
	}

	// put objs in list to free list
	AddToFreeList(first);
	first = NULL;
	return 0;
}

#endif
