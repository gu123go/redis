/*
 * Copyright (c) 2009-2012, Salvatore Sanfilippo <antirez at gmail dot com>
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

#include "redis.h"

/*-----------------------------------------------------------------------------
 * Set Commands
 *----------------------------------------------------------------------------*/

void sunionDiffGenericCommand(redisClient *c, robj **setkeys, int setnum, robj *dstkey, int op);

/* Factory method to return a set that *can* hold "value". When the object has
 * an integer-encodable value, an intset will be returned. Otherwise a regular
 * hash table. */
//创建一个空set
//可能是intset，或者set
robj *setTypeCreate(robj *value) {
	//是否能转换成long long
    if (isObjectRepresentableAsLongLong(value,NULL) == REDIS_OK)
        return createIntsetObject();
    return createSetObject();
}

//添加一个value
//能用intset尽可能用intset，否则转换成ht
int setTypeAdd(robj *subject, robj *value) {
    long long llval;
	//hash table
    if (subject->encoding == REDIS_ENCODING_HT) {
        if (dictAdd(subject->ptr,value,NULL) == DICT_OK) {
            incrRefCount(value);
            return 1;
        }
	//intset
    } else if (subject->encoding == REDIS_ENCODING_INTSET) {
        if (isObjectRepresentableAsLongLong(value,&llval) == REDIS_OK) {
            uint8_t success = 0;
            subject->ptr = intsetAdd(subject->ptr,llval,&success);
            if (success) {
                /* Convert to regular set when the intset contains
                 * too many entries. */
				//长度过长
                if (intsetLen(subject->ptr) > server.set_max_intset_entries)
					//转换成ht编码
                    setTypeConvert(subject,REDIS_ENCODING_HT);
                return 1;
            }
        } else {
            /* Failed to get integer from object, convert to regular set. */
            setTypeConvert(subject,REDIS_ENCODING_HT);

            /* The set *was* an intset and this value is not integer
             * encodable, so dictAdd should always work. */
            redisAssertWithInfo(NULL,value,dictAdd(subject->ptr,value,NULL) == DICT_OK);
            incrRefCount(value);
            return 1;
        }
    } else {
        redisPanic("Unknown set encoding");
    }
    return 0;
}

//删除数据value
int setTypeRemove(robj *setobj, robj *value) {
    long long llval;
    if (setobj->encoding == REDIS_ENCODING_HT) {
		//删除
        if (dictDelete(setobj->ptr,value) == DICT_OK) {
            if (htNeedsResize(setobj->ptr)) dictResize(setobj->ptr);
            return 1;
        }
		//转换成整形
    } else if (setobj->encoding == REDIS_ENCODING_INTSET) {
        if (isObjectRepresentableAsLongLong(value,&llval) == REDIS_OK) {
            int success;
            setobj->ptr = intsetRemove(setobj->ptr,llval,&success);
            if (success) return 1;
        }
    } else {
        redisPanic("Unknown set encoding");
    }
    return 0;
}

//是否存在value
int setTypeIsMember(robj *subject, robj *value) {
    long long llval;
    if (subject->encoding == REDIS_ENCODING_HT) {
        return dictFind((dict*)subject->ptr,value) != NULL;
    } else if (subject->encoding == REDIS_ENCODING_INTSET) {
        if (isObjectRepresentableAsLongLong(value,&llval) == REDIS_OK) {
            return intsetFind((intset*)subject->ptr,llval);
        }
    } else {
        redisPanic("Unknown set encoding");
    }
    return 0;
}

//返回一个迭代器
setTypeIterator *setTypeInitIterator(robj *subject) {
	//迭代器
    setTypeIterator *si = zmalloc(sizeof(setTypeIterator));
    si->subject = subject;
    si->encoding = subject->encoding;
    if (si->encoding == REDIS_ENCODING_HT) {
        si->di = dictGetIterator(subject->ptr);
    } else if (si->encoding == REDIS_ENCODING_INTSET) {
        si->ii = 0;
    } else {
        redisPanic("Unknown set encoding");
    }
    return si;
}

//释放iterator
void setTypeReleaseIterator(setTypeIterator *si) {
    if (si->encoding == REDIS_ENCODING_HT)
        dictReleaseIterator(si->di);
    zfree(si);
}

/* Move to the next entry in the set. Returns the object at the current
 * position.
 *
 * Since set elements can be internally be stored as redis objects or
 * simple arrays of integers, setTypeNext returns the encoding of the
 * set object you are iterating, and will populate the appropriate pointer
 * (eobj) or (llobj) accordingly.
 *
 * When there are no longer elements -1 is returned.
 * Returned objects ref count is not incremented, so this function is
 * copy on write friendly. */

//移动到下一个位置，并返回当前位置的元素
int setTypeNext(setTypeIterator *si, robj **objele, int64_t *llele) {
    if (si->encoding == REDIS_ENCODING_HT) {
		//迭代器指向下一个，返回当前entry
        dictEntry *de = dictNext(si->di);
        if (de == NULL) return -1;
        *objele = dictGetKey(de);
    } else if (si->encoding == REDIS_ENCODING_INTSET) {
        if (!intsetGet(si->subject->ptr,si->ii++,llele))
            return -1;
    }
    return si->encoding;
}

/* The not copy on write friendly version but easy to use version
 * of setTypeNext() is setTypeNextObject(), returning new objects
 * or incrementing the ref count of returned objects. So if you don't
 * retain a pointer to this object you should call decrRefCount() against it.
 *
 * This function is the way to go for write operations where COW is not
 * an issue as the result will be anyway of incrementing the ref count. */

//返回下一个元素
robj *setTypeNextObject(setTypeIterator *si) {
    int64_t intele;
    robj *objele;
    int encoding;

    encoding = setTypeNext(si,&objele,&intele);
    switch(encoding) {
        case -1:    return NULL;
        case REDIS_ENCODING_INTSET:
            return createStringObjectFromLongLong(intele);
        case REDIS_ENCODING_HT:
            incrRefCount(objele);
            return objele;
        default:
            redisPanic("Unsupported encoding");
    }
    return NULL; /* just to suppress warnings */
}

/* Return random element from a non empty set.
 * The returned element can be a int64_t value if the set is encoded
 * as an "intset" blob of integers, or a redis object if the set
 * is a regular set.
 *
 * The caller provides both pointers to be populated with the right
 * object. The return value of the function is the object->encoding
 * field of the object and is used by the caller to check if the
 * int64_t pointer or the redis object pointer was populated.
 *
 * When an object is returned (the set was a real set) the ref count
 * of the object is not incremented so this function can be considered
 * copy on write friendly. */
//返回一个随机的元素
int setTypeRandomElement(robj *setobj, robj **objele, int64_t *llele) {
    if (setobj->encoding == REDIS_ENCODING_HT) {
        dictEntry *de = dictGetRandomKey(setobj->ptr);
        *objele = dictGetKey(de);
    } else if (setobj->encoding == REDIS_ENCODING_INTSET) {
        *llele = intsetRandom(setobj->ptr);
    } else {
        redisPanic("Unknown set encoding");
    }
    return setobj->encoding;
}

//返回size
unsigned long setTypeSize(robj *subject) {
    if (subject->encoding == REDIS_ENCODING_HT) {
        return dictSize((dict*)subject->ptr);
    } else if (subject->encoding == REDIS_ENCODING_INTSET) {
        return intsetLen((intset*)subject->ptr);
    } else {
        redisPanic("Unknown set encoding");
    }
}

/* Convert the set to specified encoding. The resulting dict (when converting
 * to a hash table) is presized to hold the number of elements in the original
 * set. */
//转换成enc编码格式
//seobj为intset类型，enc为REDIS_ENCODING_HT
void setTypeConvert(robj *setobj, int enc) {
    setTypeIterator *si;
	//可选编码方式为ht和intset
    redisAssertWithInfo(NULL,setobj,setobj->type == REDIS_SET &&
                             setobj->encoding == REDIS_ENCODING_INTSET);

	//hash table
    if (enc == REDIS_ENCODING_HT) {
        int64_t intele;
		//创建一个字典
        dict *d = dictCreate(&setDictType,NULL);
        robj *element;

        /* Presize the dict to avoid rehashing */
		//扩张长度
        dictExpand(d,intsetLen(setobj->ptr));

        /* To add the elements we extract integers and create redis objects */
		//返回一个intset类型迭代器
        si = setTypeInitIterator(setobj);
        while (setTypeNext(si,NULL,&intele) != -1) {
			//intset中存储的都是整形
            element = createStringObjectFromLongLong(intele);
			//添加到字典中
            redisAssertWithInfo(NULL,element,dictAdd(d,element,NULL) == DICT_OK);
        }
		//释放迭代器
        setTypeReleaseIterator(si);

		//设置编码格式
        setobj->encoding = REDIS_ENCODING_HT;
		//释放旧数据
        zfree(setobj->ptr);
		//修改指针
        setobj->ptr = d;
    } else {
        redisPanic("Unsupported set conversion");
    }
}

// SADD KEY_NAME VALUE1..VALUEN
//将一个或多个成员元素加入到集合中，已经存在于集合的成员元素将被忽略。
void saddCommand(redisClient *c) {
    robj *set;
    int j, added = 0;

	//KEY_NAME
    set = lookupKeyWrite(c->db,c->argv[1]);
    if (set == NULL) {
		//根据第一个value判断set还是intset
		//value只是用来获取类型
        set = setTypeCreate(c->argv[2]);
        dbAdd(c->db,c->argv[1],set);
    } else {
        if (set->type != REDIS_SET) {
            addReply(c,shared.wrongtypeerr);
            return;
        }
    }

	//处理value
    for (j = 2; j < c->argc; j++) {
        c->argv[j] = tryObjectEncoding(c->argv[j]);
        if (setTypeAdd(set,c->argv[j])) added++;
    }
	//通知
    if (added) {
        signalModifiedKey(c->db,c->argv[1]);
        notifyKeyspaceEvent(REDIS_NOTIFY_SET,"sadd",c->argv[1],c->db->id);
    }
    server.dirty += added;
    addReplyLongLong(c,added);
}


//SREM KEY MEMBER1..MEMBERN
//用于移除集合中的一个或多个成员元素，不存在的成员元素会被忽略。
void sremCommand(redisClient *c) {
    robj *set;
    int j, deleted = 0, keyremoved = 0;

	//不能为空
    if ((set = lookupKeyWriteOrReply(c,c->argv[1],shared.czero)) == NULL ||
        checkType(c,set,REDIS_SET)) return;

	//循环删除value
    for (j = 2; j < c->argc; j++) {
        if (setTypeRemove(set,c->argv[j])) {
            deleted++;
            if (setTypeSize(set) == 0) {
                dbDelete(c->db,c->argv[1]);
                keyremoved = 1;
                break;
            }
        }
    }
    if (deleted) {
        signalModifiedKey(c->db,c->argv[1]);
        notifyKeyspaceEvent(REDIS_NOTIFY_SET,"srem",c->argv[1],c->db->id);
        if (keyremoved)
            notifyKeyspaceEvent(REDIS_NOTIFY_GENERIC,"del",c->argv[1],
                                c->db->id);
        server.dirty += deleted;
    }
    addReplyLongLong(c,deleted);
}

// SMOVE SOURCE DESTINATION MEMBER 
//将指定成员 member 元素从 source 集合移动到 destination 集合。
void smoveCommand(redisClient *c) {
    robj *srcset, *dstset, *ele;
    srcset = lookupKeyWrite(c->db,c->argv[1]);
    dstset = lookupKeyWrite(c->db,c->argv[2]);
    ele = c->argv[3] = tryObjectEncoding(c->argv[3]);

    /* If the source key does not exist return 0 */
    if (srcset == NULL) {
        addReply(c,shared.czero);
        return;
    }

    /* If the source key has the wrong type, or the destination key
     * is set and has the wrong type, return with an error. */
    if (checkType(c,srcset,REDIS_SET) ||
        (dstset && checkType(c,dstset,REDIS_SET))) return;

    /* If srcset and dstset are equal, SMOVE is a no-op */
	//src和dest是同一个set
    if (srcset == dstset) {
        addReply(c,setTypeIsMember(srcset,ele) ? shared.cone : shared.czero);
        return;
    }

    /* If the element cannot be removed from the src set, return 0. */
	//不能移除，直接返回
    if (!setTypeRemove(srcset,ele)) {
        addReply(c,shared.czero);
        return;
    }
    notifyKeyspaceEvent(REDIS_NOTIFY_SET,"srem",c->argv[1],c->db->id);

    /* Remove the src set from the database when empty */
    if (setTypeSize(srcset) == 0) {
        dbDelete(c->db,c->argv[1]);
        notifyKeyspaceEvent(REDIS_NOTIFY_GENERIC,"del",c->argv[1],c->db->id);
    }
    signalModifiedKey(c->db,c->argv[1]);
    signalModifiedKey(c->db,c->argv[2]);
    server.dirty++;

    /* Create the destination set when it doesn't exist */
    if (!dstset) {
        dstset = setTypeCreate(ele);
        dbAdd(c->db,c->argv[2],dstset);
    }

    /* An extra key has changed when ele was successfully added to dstset */
    if (setTypeAdd(dstset,ele)) {
        server.dirty++;
        notifyKeyspaceEvent(REDIS_NOTIFY_SET,"sadd",c->argv[2],c->db->id);
    }
    addReply(c,shared.cone);
}

//SISMEMBER KEY VALUE 
//判断成员元素是否是集合的成员。
//查找
void sismemberCommand(redisClient *c) {
    robj *set;

    if ((set = lookupKeyReadOrReply(c,c->argv[1],shared.czero)) == NULL ||
        checkType(c,set,REDIS_SET)) return;

    c->argv[2] = tryObjectEncoding(c->argv[2]);
    if (setTypeIsMember(set,c->argv[2]))
        addReply(c,shared.cone);
    else
        addReply(c,shared.czero);
}

//SCARD KEY_NAME 
//返回集合中元素的数量
void scardCommand(redisClient *c) {
    robj *o;

	//返回key对应的value
    if ((o = lookupKeyReadOrReply(c,c->argv[1],shared.czero)) == NULL ||
        checkType(c,o,REDIS_SET)) return;

	//value的size
    addReplyLongLong(c,setTypeSize(o));
}

//SPOP KEY 
//移除并返回集合中的一个随机元素。
void spopCommand(redisClient *c) {
    robj *set, *ele, *aux;
    int64_t llele;
    int encoding;

	//key是否存在
    if ((set = lookupKeyWriteOrReply(c,c->argv[1],shared.nullbulk)) == NULL ||
        checkType(c,set,REDIS_SET)) return;

	//随机返回一个元素编码
    encoding = setTypeRandomElement(set,&ele,&llele);
	//删除
    if (encoding == REDIS_ENCODING_INTSET) {
        ele = createStringObjectFromLongLong(llele);
        set->ptr = intsetRemove(set->ptr,llele,NULL);
    } else {
        incrRefCount(ele);
        setTypeRemove(set,ele);
    }
    notifyKeyspaceEvent(REDIS_NOTIFY_SET,"spop",c->argv[1],c->db->id);

    /* Replicate/AOF this command as an SREM operation */
    aux = createStringObject("SREM",4);
    rewriteClientCommandVector(c,3,aux,c->argv[1],ele);
    decrRefCount(ele);
    decrRefCount(aux);

    addReplyBulk(c,ele);
    if (setTypeSize(set) == 0) {
        dbDelete(c->db,c->argv[1]);
        notifyKeyspaceEvent(REDIS_NOTIFY_GENERIC,"del",c->argv[1],c->db->id);
    }
    signalModifiedKey(c->db,c->argv[1]);
    server.dirty++;
}

/* handle the "SRANDMEMBER key <count>" variant. The normal version of the
 * command is handled by the srandmemberCommand() function itself. */

/* How many times bigger should be the set compared to the requested size
 * for us to don't use the "remove elements" strategy? Read later in the
 * implementation for more info. */
#define SRANDMEMBER_SUB_STRATEGY_MUL 3

//真正处理srandCommand部分
//如果 count 为正数，且小于集合基数，那么命令返回一个包含 count 个元素的数组，数组中的元素各不相同。如果 count 大于等于集合基数，那么返回整个集合。
//如果 count 为负数，那么命令返回一个数组，数组中的元素可能会重复出现多次，而数组的长度为 count 的绝对值
void srandmemberWithCountCommand(redisClient *c) {
    long l;
    unsigned long count, size;
    int uniq = 1;
    robj *set, *ele;
    int64_t llele;
    int encoding;

    dict *d;

	//count值写入l
    if (getLongFromObjectOrReply(c,c->argv[2],&l,NULL) != REDIS_OK) return;
	
	//正数还是负数
    if (l >= 0) {
        count = (unsigned) l;
    } else {
        /* A negative count means: return the same elements multiple times
         * (i.e. don't remove the extracted element after every extraction). */
        count = -l;
        uniq = 0;
    }

	//
    if ((set = lookupKeyReadOrReply(c,c->argv[1],shared.emptymultibulk))
        == NULL || checkType(c,set,REDIS_SET)) return;
    size = setTypeSize(set);

    /* If count is zero, serve it ASAP to avoid special cases later. */
    if (count == 0) {
        addReply(c,shared.emptymultibulk);
        return;
    }

    /* CASE 1: The count was negative, so the extraction method is just:
     * "return N random elements" sampling the whole set every time.
     * This case is trivial and can be served without auxiliary data
     * structures. */
	//负数
    if (!uniq) {
        addReplyMultiBulkLen(c,count);
        while(count--) {
			//返回一个随机值
            encoding = setTypeRandomElement(set,&ele,&llele);
            if (encoding == REDIS_ENCODING_INTSET) {
                addReplyBulkLongLong(c,llele);
            } else {
                addReplyBulk(c,ele);
            }
        }
        return;
    }

    /* CASE 2:
     * The number of requested elements is greater than the number of
     * elements inside the set: simply return the whole set. */
	//大于size，返回所有
    if (count >= size) {
        sunionDiffGenericCommand(c,c->argv+1,1,NULL,REDIS_OP_UNION);
        return;
    }

    /* For CASE 3 and CASE 4 we need an auxiliary dictionary. */
    d = dictCreate(&setDictType,NULL);

    /* CASE 3:
     * The number of elements inside the set is not greater than
     * SRANDMEMBER_SUB_STRATEGY_MUL times the number of requested elements.
     * In this case we create a set from scratch with all the elements, and
     * subtract random elements to reach the requested number of elements.
     *
     * This is done because if the number of requsted elements is just
     * a bit less than the number of elements in the set, the natural approach
     * used into CASE 3 is highly inefficient. */
	//count*3 > size时，返回所有元素，随机删除达到要求返回的元素为止
    if (count*SRANDMEMBER_SUB_STRATEGY_MUL > size) {
        setTypeIterator *si;

        /* Add all the elements into the temporary dictionary. */
        si = setTypeInitIterator(set);
        while((encoding = setTypeNext(si,&ele,&llele)) != -1) {
            int retval = DICT_ERR;

            if (encoding == REDIS_ENCODING_INTSET) {
                retval = dictAdd(d,createStringObjectFromLongLong(llele),NULL);
            } else {
                retval = dictAdd(d,dupStringObject(ele),NULL);
            }
            redisAssert(retval == DICT_OK);
        }
        setTypeReleaseIterator(si);
        redisAssert(dictSize(d) == size);

        /* Remove random elements to reach the right count. */
		//随机删除
        while(size > count) {
            dictEntry *de;

            de = dictGetRandomKey(d);
            dictDelete(d,dictGetKey(de));
            size--;
        }
    }

    /* CASE 4: We have a big set compared to the requested number of elements.
     * In this case we can simply get random elements from the set and add
     * to the temporary set, trying to eventually get enough unique elements
     * to reach the specified count. */
    else {
        unsigned long added = 0;

		//插入的值小于要求的值继续插入
        while(added < count) {
            encoding = setTypeRandomElement(set,&ele,&llele);
            if (encoding == REDIS_ENCODING_INTSET) {
				//创建一个新的
                ele = createStringObjectFromLongLong(llele);
            } else {
				//复制一个全新的，有自己的空间内存
                ele = dupStringObject(ele);
            }
            /* Try to add the object to the dictionary. If it already exists
             * free it, otherwise increment the number of objects we have
             * in the result dictionary. */
			//如果不存在，插入到dict中
			//否则删除返回的ele，重新随机返回一个元素
            if (dictAdd(d,ele,NULL) == DICT_OK)
                added++;
            else
				//释放
                decrRefCount(ele);
        }
    }

    /* CASE 3 & 4: send the result to the user. */
    {
        dictIterator *di;
        dictEntry *de;

        addReplyMultiBulkLen(c,count);
        di = dictGetIterator(d);
        while((de = dictNext(di)) != NULL)
            addReplyBulk(c,dictGetKey(de));
        dictReleaseIterator(di);
        dictRelease(d);
    }
}

//SRANDMEMBER KEY [count]
//如果 count 为正数，且小于集合基数，那么命令返回一个包含 count 个元素的数组，数组中的元素各不相同。如果 count 大于等于集合基数，那么返回整个集合。
//如果 count 为负数，那么命令返回一个数组，数组中的元素可能会重复出现多次，而数组的长度为 count 的绝对值
void srandmemberCommand(redisClient *c) {
    robj *set, *ele;
    int64_t llele;
    int encoding;

    if (c->argc == 3) {
		//真正处理部分
        srandmemberWithCountCommand(c);
        return;
	//错误
    } else if (c->argc > 3) {
        addReply(c,shared.syntaxerr);
        return;
    }

    if ((set = lookupKeyReadOrReply(c,c->argv[1],shared.nullbulk)) == NULL ||
        checkType(c,set,REDIS_SET)) return;

	//只有两个参数（srand key），默认返回一个元素
    encoding = setTypeRandomElement(set,&ele,&llele);
    if (encoding == REDIS_ENCODING_INTSET) {
        addReplyBulkLongLong(c,llele);
    } else {
        addReplyBulk(c,ele);
    }
}

//
int qsortCompareSetsByCardinality(const void *s1, const void *s2) {
    return setTypeSize(*(robj**)s1)-setTypeSize(*(robj**)s2);
}

/* This is used by SDIFF and in this case we can receive NULL that should
 * be handled as empty sets. */
//
int qsortCompareSetsByRevCardinality(const void *s1, const void *s2) {
    robj *o1 = *(robj**)s1, *o2 = *(robj**)s2;

    return  (o2 ? setTypeSize(o2) : 0) - (o1 ? setTypeSize(o1) : 0);
}

//交集
//setnum表示有多少个keys
//setkeys存储所有的keys
//dstkey可以为NULL或者argv[1]，表示是否传递过来一个用来存储结果的集合
void sinterGenericCommand(redisClient *c, robj **setkeys, unsigned long setnum, robj *dstkey) {
    robj **sets = zmalloc(sizeof(robj*)*setnum);
    setTypeIterator *si;
    robj *eleobj, *dstset = NULL;
    int64_t intobj;
    void *replylen = NULL;
    unsigned long j, cardinality = 0;
    int encoding;

	//查找所有keys对应的value，存入sets
    for (j = 0; j < setnum; j++) {
		//write比read少了一个访问命中和访问缺失
        robj *setobj = dstkey ?
            lookupKeyWrite(c->db,setkeys[j]) :
            lookupKeyRead(c->db,setkeys[j]);
		//查找j失败
		//释放空间
        if (!setobj) {
            zfree(sets);
            if (dstkey) {
                if (dbDelete(c->db,dstkey)) {
                    signalModifiedKey(c->db,dstkey);
                    server.dirty++;
                }
                addReply(c,shared.czero);
            } else {
                addReply(c,shared.emptymultibulk);
            }
            return;
        }
        if (checkType(c,setobj,REDIS_SET)) {
            zfree(sets);
            return;
        }
        sets[j] = setobj;
    }
    /* Sort sets from the smallest to largest, this will improve our
     * algorithm's performance */
	//排序，根据size
    qsort(sets,setnum,sizeof(robj*),qsortCompareSetsByCardinality);

    /* The first thing we should output is the total number of elements...
     * since this is a multi-bulk write, but at this stage we don't know
     * the intersection set size, so we use a trick, append an empty object
     * to the output list and save the pointer to later modify it with the
     * right length */
    if (!dstkey) {
        replylen = addDeferredMultiBulkLength(c);
    } else {
        /* If we have a target key where to store the resulting set
         * create this key with an empty set inside */
        dstset = createIntsetObject();
    }

    /* Iterate all the elements of the first (smallest) set, and test
     * the element against all the other sets, if at least one set does
     * not include the element it is discarded */
	//迭代器sets[0]
    si = setTypeInitIterator(sets[0]);
	//遍历sets[0]，然后在1至setnum中查找
	//查找过程中，优先使用intsetFind（估计是因为intset元素个数不是很多，查找比较快）
	//当所有都包含了该元素，添加进交集结果
    while((encoding = setTypeNext(si,&eleobj,&intobj)) != -1) {
        for (j = 1; j < setnum; j++) {
            if (sets[j] == sets[0]) continue;
			//如果都是intset，直接查找（二分）
			//否则将sets[0]的元素转换成string，然后dictFind查找
            if (encoding == REDIS_ENCODING_INTSET) {
                /* intset with intset is simple... and fast */
                if (sets[j]->encoding == REDIS_ENCODING_INTSET &&
                    !intsetFind((intset*)sets[j]->ptr,intobj))
                {
                    break;
                /* in order to compare an integer with an object we
                 * have to use the generic function, creating an object
                 * for this */
                } else if (sets[j]->encoding == REDIS_ENCODING_HT) {
                    eleobj = createStringObjectFromLongLong(intobj);
                    if (!setTypeIsMember(sets[j],eleobj)) {
                        decrRefCount(eleobj);
                        break;
                    }
                    decrRefCount(eleobj);
                }
			//如果sets[0]元素编码为ht，并且sets[j]元素编码为整形，并且用intset存储，则使用intsetFind
            } else if (encoding == REDIS_ENCODING_HT) {
                /* Optimization... if the source object is integer
                 * encoded AND the target set is an intset, we can get
                 * a much faster path. */
                if (eleobj->encoding == REDIS_ENCODING_INT &&
                    sets[j]->encoding == REDIS_ENCODING_INTSET &&
                    !intsetFind((intset*)sets[j]->ptr,(long)eleobj->ptr))
                {
                    break;
                /* else... object to object check is easy as we use the
                 * type agnostic API here. */
                } else if (!setTypeIsMember(sets[j],eleobj)) {
                    break;
                }
            }
        }

        /* Only take action when all sets contain the member */
		//当所有集合都包含了该元素
        if (j == setnum) {
            if (!dstkey) {
                if (encoding == REDIS_ENCODING_HT)
                    addReplyBulk(c,eleobj);
                else
                    addReplyBulkLongLong(c,intobj);
                cardinality++;
            } else {
                if (encoding == REDIS_ENCODING_INTSET) {
                    eleobj = createStringObjectFromLongLong(intobj);
                    setTypeAdd(dstset,eleobj);
                    decrRefCount(eleobj);
                } else {
                    setTypeAdd(dstset,eleobj);
                }
            }
        }
    }//sets[0]遍历完成
	
	//释放
    setTypeReleaseIterator(si);

	 //判断是否需要存储交集结果, 并进行相应操作
    if (dstkey) {
        /* Store the resulting set into the target, if the intersection
         * is not an empty set. */
        int deleted = dbDelete(c->db,dstkey);
        if (setTypeSize(dstset) > 0) {
            dbAdd(c->db,dstkey,dstset);
            addReplyLongLong(c,setTypeSize(dstset));
            notifyKeyspaceEvent(REDIS_NOTIFY_SET,"sinterstore",
                dstkey,c->db->id);
        } else {
            decrRefCount(dstset);
            addReply(c,shared.czero);
            if (deleted)
                notifyKeyspaceEvent(REDIS_NOTIFY_GENERIC,"del",
                    dstkey,c->db->id);
        }
        signalModifiedKey(c->db,dstkey);
        server.dirty++;
    } else {
        setDeferredMultiBulkLength(c,replylen,cardinality);
    }
    zfree(sets);
}

//SINTER KEY KEY1..KEYN 
//返回给定所有给定集合的交集
void sinterCommand(redisClient *c) {
    sinterGenericCommand(c,c->argv+1,c->argc-1,NULL);
}

// SINTERSTORE DESTINATION_KEY KEY KEY1..KEYN 
//将给定集合之间的交集存储在指定的集合中。如果指定的集合已经存在，则将其覆盖。
void sinterstoreCommand(redisClient *c) {
    sinterGenericCommand(c,c->argv+2,c->argc-2,c->argv[1]);
}

#define REDIS_OP_UNION 0
#define REDIS_OP_DIFF 1
#define REDIS_OP_INTER 2

//处理并集实际操作函数
//setkeys所有keys
//setnum表示keys的个数
//dstkey表示是否传递一个存储结果集合的指针
//op可以为REDIS_OP_DIFF、REDIS_OP_UNION
void sunionDiffGenericCommand(redisClient *c, robj **setkeys, int setnum, robj *dstkey, int op) {
    robj **sets = zmalloc(sizeof(robj*)*setnum);
    setTypeIterator *si;
    robj *ele, *dstset = NULL;
    int j, cardinality = 0;
    int diff_algo = 1;

	//所有结果用sets存储，如果为空，存储NULL
    for (j = 0; j < setnum; j++) {
        robj *setobj = dstkey ?
            lookupKeyWrite(c->db,setkeys[j]) :
            lookupKeyRead(c->db,setkeys[j]);
        if (!setobj) {
            sets[j] = NULL;
            continue;
        }
        if (checkType(c,setobj,REDIS_SET)) {
            zfree(sets);
            return;
        }
        sets[j] = setobj;
    }

    /* Select what DIFF algorithm to use.
     *
     * Algorithm 1 is O(N*M) where N is the size of the element first set
     * and M the total number of sets.
     *
     * Algorithm 2 is O(N) where N is the total number of elements in all
     * the sets.
     *
     * We compute what is the best bet with the current input here. */
	//求差集
	//依据待运算集合中元素数量,选择计算差集算法, 其中算法1时间复杂度:O(N*M), N是第一个集合中元素个数, M是参与运算的集合数量.
	//算法2时间复杂度:O(K), K是所有集合中元素数量总和
    if (op == REDIS_OP_DIFF && sets[0]) {
        long long algo_one_work = 0, algo_two_work = 0;

		//总长度
        for (j = 0; j < setnum; j++) {
            if (sets[j] == NULL) continue;

            algo_one_work += setTypeSize(sets[0]);
            algo_two_work += setTypeSize(sets[j]);
        }

        /* Algorithm 1 has better constant times and performs less operations
         * if there are elements in common. Give it some advantage. */
		//algo_one_work值即为算法1中N*M, algo_two_work值即为算法2中K. 考虑到如果参与运算集合为intset时, 算法1的时间复杂度稳定性要好于算法2,
        //因此没有直接比较两者大小选择算法, 而是算法1理论时间复杂度一半大于算法2时, 才使用算法2
        algo_one_work /= 2;
        diff_algo = (algo_one_work <= algo_two_work) ? 1 : 2;

        if (diff_algo == 1 && setnum > 1) {
            /* With algorithm 1 it is better to order the sets to subtract
             * by decreasing size, so that we are more likely to find
             * duplicated elements ASAP. */
            qsort(sets+1,setnum-1,sizeof(robj*),
                qsortCompareSetsByRevCardinality);
        }
    }

    /* We need a temp set object to store our union. If the dstkey
     * is not NULL (that is, we are inside an SUNIONSTORE operation) then
     * this set object will be the resulting object to set into the target key*/
    dstset = createIntsetObject();

	//并集
    if (op == REDIS_OP_UNION) {
        /* Union is trivial, just add every element of every set to the
         * temporary set. */
        for (j = 0; j < setnum; j++) {
            if (!sets[j]) continue; /* non existing keys are like empty sets */

            si = setTypeInitIterator(sets[j]);
            while((ele = setTypeNextObject(si)) != NULL) {
                if (setTypeAdd(dstset,ele)) cardinality++;
                decrRefCount(ele);
            }
            setTypeReleaseIterator(si);
        }
	//差集，选择算法1，复杂度O(N*M)
	//遍历sets[0]，如果1至setnum中都没有，则添加进差集合中
    } else if (op == REDIS_OP_DIFF && sets[0] && diff_algo == 1) {
        /* DIFF Algorithm 1:
         *
         * We perform the diff by iterating all the elements of the first set,
         * and only adding it to the target set if the element does not exist
         * into all the other sets.
         *
         * This way we perform at max N*M operations, where N is the size of
         * the first set, and M the number of sets. */
        si = setTypeInitIterator(sets[0]);
        while((ele = setTypeNextObject(si)) != NULL) {
            for (j = 1; j < setnum; j++) {
                if (!sets[j]) continue; /* no key is an empty set. */
                if (sets[j] == sets[0]) break; /* same set! */
                if (setTypeIsMember(sets[j],ele)) break;
            }
            if (j == setnum) {
                /* There is no other set with this element. Add it. */
                setTypeAdd(dstset,ele);
                cardinality++;
            }
            decrRefCount(ele);
        }
        setTypeReleaseIterator(si);
	//差集，选择算法2，复杂度O(K)，K为所有元素的个数
	//遍历所有元素，如果是sets[0]中，直接添加进差集合中，
	//否则从差集合中删除该元素(如果存在)
    } else if (op == REDIS_OP_DIFF && sets[0] && diff_algo == 2) {
        /* DIFF Algorithm 2:
         *
         * Add all the elements of the first set to the auxiliary set.
         * Then remove all the elements of all the next sets from it.
         *
         * This is O(N) where N is the sum of all the elements in every
         * set. */
        for (j = 0; j < setnum; j++) {
            if (!sets[j]) continue; /* non existing keys are like empty sets */

            si = setTypeInitIterator(sets[j]);
            while((ele = setTypeNextObject(si)) != NULL) {
                if (j == 0) {
                    if (setTypeAdd(dstset,ele)) cardinality++;
                } else {
                    if (setTypeRemove(dstset,ele)) cardinality--;
                }
                decrRefCount(ele);
            }
            setTypeReleaseIterator(si);

            /* Exit if result set is empty as any additional removal
             * of elements will have no effect. */
            if (cardinality == 0) break;
        }
    }

    /* Output the content of the resulting set, if not in STORE mode */
	
    if (!dstkey) {
		//运算结果不需要存储,直接返回结果元素至客户端
        addReplyMultiBulkLen(c,cardinality);
        si = setTypeInitIterator(dstset);
        while((ele = setTypeNextObject(si)) != NULL) {
            addReplyBulk(c,ele);
            decrRefCount(ele);
        }
        setTypeReleaseIterator(si);
        decrRefCount(dstset);
    } else {
        /* If we have a target key where to store the resulting set
         * create this key with the result set inside */
		//需要存储, 首先删除原来可能已经存在dstkey的集合
        int deleted = dbDelete(c->db,dstkey);
        if (setTypeSize(dstset) > 0) {
            dbAdd(c->db,dstkey,dstset);
            addReplyLongLong(c,setTypeSize(dstset));
            notifyKeyspaceEvent(REDIS_NOTIFY_SET,
                op == REDIS_OP_UNION ? "sunionstore" : "sdiffstore",
                dstkey,c->db->id);
        } else {
            decrRefCount(dstset);
            addReply(c,shared.czero);
            if (deleted)
                notifyKeyspaceEvent(REDIS_NOTIFY_GENERIC,"del",
                    dstkey,c->db->id);
        }
        signalModifiedKey(c->db,dstkey);
        server.dirty++;
    }
    zfree(sets);
}

//SUNION KEY KEY1..KEYN
//返回给定集合的并集。不存在的集合 key 被视为空集。
void sunionCommand(redisClient *c) {
    sunionDiffGenericCommand(c,c->argv+1,c->argc-1,NULL,REDIS_OP_UNION);
}


//SUNIONSTORE DESTINATION KEY KEY1..KEYN
//将给定集合的并集存储在指定的集合 destination 中。如果 destination 已经存在，则将其覆盖
void sunionstoreCommand(redisClient *c) {
    sunionDiffGenericCommand(c,c->argv+2,c->argc-2,c->argv[1],REDIS_OP_UNION);
}

//SDIFF FIRST_KEY OTHER_KEY1..OTHER_KEYN 
//返回给定集合之间的差集。不存在的集合 key 将视为空集。
void sdiffCommand(redisClient *c) {
    sunionDiffGenericCommand(c,c->argv+1,c->argc-1,NULL,REDIS_OP_DIFF);
}

//SDIFFSTORE DESTINATION_KEY KEY1..KEYN 
//将给定集合之间的差集存储在指定的集合中。如果指定的集合 key 已存在，则会被覆盖。
void sdiffstoreCommand(redisClient *c) {
    sunionDiffGenericCommand(c,c->argv+2,c->argc-2,c->argv[1],REDIS_OP_DIFF);
}

//SSCAN key cursor [MATCH pattern] [COUNT count]
//迭代集合中键的元素
void sscanCommand(redisClient *c) {
    robj *set;
    unsigned long cursor;

    if (parseScanCursorOrReply(c,c->argv[2],&cursor) == REDIS_ERR) return;
    if ((set = lookupKeyReadOrReply(c,c->argv[1],shared.emptyscan)) == NULL ||
        checkType(c,set,REDIS_SET)) return;
    scanGenericCommand(c,set,cursor);
}
