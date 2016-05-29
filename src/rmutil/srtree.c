#include "../types.h"
#include "vector.h"
#include <stdio.h>
#include <stdlib.h>
#include "../util/heap.h"
#include <time.h>

#define SR_MAX_LEAF_SIZE 1000  

typedef enum {
  StateNone,
  StateLeft,
  StateSelf,
  StateRight,
} IteratorState;


typedef struct docNode {
  t_docId docId;
  float score;
  int size;
  struct docNode *left;
  struct docNode *right;
  struct docNode *parent;
  IteratorState state;
} DocNode;


DocNode *DocNode_Next(DocNode *i);



DocNode *DocNode_Iterate(DocNode *n) {
  return DocNode_Next(n);
}

DocNode *DocNode_Next(DocNode *n) {
  // fmt.Printf("next %p: state %v, docId %d left %p right %p parent %p\n",
  // i.node, i.state, i.node.docId, i.node.left, i.node.right, parent)
  if (n->state == StateNone) {
    while (n->left) {
      n->state = StateLeft;
      n = n->left;
    }
  }

  if (n->state == StateLeft ||
      (n->state == StateNone && n->left == NULL)) {
    n->state = StateSelf;
    return n;
  }

  if (n->state == StateSelf) {
    n->state = StateRight;
    if (n->right) {
      return DocNode_Next(n->right);
    }
  }
  
  n->state = StateNone;
  return n->parent;
}

DocNode *newDocNode(t_docId docId, float score, DocNode *parent) {
  DocNode *ret = malloc(sizeof(DocNode));
  ret->docId = docId;
  ret->score = score;
  ret->left = NULL;
  ret->size = 1;
  ret->right = NULL;
  ret->parent = parent;
  ret->state = StateNone;
  return ret;
}

void DocNode_Add(DocNode *n, t_docId docId, float score) {
  DocNode *current = n;
  n->size++;
  while (current != NULL) {
    if (docId == current->docId) {
      return;
    }

    if (docId < current->docId) {
      if (current->left == NULL) {
        current->left = newDocNode(docId, score, current);
        return;
      }
      current = current->left;
    } else {
      if (current->right == NULL) {
        current->right = newDocNode(docId, score, current);
        return;
      }
      current = current->right;
    }
  }
}

void DocNode_Split(DocNode *n, float splitPoint, DocNode **left,
                   DocNode **right) {
  DocNode *current = DocNode_Iterate(n);
  while (current) {
    
    if (current->score < splitPoint) {
      if (*left == NULL) {
        *left = newDocNode(current->docId, current->score, NULL);
      } else {
        DocNode_Add(*left, current->docId, current->score);
      }
    } else {
      if (*right == NULL) {
        *right = newDocNode(current->docId, current->score, NULL);
      } else {
        DocNode_Add(*right, current->docId, current->score);
      }
    }
    
    current = DocNode_Next(current);
  };
  
}

typedef struct leaf {
  DocNode *doctree;
  float min;
  float max;
  // TODO: make this a set/bloom filter
  int distinct;  // map[float32]struct{}
} Leaf;

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

typedef struct scoreNode {
  float score;
  struct scoreNode *left;
  struct scoreNode *right;
  Leaf *leaf;
} ScoreNode;

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
  //printf("Adding to leaf %p\n", n->leaf);
  Leaf_Add(n->leaf, docId, score);

  if (n->leaf->distinct > SR_MAX_LEAF_SIZE) {
//     printf("Splitting node with leaf %p\n", n->leaf);

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

  printf("found %d leaves\n", Vector_Size(leaves));
  return leaves;
}
static int cmpDocIds(const void *e1, const void *e2, const void *udata) {
  const DocNode *n1 = e1, *n2 = e2;

  return n1->docId - n2->docId;
  
}
int main(int argc, char **argv) {
  ScoreNode *root = newScoreNode(newLeaf(newDocNode(0, 0, NULL), 0, 0, 0));

  float min = 0, max = 50000;
  int N = 100000;
  for (int i = 0; i < N; i++) {
    ScoreNode_Add(root, rand() % (N*5), (float)(rand() % N*2));
  }
  

  int c = 0;
  int fp = 0;

  struct timespec start_time, end_time;

  
  
  for (int x = 0; x < 3; x++) {
    c = 0;
    fp = 0;
    

  Vector *leaves = ScoreNode_FindRange(root, min, max);
  int n = (int)Vector_Size(leaves);
  
  heap_t *pq = malloc(heap_sizeof(n));
  heap_init(pq, cmpDocIds, NULL, n);
    
    for (int i = 0; i < n; i++) {
    Leaf *l;
    Vector_Get(leaves, i, &l);
    // printf("found leaf %f..%f\n", l->min, l->max);
    heap_offer(&pq, DocNode_Iterate(l->doctree));
  }
  clock_gettime(CLOCK_REALTIME, &start_time);
  while (heap_count(pq)) {
    
    DocNode *minit = heap_poll(pq);
    if (minit == NULL) {
      break;
    }
    

    // printf("%d => %f\n", min->node->docId, min->node->score);

    // fmt.Println(min.node.docId, min.node.score)
    if (minit->score >= min && minit->score <= max) {
      c++;
    } else {
      fp++;
    }
    do {
      minit = DocNode_Next(minit);
      if (minit && minit->score >= min && minit->score <= max) {
        break;
      }
      fp++;
    } while (minit);
    
    if (minit) {
      heap_offerx(pq, minit);
    }
  }

  clock_gettime(CLOCK_REALTIME, &end_time);
  long diffInNanos = end_time.tv_nsec - start_time.tv_nsec;

  printf("got %d/%d nodes, Time elapsed: %ldnano\n", c, fp, diffInNanos);
  // printf("got %d nodes\n", c);
  }
}
