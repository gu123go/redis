/* Hash Tables Implementation.
 *
 * This file implements in-memory hash tables with insert/del/replace/find/
 * get-random-element operations. Hash tables will auto-resize if needed
 * tables of power of two in size are used, collisions are handled by
 * chaining. See the source code for more information... :)
 *
 * Copyright (c) 2006-2012, Salvatore Sanfilippo <antirez at gmail dot com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Redis nor the names of its contributors may be used
 *     to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdint.h>

#ifndef __DICT_H
#define __DICT_H

#define DICT_OK 0
#define DICT_ERR 1

/* Unused arguments generate annoying warnings... */
//去掉对没有使用的局部变量的警告
//主要使用在复制key和value时，传入的privatdata，但是并没有使用，见dict.c+1096
#define DICT_NOTUSED(V) ((void) V)

typedef struct dictEntry {
    void *key;
	//下面各类型在x64系统上面都是8字节
    union {
        void *val;
        uint64_t u64;
        int64_t s64;
        double d;
    } v;
    struct dictEntry *next;
} dictEntry;

typedef struct dictType {
    unsigned int (*hashFunction)(const void *key);
	//复制key
    void *(*keyDup)(void *privdata, const void *key);             
    void *(*valDup)(void *privdata, const void *obj);
    int (*keyCompare)(void *privdata, const void *key1, const void *key2);
    void (*keyDestructor)(void *privdata, void *key);
    void (*valDestructor)(void *privdata, void *obj);
} dictType;

/* This is our hash table structure. Every dictionary has two of this as we
 * implement incremental rehashing, for the old to the new table. */

//hash 表
typedef struct dictht {
    dictEntry **table;
    unsigned long size;
    unsigned long sizemask;     //大小等于size-1，
    unsigned long used;
} dictht;

typedef struct dict {
    dictType *type;
    void *privdata;
    dictht ht[2];
	//记录当前重哈希哪个桶，为了防止重哈希过程中阻塞线程，每次只处理少量。
    long rehashidx; /* rehashing not in progress if rehashidx == -1 */
    int iterators; /* number of iterators currently running */
} dict;

/* If safe is set to 1 this is a safe iterator, that means, you can call
 * dictAdd, dictFind, and other functions against the dictionary even while
 * iterating. Otherwise it is a non safe iterator, and only dictNext()
 * should be called while iterating. */
typedef struct dictIterator {
    dict *d;
    long index;
    int table, safe;
    dictEntry *entry, *nextEntry;
    /* unsafe iterator fingerprint for misuse detection. */
    long long fingerprint;
} dictIterator;

typedef void (dictScanFunction)(void *privdata, const dictEntry *de);

/* This is the initial size of every hash table */
#define DICT_HT_INITIAL_SIZE     4

/* ------------------------------- Macros ------------------------------------*/
#define dictFreeVal(d, entry) \
    if ((d)->type->valDestructor) \
        (d)->type->valDestructor((d)->privdata, (entry)->v.val)

#define dictSetVal(d, entry, _val_) do { \
    if ((d)->type->valDup) \
        entry->v.val = (d)->type->valDup((d)->privdata, _val_); \
    else \
        entry->v.val = (_val_); \
} while(0)

#define dictSetSignedIntegerVal(entry, _val_) \
    do { entry->v.s64 = _val_; } while(0)

#define dictSetUnsignedIntegerVal(entry, _val_) \
    do { entry->v.u64 = _val_; } while(0)

#define dictSetDoubleVal(entry, _val_) \
    do { entry->v.d = _val_; } while(0)

#define dictFreeKey(d, entry) \
    if ((d)->type->keyDestructor) \
        (d)->type->keyDestructor((d)->privdata, (entry)->key)

#define dictSetKey(d, entry, _key_) do { \
    if ((d)->type->keyDup) \
        entry->key = (d)->type->keyDup((d)->privdata, _key_); \
    else \
        entry->key = (_key_); \
} while(0)

#define dictCompareKeys(d, key1, key2) \
    (((d)->type->keyCompare) ? \
        (d)->type->keyCompare((d)->privdata, key1, key2) : \
        (key1) == (key2))

#define dictHashKey(d, key) (d)->type->hashFunction(key)
#define dictGetKey(he) ((he)->key)
#define dictGetVal(he) ((he)->v.val)
#define dictGetSignedIntegerVal(he) ((he)->v.s64)
#define dictGetUnsignedIntegerVal(he) ((he)->v.u64)
#define dictGetDoubleVal(he) ((he)->v.d)
#define dictSlots(d) ((d)->ht[0].size+(d)->ht[1].size)
#define dictSize(d) ((d)->ht[0].used+(d)->ht[1].used)
#define dictIsRehashing(d) ((d)->rehashidx != -1)

/* API */
	
//构造一个dict，并初始化
dict *dictCreate(dictType *type, void *privDataPtr);
//根据size构建新的hashtable，如果0号hashtable没使用，初始化0号hashtable，否则初始化1号hashtable，并设置rehashidx=1
//设置ht[0]，或者ht[1]
int dictExpand(dict *d, unsigned long size);
//哈希表中添加一个元素
//调用dictAddRaw，_dictRehashStep如果重哈希，则单步重哈希；
//在_dictKeyIndex中，如果需要初始化/扩大哈希表，则扩大/初始化；然后通过key计算哈希，并判断是否已经存在（可能判断两个哈希表）
//如果设置了rehashidx，优先使用ht[1]；构建entry，使用头插入插入index所在链表，然后将key复制进entry(可能需要分配内存，dup函数)
//如果构建entry成功，将value复制进entry
int dictAdd(dict *d, void *key, void *val);

//通过key构建entry
dictEntry *dictAddRaw(dict *d, void *key);
//首先尝试添加，如果添加成功，返回
//查找，返回指向entry的指针，修改value
int dictReplace(dict *d, void *key, void *val);
//查找，如果不为空，返回entry指针，否则添加进dict
dictEntry *dictReplaceRaw(dict *d, void *key);

//调用dictGenericDelete
//nofree=0
int dictDelete(dict *d, const void *key);

//调用dictGenericDelete
//nofree=1
int dictDeleteNoFree(dict *d, const void *key);

//清除并销毁字典dict内部的哈希表
void dictRelease(dict *d);

//通过key查找,不存在返回NULL
dictEntry * dictFind(dict *d, const void *key);

//通过key返回value，否则返回NULL
//内部调用dictFind
void *dictFetchValue(dict *d, const void *key);

//调整哈希表容量，目标是用最小的散列数组来容纳所有的键值对。
int dictResize(dict *d);

//初始化一个普通迭代器
                                                 //当一个迭代器添加到一个dict上面时，dict的iterator个数为什么没有改变？？？？？？
dictIterator *dictGetIterator(dict *d);

//safe设置为1
dictIterator *dictGetSafeIterator(dict *d);

//遍历哈希表
dictEntry *dictNext(dictIterator *iter);
//释放迭代器
void dictReleaseIterator(dictIterator *iter);

//随机，如果没有重哈希，只计算ht[0]，否则计算两个表，然后在得到的非空桶里面随机出一个值所在的entry返回
dictEntry *dictGetRandomKey(dict *d);

//改自dictGetRandomKey，比dictGetRandomKey快
unsigned int dictGetSomeKeys(dict *d, dictEntry **des, unsigned int count);
//调用_dictPrintStatsHt，debugging
void dictPrintStats(dict *d);
//hash函数
unsigned int dictGenHashFunction(const void *key, int len);
//hash函数
unsigned int dictGenCaseHashFunction(const unsigned char *buf, int len);
//清空字典上的所有哈希表节点，并重置字典属性
//和dictRelease的区别是并不删除dict
void dictEmpty(dict *d, void(callback)(void*));
//dict_can_resize = 1
void dictEnableResize(void);
//dict_can_resize = 0
void dictDisableResize(void);

//重哈希，将表0中的元素逐步移动到表1
int dictRehash(dict *d, int n);

//在给定毫秒数内，以 100 步为单位，对字典进行 rehash 
int dictRehashMilliseconds(dict *d, int ms);
//哈希种子，5381
void dictSetHashFunctionSeed(unsigned int initval);
//返回哈希种子，5381
unsigned int dictGetHashFunctionSeed(void);
//遍历dict
unsigned long dictScan(dict *d, unsigned long v, dictScanFunction *fn, void *privdata);

/* Hash table types */
extern dictType dictTypeHeapStringCopyKey;
extern dictType dictTypeHeapStrings;
extern dictType dictTypeHeapStringCopyKeyValue;

#endif /* __DICT_H */
