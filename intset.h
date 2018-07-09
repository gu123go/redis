/*
 * Copyright (c) 2009-2012, Pieter Noordhuis <pcnoordhuis at gmail dot com>
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

#ifndef __INTSET_H
#define __INTSET_H
#include <stdint.h>

//有序set
//使用一段连续的内存空间存储一段数据

typedef struct intset {
	////编码的方式 分为int16 int32 int64
    uint32_t encoding;
	//数据的个数
    uint32_t length;
	//存储数据的点。int8_t与char 所用的字节数相同
    int8_t contents[];
} intset;

//创建一个新的intset结构
//encoding默认为int16
//length为0
intset *intsetNew(void);
//在is中添加value，如果成功*success为1，否则为0
//返回新的intset
intset *intsetAdd(intset *is, int64_t value, uint8_t *success);

//从is中删除value，如果删除成功*success为1，否则为0
intset *intsetRemove(intset *is, int64_t value, int *success);
//查找value，找到返回1，否则返回0
uint8_t intsetFind(intset *is, int64_t value);
//返回一个随机的值
int64_t intsetRandom(intset *is);
//返回pos位置的值，用*value存储
//如果成功返回1，否则返回0
uint8_t intsetGet(intset *is, uint32_t pos, int64_t *value);
//返回长度
uint32_t intsetLen(intset *is);
//is结构总长度，包括encoding，length和contents
size_t intsetBlobLen(intset *is);

#endif // __INTSET_H
