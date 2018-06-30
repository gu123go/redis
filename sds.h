/* SDSLib, A C dynamic strings library
 *
 * Copyright (c) 2006-2010, Salvatore Sanfilippo <antirez at gmail dot com>
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

#ifndef __SDS_H
#define __SDS_H

#define SDS_MAX_PREALLOC (1024*1024)

#include <sys/types.h>
#include <stdarg.h>

typedef char *sds;

//字符串类型结构
//char buf[]不能换成 char *buf
//该结构在64位机器上面sizeof结果是8，char buf[]换成 char *buf结果为16
struct sdshdr {
// buf 中已使用的长度
    unsigned int len;
	
// buf 中剩余可使用的长度
    unsigned int free;
	
// 柔性数组，实际字符串地址
    char buf[];
};

//struct sdshdr + buf length + 1
//结构体大小，buf空间大小，\0
static inline size_t sdslen(const sds s) {
    struct sdshdr *sh = (void*)(s-(sizeof(struct sdshdr)));
    return sh->len;
}

static inline size_t sdsavail(const sds s) {
    struct sdshdr *sh = (void*)(s-(sizeof(struct sdshdr)));
    return sh->free;
}

//通过init指针指向的字符串创建一个新的字符串并返回指向新串的指针
sds sdsnewlen(const void *init, size_t initlen);
//通过init创建新串，调用sdsnewlen()
sds sdsnew(const char *init);
//创建一个长度为0的空串
sds sdsempty(void);
//返回buffer s的长度
size_t sdslen(const sds s);
//复制buffer s，并返回新串
sds sdsdup(const sds s);

//释放buffer s，包括sdshdr结构体空间，还有记录空间大小的PREFIX_SIZE空间
void sdsfree(sds s);

//返回可用空间大小
size_t sdsavail(const sds s);

//将sds扩充至指定长度，末尾未使用的空间以0填充,如果长度够（free+len > len)，当len小于原来的长度，什么也不做
//sh->len不会改变，只改变free的长度
//sh->len长度sh->buf + len-sh->len长度0 + sh->free+len-sh->len长度任意字符
sds sdsgrowzero(sds s, size_t len);
//将字符串t的前len个字节填充到s的末尾
sds sdscatlen(sds s, const void *t, size_t len);
//将字符串t填充到s的末尾
sds sdscat(sds s, const char *t);

sds sdscatsds(sds s, const sds t);

//将t的前len个字符拷贝到s上，也就是会覆盖s的内容
sds sdscpylen(sds s, const char *t, size_t len);
// 将t的内容拷贝到s上
sds sdscpy(sds s, const char *t);
//通过fmt指定个格式来格式化字符串
sds sdscatvprintf(sds s, const char *fmt, va_list ap);
#ifdef __GNUC__
//调用sdscatvprintf
sds sdscatprintf(sds s, const char *fmt, ...)
//使编译器检查函数声明和函数实际调用参数之间的格式化字符串是否匹配。
//format属性告诉编译器，按照printf, scanf等标准C函数参数格式规则对该函数的参数进行检查。
//format (archetype, string-index, first-to-check)
//“archetype”指定是哪种风格；
//“string-index”指定传入函数的第几个参数是格式化字符串；
//“first-to-check”指定从函数的第几个参数开始按上述规则进行检查。
    __attribute__((format(printf, 2, 3)));
#else
sds sdscatprintf(sds s, const char *fmt, ...);
#endif

//和sdscatprintf功能一样，速度更快
sds sdscatfmt(sds s, char const *fmt, ...);

//对s的左右两端进行裁剪，去掉cset指定的字符
sds sdstrim(sds s, const char *cset);

//通过索引区间[start,end]来截取字符串
void sdsrange(sds s, int start, int end);
//根据字符串所占用空间的长度大小来更新len、free（内部有\0的情况）
void sdsupdatelen(sds s);
//将字符串的第一个字符串置为'\0'，也就是把字符串置为空字符串，但是没有释放空间
void sdsclear(sds s);

//比较两个sds是否相等(字符串比较)
int sdscmp(const sds s1, const sds s2);
//分割，len总长度，seplen分隔符长度，*count返回切割个数
//默认分配的数组为5， 然后按照2的倍数进行增长， 这样做法，有点浪费空间，但是加快速度，不要每分割出来一个字符串就要申请空间。 
//比较的时候把seplen为1分出来， 也是加快字符串比较速度的考虑， 大部分时候应该是seplen为1。
//不必担心内存泄漏，开始都会有个头存储了空间大小
//  		        		len
//							free
//tokens --> tokens[0] -->  (sds) buf
//							len
//							free
//		     tokens[1] -->  (sds) buf
//tokens 指向一个一维数组，包含了*count个sds结构，每个sds里面存放的是切割的结果，其中，每个sds结构都有一个sdshdr结构体头
sds *sdssplitlen(const char *s, int len, const char *sep, int seplen, int *count);
//释放数组tokens的*count个sds,其中也包含sdshdr头
void sdsfreesplitres(sds *tokens, int count);

//tolower
void sdstolower(sds s);
//toupper
void sdstoupper(sds s);
//long long 类型转换sds
//调用sdsll2str
sds sdsfromlonglong(long long value);
//将长度为len的字符串p以带引号的格式追加到s的末尾
//如 s = "abc" , p = "gbdf\n134"; 那么函数的返回结果为 ret = "abc\"gbdf\\n134\""
//调整成转义字符可以输出
sds sdscatrepr(sds s, const char *p, size_t len);
//将一行文本分割成多个参数，参数的个数存在argc
sds *sdssplitargs(const char *line, int *argc);
// 将字符串s中，出现存在from中指定的字符，都转换成to中的字符，from与to是有位置关系，
// 假如from = "ckj", to = "345", 那么‘c’就换成‘3’, 'k'就换成‘4’, 以此类推
sds sdsmapchars(sds s, const char *from, const char *to, size_t setlen);
 //通过分隔符sep把字符数组argv拼接成一个字符串（和sdssplitlen互为相反操作）
sds sdsjoin(char **argv, int argc, char *sep);

/* Low level functions exposed to the user API */
//对字符串进行扩充
//addlen小于1M空间，扩大1倍，大于1M，增加1M空间
sds sdsMakeRoomFor(sds s, size_t addlen);

//在不重新分配空间的基础上，给字符串增加incr长度
//直接修改len、free的值，并设置buf[sh->len]=0
void sdsIncrLen(sds s, int incr);
////回收sds剩余的空间内容，但是不会修改字符串的内容
sds sdsRemoveFreeSpace(sds s);
//返回给s分配的内存的字节数
//包括指针大小
size_t sdsAllocSize(sds s);

#endif
