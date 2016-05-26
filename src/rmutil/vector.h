#ifndef __VECTOR_H__
#define __VECTOR_H__
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

/*
* Generic resizable vector that can be used if you just want to store stuff
* temporarily.
* Works like C++ std::vector with an underlying resizable buffer
*/
typedef struct {
  char *data;
  int elemSize;
  int cap;
  int top;

} Vector;

/* Create a new vector with element size. This should generally be used
 * internall by the NewVector macro */
Vector *__newVectorSize(size_t elemSize, size_t cap);

// Put a pointer in the vector. To be used internall by the library
int __vector_PutPtr(Vector *v, int pos, void *elem);

/*
* Create a new vector for a given type and a given capacity.
* e.g. NewVector(int, 0) - empty vector of ints
*/
#define NewVector(type, cap) __newVectorSize(sizeof(type), cap)

/*
* get the element at index pos. The value is copied in to ptr. If pos is outside
* the vector capacity, we return 0
* otherwise 1
*/
int Vector_Get(Vector *v, int pos, void *ptr);

/* 
* Get the element at the top of the vector (rightmost) and remove it from the vector.
* Notice that this doesn't actually free the element if it's a pointer.
*/
int Vector_Pop(Vector *v, void *ptr);

//#define Vector_Getx(v, pos, ptr) pos < v->cap ? 1 : 0; *ptr =
//*(typeof(ptr))(v->data + v->elemSize*pos)

/*
* Put an element at pos.
* Note: If pos is outside the vector capacity, we resize it accordingly
*/
#define Vector_Put(v, pos, elem) __vector_PutPtr(v, pos, &(typeof(elem)){elem})

/* Push an element at the end of v, resizing it if needed. This macro wraps
 * __vector_PushPtr */
#define Vector_Push(v, elem) __vector_PushPtr(v, &(typeof(elem)){elem})

int __vector_PushPtr(Vector *v, void *elem);

/* resize capacity of v */
int Vector_Resize(Vector *v, int newcap);

/* return the used size of the vector, regardless of capacity */
inline int Vector_Size(Vector *v) { return v->top; }

/* return the actual capacity */
inline int Vector_Cap(Vector *v) { return v->cap; }

/* free the vector and the underlying data. Does not release its elements if
 * they are pointers*/
void Vector_Free(Vector *v);

int __vecotr_PutPtr(Vector *v, int pos, void *elem);

#endif