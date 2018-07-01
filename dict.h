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
//ȥ����û��ʹ�õľֲ������ľ���
//��Ҫʹ���ڸ���key��valueʱ�������privatdata�����ǲ�û��ʹ�ã���dict.c+1096
#define DICT_NOTUSED(V) ((void) V)

typedef struct dictEntry {
    void *key;
	//�����������x64ϵͳ���涼��8�ֽ�
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
	//����key
    void *(*keyDup)(void *privdata, const void *key);             
    void *(*valDup)(void *privdata, const void *obj);
    int (*keyCompare)(void *privdata, const void *key1, const void *key2);
    void (*keyDestructor)(void *privdata, void *key);
    void (*valDestructor)(void *privdata, void *obj);
} dictType;

/* This is our hash table structure. Every dictionary has two of this as we
 * implement incremental rehashing, for the old to the new table. */

//hash ��
typedef struct dictht {
    dictEntry **table;
    unsigned long size;
    unsigned long sizemask;     //��С����size-1��
    unsigned long used;
} dictht;

typedef struct dict {
    dictType *type;
    void *privdata;
    dictht ht[2];
	//��¼��ǰ�ع�ϣ�ĸ�Ͱ��Ϊ�˷�ֹ�ع�ϣ�����������̣߳�ÿ��ֻ����������
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
	
//����һ��dict������ʼ��
dict *dictCreate(dictType *type, void *privDataPtr);
//����size�����µ�hashtable�����0��hashtableûʹ�ã���ʼ��0��hashtable�������ʼ��1��hashtable��������rehashidx=1
//����ht[0]������ht[1]
int dictExpand(dict *d, unsigned long size);
//��ϣ�������һ��Ԫ��
//����dictAddRaw��_dictRehashStep����ع�ϣ���򵥲��ع�ϣ��
//��_dictKeyIndex�У������Ҫ��ʼ��/�����ϣ��������/��ʼ����Ȼ��ͨ��key�����ϣ�����ж��Ƿ��Ѿ����ڣ������ж�������ϣ��
//���������rehashidx������ʹ��ht[1]������entry��ʹ��ͷ�������index��������Ȼ��key���ƽ�entry(������Ҫ�����ڴ棬dup����)
//�������entry�ɹ�����value���ƽ�entry
int dictAdd(dict *d, void *key, void *val);

//ͨ��key����entry
dictEntry *dictAddRaw(dict *d, void *key);
//���ȳ�����ӣ������ӳɹ�������
//���ң�����ָ��entry��ָ�룬�޸�value
int dictReplace(dict *d, void *key, void *val);
//���ң������Ϊ�գ�����entryָ�룬������ӽ�dict
dictEntry *dictReplaceRaw(dict *d, void *key);

//����dictGenericDelete
//nofree=0
int dictDelete(dict *d, const void *key);

//����dictGenericDelete
//nofree=1
int dictDeleteNoFree(dict *d, const void *key);

//����������ֵ�dict�ڲ��Ĺ�ϣ��
void dictRelease(dict *d);

//ͨ��key����,�����ڷ���NULL
dictEntry * dictFind(dict *d, const void *key);

//ͨ��key����value�����򷵻�NULL
//�ڲ�����dictFind
void *dictFetchValue(dict *d, const void *key);

//������ϣ��������Ŀ��������С��ɢ���������������еļ�ֵ�ԡ�
int dictResize(dict *d);

//��ʼ��һ����ͨ������
                                                 //��һ����������ӵ�һ��dict����ʱ��dict��iterator����Ϊʲôû�иı䣿����������
dictIterator *dictGetIterator(dict *d);

//safe����Ϊ1
dictIterator *dictGetSafeIterator(dict *d);

//������ϣ��
dictEntry *dictNext(dictIterator *iter);
//�ͷŵ�����
void dictReleaseIterator(dictIterator *iter);

//��������û���ع�ϣ��ֻ����ht[0]���������������Ȼ���ڵõ��ķǿ�Ͱ���������һ��ֵ���ڵ�entry����
dictEntry *dictGetRandomKey(dict *d);

//����dictGetRandomKey����dictGetRandomKey��
unsigned int dictGetSomeKeys(dict *d, dictEntry **des, unsigned int count);
//����_dictPrintStatsHt��debugging
void dictPrintStats(dict *d);
//hash����
unsigned int dictGenHashFunction(const void *key, int len);
//hash����
unsigned int dictGenCaseHashFunction(const unsigned char *buf, int len);
//����ֵ��ϵ����й�ϣ��ڵ㣬�������ֵ�����
//��dictRelease�������ǲ���ɾ��dict
void dictEmpty(dict *d, void(callback)(void*));
//dict_can_resize = 1
void dictEnableResize(void);
//dict_can_resize = 0
void dictDisableResize(void);

//�ع�ϣ������0�е�Ԫ�����ƶ�����1
int dictRehash(dict *d, int n);

//�ڸ����������ڣ��� 100 ��Ϊ��λ�����ֵ���� rehash 
int dictRehashMilliseconds(dict *d, int ms);
//��ϣ���ӣ�5381
void dictSetHashFunctionSeed(unsigned int initval);
//���ع�ϣ���ӣ�5381
unsigned int dictGetHashFunctionSeed(void);
//����dict
unsigned long dictScan(dict *d, unsigned long v, dictScanFunction *fn, void *privdata);

/* Hash table types */
extern dictType dictTypeHeapStringCopyKey;
extern dictType dictTypeHeapStrings;
extern dictType dictTypeHeapStringCopyKeyValue;

#endif /* __DICT_H */
