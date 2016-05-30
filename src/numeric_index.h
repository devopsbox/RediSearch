#ifndef __NUMERIC_INDEX_H__
#define __NUMERIC_INDEX_H__
#include "types.h"
#include "spec.h"
#include "rmutil/strings.h"
#include "redismodule.h"
#include "index.h"
#include "srtree.h"

typedef struct numericIndex {
    RedisModuleKey *key;
    RedisSearchCtx *ctx;
    ScoreNode *srtree;
} NumericIndex;

typedef struct {
    double min;
    double max;
    int minNegInf;
    int maxInf;
    int inclusiveMin;
    int inclusiveMax;
    const char *fieldName;
    size_t fieldNameLen;
} NumericFilter;

typedef struct {
    NumericIndex *idx;   
    NumericFilter *filter;   
    t_docId lastDocid;
    heap_t *pq;
   
    int eof;
} NumericIterator;


static ScoreNode *cachedIndex;

NumericIndex *NewNumericIndex(RedisSearchCtx *ctx, FieldSpec *sp);

void NumerIndex_Free(NumericIndex *idx);

int NumerIndex_Add(NumericIndex *idx, t_docId docId, double score);

IndexIterator *NewNumericFilterIterator(NumericIterator *it) ;
NumericIterator *NewNumericIterator(NumericFilter *f, NumericIndex *idx);

int NumericIterator_SkipTo(void *ctx, u_int32_t docId, IndexHit *hit);
int NumericIterator_Read(void *ctx, IndexHit *e);
int NumericIterator_HasNext(void *ctx);
t_docId NumericIterator_LastDocId(void *ctx);
void NumericIterator_Free(IndexIterator *it);

NumericFilter *NewNumericFilter(double min, double max, int inclusiveMin, int inclusiveMax);
DocNode *__numericIterator_Next(NumericIterator *it);
void __numericIterator_start(NumericIterator *it, ScoreNode *root);

NumericFilter *ParseNumericFilter(RedisSearchCtx *ctx, RedisModuleString **argv, int argc);
DocNode *DocTreeIterator_Current(DocTreeIterator *it);
#endif