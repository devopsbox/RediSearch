#ifndef __SRTREE_H__
#define __SRTREE_H__

#include <stdio.h>
#include <stdlib.h>
#include "types.h"

#include "rmutil/vector.h"
#include "util/heap.h"

#define SR_MAX_LEAF_SIZE 500  

typedef struct docNode {
  t_docId docId;
  float score;
  int size;
  struct docNode *left;
  struct docNode *right;
  
} DocNode;

typedef struct {
  DocNode *current;
  int state;
} doctreeIterState;

typedef struct {
  doctreeIterState *stack;
  size_t top;
  size_t cap;
} DocTreeIterator;
DocTreeIterator *DocTree_Iterate(DocNode *n);
void dti_push(DocTreeIterator*dti, DocNode *n);
DocNode *DocTreeIterator_Next(DocTreeIterator *it);


typedef struct leaf {
  DocNode *doctree;
  float min;
  float max;
  // TODO: make this a set/bloom filter
  int distinct;  // map[float32]struct{}
} Leaf;

typedef struct scoreNode {
  float score;
  struct scoreNode *left;
  struct scoreNode *right;
  Leaf *leaf;
} ScoreNode;

typedef struct {
   heap_t *pq;
   float min;
   float max;
} SortedRangeIterator;




DocNode *newDocNode(t_docId docId, float score);
void DocNode_Add(DocNode *n, t_docId docId, float score);
void DocNode_Split(DocNode *n, float splitPoint, DocNode **left,
                   DocNode **right);




Leaf *newLeaf(DocNode *docs, int size, float min, float max);
void Leaf_Split(Leaf *l, Leaf **left, Leaf **right); 
void Leaf_Add(Leaf *l, t_docId docId, float score) ;




ScoreNode *newScoreNode(Leaf *l);
void ScoreNode_Add(ScoreNode *n, t_docId docId, float score);
Vector *ScoreNode_FindRange(ScoreNode *n, float min, float max); 
static int cmpDocIds(const void *e1, const void *e2, const void *udata);
SortedRangeIterator Iterate(ScoreNode *root, float min, float max); 
DocNode *SortedRangeIterator_Next(SortedRangeIterator *it);

void SortedRangeIterator_Free(SortedRangeIterator it);

#endif