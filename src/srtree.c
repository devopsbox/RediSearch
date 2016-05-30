#include "srtree.h"


DocTreeIterator *DocTree_Iterate(DocNode *n) {
  DocTreeIterator *ret = malloc(sizeof(DocTreeIterator));
  ret->cap = 8;
  ret->stack = calloc(ret->cap, sizeof(doctreeIterState));
  ret->stack[0].current = n;
  ret->stack[0].state = 0;
  ret->top = 1;
   
  return ret;
}

void dti_push(DocTreeIterator*dti, DocNode *n) {
  if (dti->top == dti->cap) {
    dti->cap *= 2;
    dti->stack = realloc(dti->stack, dti->cap*sizeof(doctreeIterState));
  }
  dti->stack[dti->top].current = n;
  dti->stack[dti->top].state = 0;
  dti->top++;
   
}

void DocNode_Visit(DocNode *n, void (*Callback)(DocNode*, void *), void* ctx) {
  
  if (n->left) {
    DocNode_Visit(n->left, Callback, ctx);
  }
  
  DocNode *right = n->right;
  Callback(n, ctx);
  if (right)
    DocNode_Visit(right, Callback, ctx);
  
}

DocNode *DocTreeIterator_Current(DocTreeIterator *it) {
  if (it->top < 1) {
    return NULL;
  }
  return it->stack[it->top-1].current;
}
DocNode *DocTreeIterator_Next(DocTreeIterator *it) {
   
   if (it->top == 0) return NULL;
   
   doctreeIterState *st = &it->stack[it->top-1];
   //printf("next %p, top %d cap %d\n",it, it->top, it->cap);
   switch (st->state) {
     case 0: {
      st->state++;
      if (st->current->left) {
        dti_push(it, st->current->left);
        return DocTreeIterator_Next(it);
      } 
     }
     case 1:
      st->state++;
      return st->current;
     
     case 2:
      st->state++;
      if (st->current->right) {
        dti_push(it, st->current->right);
        return DocTreeIterator_Next(it);
      }
      
     case 3: 
      // pop
      if (it->top > 0) {
        it->top--;
        return DocTreeIterator_Next(it);
      } 
   }
   
   return NULL;
}

DocNode *newDocNode(t_docId docId, float score) {
  //printf("created new docNode %d, son of %d\n", docId, parent ? parent->docId : -1);
  DocNode *ret = malloc(sizeof(DocNode));
  ret->docId = docId;
  ret->score = score;
  ret->left = NULL;
  ret->size = 1;
  ret->right = NULL;
  
  return ret;
}

void DocNode_Add(DocNode *n, t_docId docId, float score) {
  DocNode *current = n;
  n->size++;
  while (current != NULL) {
    if (docId == current->docId) {
      //printf("%d already in the db", docId);
      return;
    }

    if (docId < current->docId) {
      if (current->left == NULL) {
        
        current->left = newDocNode(docId, score);
        return;
      }
      current = current->left;
    } else {
      if (current->right == NULL) {
        current->right = newDocNode(docId, score);
        return;
      }
      current = current->right;
    }
  }
}


void freeCallback(DocNode *n, void *ctx) {
  free(n);
}
void DocNode_Split(DocNode *n, float splitPoint, DocNode **left,
                   DocNode **right) {
  DocTreeIterator *it = DocTree_Iterate(n);
  DocNode *current = DocTreeIterator_Next(it);
  while (current) {
    
    if (current->score < splitPoint) {
      if (*left == NULL) {
        
        *left = newDocNode(current->docId, current->score);
      } else {
        DocNode_Add(*left, current->docId, current->score);
      }
    } else {
      if (*right == NULL) {
        *right = newDocNode(current->docId, current->score);
      } else {
        DocNode_Add(*right, current->docId, current->score);
      }
    }
    
    
    current = DocTreeIterator_Next(it);
  };
  
  DocNode_Visit(n, freeCallback, NULL);
  free(it->stack);
  free(it);
  
}


Leaf *newLeaf(DocNode *docs, int size, float min, float max) {
  Leaf *ret = malloc(sizeof(Leaf));
  ret->doctree = docs;
  ret->distinct = size;
  ret->min = min;
  ret->max = max;

  return ret;
}

void Leaf_Split(Leaf *l, Leaf **left, Leaf **right) {
  float split = (l->min + l->max) / 2;

  DocNode *ld = NULL, *rd = NULL;
  DocNode_Split(l->doctree, split, &ld, &rd);
  if (ld && rd) { //don't split if only one side was created
    *left = newLeaf(ld, ld ? ld->size : 0, l->min, split);
    *right = newLeaf(rd, rd ? rd->size : 0, split, l->max);
  }
}

void Leaf_Add(Leaf *l, t_docId docId, float score) {
  l->distinct++;
  DocNode_Add(l->doctree, docId, score);

  if (score < l->min) l->min = score;
  if (score > l->max) l->max = score;
}




ScoreNode *newScoreNode(Leaf *l) {
  ScoreNode *ret = malloc(sizeof(ScoreNode));
  ret->leaf = l;
  ret->left = NULL;
  ret->right = NULL;
  ret->score = 0;
  return ret;
}

void ScoreNode_Add(ScoreNode *n, t_docId docId, float score) {
  while (n && !n->leaf) {
    n = score < n->score ? n->left : n->right;
  }
  ////printf("Adding to leaf %f..%f\n", n->leaf->min, n->leaf->max);
  Leaf_Add(n->leaf, docId, score);

  if (n->leaf->distinct > SR_MAX_LEAF_SIZE) {
     //printf("Splitting node with leaf %p\n", n->leaf);

    Leaf *ll = NULL, *rl = NULL;
    Leaf_Split(n->leaf, &ll, &rl);
    
    if (ll && rl) {
      free(n->leaf);
      n->leaf = NULL;
      n->score = ll->max;

      n->right = newScoreNode(rl);
      n->left = newScoreNode(ll);
    } else {
      n->leaf->distinct /= 2;
    }
  }
}


Vector *ScoreNode_FindRange(ScoreNode *n, float min, float max) {
  Vector *leaves = NewVector(Leaf *, 8);

  ScoreNode *vmin = n, *vmax = n;

  while (vmin == vmax && !vmin->leaf) {
    vmin = min < vmin->score ? vmin->left : vmin->right;
    vmax = max < vmax->score ? vmax->left : vmax->right;
  }

  Vector *stack = NewVector(ScoreNode *, 8);

  // put on the stack all right trees of our path to the minimum node
  while (!vmin->leaf) {
    if (vmin->right && min < vmin->score) {
      Vector_Push(stack, vmin->right);
    }
    vmin = min < vmin->score ? vmin->left : vmin->right;
  }
  // put on the stack all left trees of our path to the maximum node
  while (vmax && !vmax->leaf) {
    if (vmax->left && max >= vmax->score) {
      Vector_Push(stack, vmax->left);
    }
    vmax = max < vmax->score ? vmax->left : vmax->right;
  }

  Vector_Push(leaves, vmin->leaf);
  if (vmin != vmax) 
     Vector_Push(leaves, vmax->leaf);

  while (Vector_Size(stack)) {
    ScoreNode *n;
    if (!Vector_Pop(stack, &n)) break;
    if (!n) continue;

    if (n->leaf) Vector_Push(leaves, n->leaf);

    if (n->left) Vector_Push(stack, n->left);
    if (n->right) Vector_Push(stack, n->right);
  }

  Vector_Free(stack);

  //printf("found %d leaves\n", Vector_Size(leaves));
  return leaves;
}

// void SortedRangeIterator_Free(SortedRangeIterator it) {
   
    
//     heap_clear(it.pq);
//     heap_free(it.pq);
// }

// int xxxmain(int argc, char **argv) {
//   ScoreNode *root = newScoreNode(newLeaf(newDocNode(0, 0, NULL), 0, 0, 0));

//   float min = 0, max = 10000;
//   int N = 100000;
//   for (int i = 0; i < N; i++) {
//     ScoreNode_Add(root, rand() % (N*10), (float)(rand() % N*2));
//   }
  

//   int c = 0;
//   int fp = 0;

//   struct timespec start_time, end_time;

  
//   for (int  i =0 ; i < 10; i++) {
//   //clock_gettime(CLOCK_REALTIME, &start_time);
  
//   SortedRangeIterator it = Iterate(root, min, max);
//   DocNode *n;
//   do {
//     n = SortedRangeIterator_Next(&it);
//     if (n) {
//         c++;
//     }
//   } while(n);
  
//   clock_gettime(CLOCK_REALTIME, &end_time);
//   long diffInNanos = end_time.tv_nsec - start_time.tv_nsec;

//   //printf("got %d/%d nodes, Time elapsed: %ldnano\n", c, fp, diffInNanos);
//   // //printf("got %d nodes\n", c);
//   }
  
// }
