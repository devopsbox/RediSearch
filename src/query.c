#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <math.h>
#include <sys/param.h>

#include "index.h"
#include "tokenize.h"
#include "redis_index.h"
#include "util/logging.h"
#include "util/heap.h"
#include "query.h"


void QueryStage_Free(QueryStage *s) {
  // recursively free the child stages
  for (int i = 0; i < s->nchildren; i++) {
    QueryStage_Free(s->children[i]);
  }
  // free the children array
  if (s->children) {
    free(s->children);
  }
  // the term is strdupped, so needs to be freed
  if (s->value && s->valueFreeable) {
      free(s->value);
  }
  free(s);
}


QueryStage *__newQueryStage(void *value, QueryOp op, int freeable) {
  QueryStage *s = malloc(sizeof(QueryStage));
  s->children = NULL;
  s->nchildren = 0;
  s->op = op;
  s->value = value;
  s->valueFreeable = freeable;
  s->parent = NULL;
  return s;
}

QueryStage *NewTokenStage(const char *term) {
  return __newQueryStage((char*)term, Q_LOAD, 1);
}

QueryStage *NewLogicStage(QueryOp op) {
  return __newQueryStage(NULL, op, 0);
}

QueryStage *NewNumericStage(NumericIterator *flt) {
  return __newQueryStage(flt, Q_NUMERIC, 0);
}


IndexIterator *query_EvalLoadStage(Query *q, QueryStage *stage) {
  
  // if there's only one word in the query and no special field filtering,
  // we can just use the optimized score index
  
  int isSingleWord = q->numTokens == 1 && q->fieldMask == 0xff && q->root->nchildren == 1;
  
  IndexReader *ir = Redis_OpenReader(q->ctx, stage->value, q->docTable, isSingleWord, q->fieldMask);
  if (ir == NULL) {
    return NULL;
  }
  
  return NewReadIterator(ir);
}

IndexIterator *query_EvalIntersectStage(Query *q, QueryStage *stage) {
  // an intersect stage with one child is the same as the child, so we just return it
  if (stage->nchildren == 1) {
    return Query_EvalStage(q, stage->children[0]);
  }
  
  // recursively eval the children
  IndexIterator **iters = calloc(stage->nchildren, sizeof(IndexIterator *));
  for (int i = 0; i < stage->nchildren; i++) {
    iters[i] = Query_EvalStage(q, stage->children[i]);
  }

  IndexIterator *ret =
      NewIntersecIterator(iters, stage->nchildren, 0, q->docTable, q->fieldMask);
  return ret;
}

IndexIterator *query_EvalNumericStage(Query *q, QueryStage *stage) {
    
    NumericIterator *it = stage->value;
    //printf("got numeric filter %p\n", it);
    return NewNumericFilterIterator(it);
}

IndexIterator *query_EvalUnionStage(Query *q, QueryStage *stage) {
  // a union stage with one child is the same as the child, so we just return it
  if (stage->nchildren == 1) {
    return Query_EvalStage(q, stage->children[0]);
  }
  
  // recursively eval the children
  IndexIterator **iters = calloc(stage->nchildren, sizeof(IndexIterator *));
  for (int i = 0; i < stage->nchildren; i++) {
    iters[i] = Query_EvalStage(q, stage->children[i]);
  }

  IndexIterator *ret = NewUnionIterator(iters, stage->nchildren, q->docTable);
  return ret;
}


IndexIterator *query_EvalExactIntersectStage(Query *q, QueryStage *stage) {
  // an intersect stage with one child is the same as the child, so we just return it
  if (stage->nchildren == 1) {
    return Query_EvalStage(q, stage->children[0]);
  }
  IndexIterator **iters = calloc(stage->nchildren, sizeof(IndexIterator *));
  for (int i = 0; i < stage->nchildren; i++) {
    iters[i] = Query_EvalStage(q, stage->children[i]);
  }

  IndexIterator *ret =
      NewIntersecIterator(iters, stage->nchildren, 1, q->docTable, q->fieldMask);
  return ret;
}

IndexIterator *Query_EvalStage(Query *q, QueryStage *s) {
  switch (s->op) {
    case Q_LOAD:
      return query_EvalLoadStage(q, s);
    case Q_INTERSECT:
      return query_EvalIntersectStage(q, s);
    case Q_EXACT:
      return query_EvalExactIntersectStage(q, s);
    case Q_UNION:
      return query_EvalUnionStage(q, s);
    case Q_NUMERIC:
      return query_EvalNumericStage(q, s);
  }

  return NULL;
}

void QueryStage_AddChild(QueryStage *parent, QueryStage *child) {
  parent->children =
      realloc(parent->children, sizeof(QueryStage *) * (parent->nchildren + 1));
  parent->children[parent->nchildren++] = child;
  child->parent = parent;
}

int queryTokenFunc(void *ctx, Token t) {
  Query *q = ctx;
  q->numTokens++;
  QueryStage_AddChild(q->root, NewTokenStage(t.s));

  return 0;
}

Query *NewQuery(RedisSearchCtx *ctx, const char *query, size_t len,
                  int offset, int limit, u_char fieldMask) {
  Query *ret = calloc(1, sizeof(Query));
  ret->ctx = ctx;
  ret->len = len;
  ret->limit = limit;
  ret->fieldMask = fieldMask;
  ret->offset = offset;
  ret->raw = strndup(query, len);
  ret->root = __newQueryStage(NULL, Q_INTERSECT, 0);
  ret->numTokens = 0;

  return ret;
}

void __queryStage_Print(QueryStage *qs, int depth) {
  for (int i = 0; i < depth; i++) {
    printf("  ");
  }
  switch (qs->op) {
    case Q_EXACT:
      printf("EXACT {\n");
      break;
    case Q_LOAD:
      printf("{%s", (char*) qs->value);
      break;
    case Q_INTERSECT:
      printf("INTERSECT {\n");
      break;
  case Q_NUMERIC: {
        NumericFilter *f = qs->value;
        printf("NUMERIC {%f < x < %f", f->min, f->max);
      }
      break;      
    case Q_UNION:
      printf("UNION {\n");
      break;
  }

  for (int i = 0; i < qs->nchildren; i++) {
    __queryStage_Print(qs->children[i], depth + 1);
  }

  if (qs->nchildren > 0) {
    for (int i = 0; i < depth; i++) {
      printf("  ");
    }
  }
  printf("}\n");
}

int Query_Tokenize(Query *q) {
  QueryTokenizer t = NewQueryTokenizer(q->raw, q->len);

  QueryStage *current = q->root;
  while (QueryTokenizer_HasNext(&t)) {
    QueryToken qt = QueryTokenizer_Next(&t);

    switch (qt.type) {
      case T_WORD:
        q->numTokens++;
        QueryStage_AddChild(current, NewTokenStage(qt.s));
        break;
        
      case T_QUOTE:
        if (current->op != Q_EXACT) {
          QueryStage *ns = NewLogicStage(Q_EXACT);
          QueryStage_AddChild(current, ns);
          current = ns;
        } else {  // end of quote
          current = current->parent;
        }
        break;
        
      case T_STOPWORD:
      case T_END:
      default:
        break;
    }

    if (current == NULL) break;
  }

  //__queryStage_Print(q->root, 0);

  return q->numTokens;
}

void Query_Free(Query *q) {
  QueryStage_Free(q->root);
  free(q->raw);
  free(q);
}

/* Compare hits for sorting in the heap during traversal of the top N */
static int cmpHits(const void *e1, const void *e2, const void *udata) {
  const IndexHit *h1 = e1, *h2 = e2;

  if (h1->totalFreq < h2->totalFreq) {
    return 1;
  } else if (h1->totalFreq > h2->totalFreq) {
    return -1;
  }
  return h1->docId - h2->docId;
}
/* Factor document score (and TBD - other factors) in the hit's score.
This is done only for the root iterator */
double processHitScore(IndexHit *h, DocTable *dt) {
  
  // for exact hits we don't need to calculate minimal offset dist
  int md = h->type == H_EXACT ? 1 : VV_MinDistance(h->offsetVecs, h->numOffsetVecs);
  return (h->totalFreq) / pow((double)md, 2);
}

QueryResult *Query_Execute(Query *query) {
  
  //__queryStage_Print(query->root, 0);
  QueryResult *res = malloc(sizeof(QueryResult));
  res->error = 0;
  res->errorString = NULL;
  res->totalResults = 0;
  res->ids = NULL;
  res->numIds = 0;
  
  int num = query->offset + query->limit;
  heap_t *pq = malloc(heap_sizeof(num));
  heap_init(pq, cmpHits, NULL, num);

  //  start lazy evaluation of all query steps
  IndexIterator *it = NULL;
  if (query->root != NULL) {
    it = Query_EvalStage(query, query->root);
  }

  // no query evaluation plan?
  if (query->root == NULL || it == NULL) {
    res->error = QUERY_ERROR_INTERNAL;
    res->errorString = QUERY_ERROR_INTERNAL_STR;
    return res;
  }

  IndexHit *pooledHit = NULL;
  // iterate the root iterator and push everything to the PQ
  while (1) {
    // TODO - Use static allocation
    if (pooledHit == NULL) {
      pooledHit = malloc(sizeof(IndexHit));
    }
    IndexHit *h = pooledHit;
    IndexHit_Init(h);
    int rc = it->Read(it->ctx, h);
     
    if (rc == INDEXREAD_EOF) {
      break;
    } else if (rc == INDEXREAD_NOTFOUND) {
      continue;
    }

    h->totalFreq = processHitScore(h, query->docTable);

    ++res->totalResults;

    if (heap_count(pq) < heap_size(pq)) {
      heap_offerx(pq, h);
      pooledHit = NULL;
    } else {
      IndexHit *qh = heap_peek(pq);
      if (qh->totalFreq < h->totalFreq) {
        pooledHit = heap_poll(pq);
        heap_offerx(pq, h);
        // IndexHit_Terminate(pooledHit);
      } else {
        pooledHit = h;
        // IndexHit_Terminate(pooledHit);
      }
    }
  }

  if (pooledHit) {
    free(pooledHit);
  }

  it->Free(it);

  // Reverse the results into the final result
  size_t n = MIN(heap_count(pq), query->limit);
  res->numIds = n;
  res->ids = calloc(n, sizeof(RedisModuleString *));

  for (int i = 0; i < n; ++i) {
    IndexHit *h = heap_poll(pq);
    LG_DEBUG("Popping %d freq %f", h->docId, h->totalFreq);
    res->ids[n - i - 1] = Redis_GetDocKey(query->ctx, h->docId);
    free(h);
  }
  
  // if we still have something in the heap (meaning offset > 0), we need to poll...
  while (heap_count(pq) > 0) {
    IndexHit *h = heap_poll(pq);
    free(h);
  }

  heap_free(pq);
  return res;
}

void QueryResult_Free(QueryResult *q) {
  free(q->ids);
  free(q);
}
