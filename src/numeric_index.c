#include "numeric_index.h"
/*
A numeric index allows indexing of documents by numeric ranges, and intersection of them with
fulltext indexes.
*/

int numericFilter_Match(NumericFilter *f, double score) {
    
    // match min - -inf or x >/>= score 
    int matchMin = f->minNegInf || (f->inclusiveMin ? score >= f->min : score > f->min);
    
    if (matchMin) {
        // match max - +inf or x </<= score
        return f->maxInf || (f->inclusiveMax ? score <= f->max : score < f->max);
    }
    
    return 0;
}

#define NUMERIC_INDEX_KEY_FMT "num:%s/%s"

RedisModuleString *fmtNumericIndexKey(RedisSearchCtx *ctx, const char *field) {
    return RMUtil_CreateFormattedString(ctx->redisCtx, NUMERIC_INDEX_KEY_FMT, ctx->spec->name, field);   
}

NumericIndex *NewNumericIndex(RedisSearchCtx *ctx, FieldSpec *sp) {
    
    RedisModuleString *s = fmtNumericIndexKey(ctx, sp->name);
    RedisModuleKey *k = RedisModule_OpenKey(ctx->redisCtx, s, REDISMODULE_READ | REDISMODULE_WRITE);
    if (k == NULL || 
        (RedisModule_KeyType(k) != REDISMODULE_KEYTYPE_EMPTY &&
        RedisModule_KeyType(k) != REDISMODULE_KEYTYPE_ZSET)) {
            k == NULL;
    }
        
    NumericIndex *ret = malloc(sizeof(NumericIndex));
    ret->ctx = ctx;
    ret->key = k;
    return ret;
    
}

void NumerIndex_Free(NumericIndex *idx) {
    
    if (idx->key) RedisModule_CloseKey(idx->key);
    free(idx);
}

int NumerIndex_Add(NumericIndex *idx, t_docId docId, double score) {
    
    if (idx->key == NULL) return REDISMODULE_ERR;
    
    return RedisModule_ZsetAdd(idx->key, score, 
                                RMUtil_CreateFormattedString(idx->ctx->redisCtx, "%u", docId), NULL);
    
}

 


NumericFilter *NewNumericFilter(double min, double max, 
                                int inclusiveMin, int inclusiveMax) {
    
    NumericFilter *f = malloc(sizeof(NumericFilter));
    f->min = min;
    f->max = max;
    f->inclusiveMax = inclusiveMax;
    f->inclusiveMin = inclusiveMin;
    return f;
}

/* qsort docId comparison function */ 
int cmp_docId(const void *a, const void *b) 
{ 
    return *(const int *)a - *(const int *)b; // casting pointer types 
} 
 

int _numericIndex_LoadIndex(NumericIndex *idx) {
    

    cachedIndex = newScoreNode(newLeaf(newDocNode(0, 0, NULL), 0, 0, 0));
    RedisModuleKey *key = idx->key;
    RedisModuleCtx *ctx = idx->ctx->redisCtx;
    RedisModule_ZsetFirstInScoreRange(key,REDISMODULE_NEGATIVE_INFINITE, REDISMODULE_POSITIVE_INFINITE,0,0);
    
    while(!RedisModule_ZsetRangeEndReached(key)) {
        
        double score;
        RedisModuleString *ele = RedisModule_ZsetRangeCurrentElement(key,&score);
        long long ll;
        RedisModule_StringToLongLong(ele, &ll);
        RedisModule_FreeString(ctx, ele);
        //printf("adding %d->%f\n", (t_docId)ll, (float)score);
        ScoreNode_Add(cachedIndex, (t_docId)ll, (float)score);
        
        RedisModule_ZsetRangeNext(key);
    }
    RedisModule_ZsetRangeStop(key);
    return 1;
    
}


NumericIterator *NewNumericIterator(NumericFilter *f, NumericIndex *idx) {
    
    NumericIterator *it = malloc(sizeof(NumericIterator));
    it->filter = f;
    it->idx = idx;
    it->lastDocid = 0;
    it->eof = 0;
    
    if (cachedIndex == NULL) {
        _numericIndex_LoadIndex(idx);
    }
    //printf("min %f max %f\n", f->min,f->max);
    it->it = Iterate(cachedIndex, f->min, f->max);
    return it;
}

IndexIterator *NewNumericFilterIterator(NumericIterator *it) {
    IndexIterator *ret = malloc(sizeof(IndexIterator));
    ret->ctx = it;
    ret->Free = NumericIterator_Free;
    ret->HasNext = NumericIterator_HasNext;
    ret->LastDocId = NumericIterator_LastDocId;
    ret->Read = NumericIterator_Read;
    ret->SkipTo = NumericIterator_SkipTo;
    return ret;
}


/*
*  Parse numeric filter arguments, in the form of:
*  <fieldname> min max
*
*  By default, the interval specified by min and max is closed (inclusive). 
*  It is possible to specify an open interval (exclusive) by prefixing the score with the character (. 
*  For example: "score (1 5"
*  Will return filter elements with 1 < score <= 5
* 
*  min and max can be -inf and +inf
*  
*  Returns a numeric filter on success, NULL if there was a problem with the arguments
*/
NumericFilter *ParseNumericFilter(RedisSearchCtx *ctx, RedisModuleString **argv, int argc) {
                                       
    if (argc != 3) {
        return NULL;
    }
    // make sure we have an index spec for this filter and it's indeed numeric
    
    // NumericIndex *ni = NewNumericIndex(ctx, fs);
    
    NumericFilter *nf = malloc(sizeof(NumericFilter));
    nf->inclusiveMax = 1;
    nf->inclusiveMin = 1;
    nf->min = 0;
    nf->max = 0;
    nf->minNegInf = 0;
    nf->maxInf = 0;
    nf->fieldName = RedisModule_StringPtrLen(argv[0], &nf->fieldNameLen);
    // Parse the min range
    
    // -inf means anything is acceptable as a minimum
    if (RMUtil_StringEqualsC(argv[1], "-inf")) {
        nf->minNegInf = 1;
    } else {
        // parse the min range value - if it's OK we just set the value
        if (RedisModule_StringToDouble(argv[1], &nf->min) != REDISMODULE_OK) {
            size_t len = 0;
            const char *p = RedisModule_StringPtrLen(argv[1], &len);
            
            // if the first character is ( we treat the minimum as exclusive
            if (*p == '(' && len > 1) {
                p++;
                nf->inclusiveMin = 0;
                // we need to create a temporary string to parse it again...
                RedisModuleString *s = RedisModule_CreateString(ctx->redisCtx, p, len-1);
                if (RedisModule_StringToDouble(s, &nf->min) != REDISMODULE_OK) {
                    RedisModule_FreeString(ctx->redisCtx, s);
                    goto error;
                }
                // free the string now that it's parsed
                RedisModule_FreeString(ctx->redisCtx, s);
                
            } else goto error; //not a number
        }
    }
    
    // check if the max range is +inf
    if (RMUtil_StringEqualsC(argv[2], "+inf")) {
        nf->maxInf = 1;
    } else {
        // parse the max range. OK means we just read it into nf->max
        if (RedisModule_StringToDouble(argv[2], &nf->max) != REDISMODULE_OK) {
            
            // check see if the first char is ( and this is an exclusive range
            size_t len = 0;
            const char *p = RedisModule_StringPtrLen(argv[2], &len);
            if (*p == '(' && len > 1) {
                p++;
                nf->inclusiveMax = 0;
                // now parse the number part of the 
                RedisModuleString *s = RedisModule_CreateString(ctx->redisCtx, p, len-1);
                if (RedisModule_StringToDouble(s, &nf->max) != REDISMODULE_OK) {
                    RedisModule_FreeString(ctx->redisCtx, s);
                    goto error;
                }
                RedisModule_FreeString(ctx->redisCtx, s);
                
            } else goto error; //not a number
        }
    }
    
    return nf;
    
    
error:
    free(nf);
    return NULL;
                                       
}

int NumericIterator_Read(void *ctx, IndexHit *e) {
     //printf("read!\n");
     NumericIterator *it = ctx;
     if (!NumericIterator_HasNext(it) || it->idx->key == NULL){
         //printf("cannot read!\n");
         it->eof = 1;  
        return INDEXREAD_EOF;    
     }
     
     DocNode *n = SortedRangeIterator_Next(&it->it);
     if (!n) {
         it->eof = 1;
         return INDEXREAD_EOF;
     }

     e->flags = 0xFF;
     e->numOffsetVecs = 0;
     e->totalFreq = 0;
     e->type = H_RAW;
     e->docId = n->docId;
     it->lastDocid = n->docId;
     
     return INDEXREAD_OK;
}
// Skip to a docid, potentially reading the entry into hit, if the docId matches
//
// In this case we don't actually skip to a docId, just check whether it is within our range
int NumericIterator_SkipTo(void *ctx, u_int32_t docId, IndexHit *hit) {
    //printf("skipto %d\n", docId);
    NumericIterator *it = ctx;
    if (it->idx == NULL || it->idx->key == NULL || !NumericIterator_HasNext(ctx)) {
        //printf("cannot continue!\n");
        it->eof = 1;
        return INDEXREAD_EOF;
    }
        
    
    // if the index was loaded - just advance until we hit or pass the docId
    // TODO: Can be optimized with binary search
    while (it->lastDocid < docId) {
        
        DocNode *n = SortedRangeIterator_Next(&it->it);
        if (!n) {
            it->eof = 1;
            return INDEXREAD_EOF;
        }
        it->lastDocid = n->docId;
    }
    //printf("wanted %d got %d\n",docId, it->lastDocid);
    if (it->lastDocid == docId) {
        hit->flags = 0xFF;
        hit->numOffsetVecs = 0;
        hit->totalFreq = 0;
        hit->docId = it->lastDocid;
        return INDEXREAD_OK;
    } 
    
    return INDEXREAD_NOTFOUND;
        
}

// the last docId read
t_docId NumericIterator_LastDocId(void *ctx) {
    
    NumericIterator *f = ctx;
    //printf("last docId: %d\n", f->lastDocid);
    return f->lastDocid;
}
// can we continue iteration?
inline int NumericIterator_HasNext(void *ctx) {
  
    //printf("has next\n");
    NumericIterator *f = ctx;
    
    return 1;
    
    
}
// release the iterator's context and free everything needed
void NumericIterator_Free(struct indexIterator *self) {
    NumericIterator *f = self->ctx;
    free(f->idx);
    free(f->filter);
    free(f);
    free(self);
}
