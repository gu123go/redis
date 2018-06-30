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

//�ַ������ͽṹ
//char buf[]���ܻ��� char *buf
//�ýṹ��64λ��������sizeof�����8��char buf[]���� char *buf���Ϊ16
struct sdshdr {
// buf ����ʹ�õĳ���
    unsigned int len;
	
// buf ��ʣ���ʹ�õĳ���
    unsigned int free;
	
// �������飬ʵ���ַ�����ַ
    char buf[];
};

//struct sdshdr + buf length + 1
//�ṹ���С��buf�ռ��С��\0
static inline size_t sdslen(const sds s) {
    struct sdshdr *sh = (void*)(s-(sizeof(struct sdshdr)));
    return sh->len;
}

static inline size_t sdsavail(const sds s) {
    struct sdshdr *sh = (void*)(s-(sizeof(struct sdshdr)));
    return sh->free;
}

//ͨ��initָ��ָ����ַ�������һ���µ��ַ���������ָ���´���ָ��
sds sdsnewlen(const void *init, size_t initlen);
//ͨ��init�����´�������sdsnewlen()
sds sdsnew(const char *init);
//����һ������Ϊ0�Ŀմ�
sds sdsempty(void);
//����buffer s�ĳ���
size_t sdslen(const sds s);
//����buffer s���������´�
sds sdsdup(const sds s);

//�ͷ�buffer s������sdshdr�ṹ��ռ䣬���м�¼�ռ��С��PREFIX_SIZE�ռ�
void sdsfree(sds s);

//���ؿ��ÿռ��С
size_t sdsavail(const sds s);

//��sds������ָ�����ȣ�ĩβδʹ�õĿռ���0���,������ȹ���free+len > len)����lenС��ԭ���ĳ��ȣ�ʲôҲ����
//sh->len����ı䣬ֻ�ı�free�ĳ���
//sh->len����sh->buf + len-sh->len����0 + sh->free+len-sh->len���������ַ�
sds sdsgrowzero(sds s, size_t len);
//���ַ���t��ǰlen���ֽ���䵽s��ĩβ
sds sdscatlen(sds s, const void *t, size_t len);
//���ַ���t��䵽s��ĩβ
sds sdscat(sds s, const char *t);

sds sdscatsds(sds s, const sds t);

//��t��ǰlen���ַ�������s�ϣ�Ҳ���ǻḲ��s������
sds sdscpylen(sds s, const char *t, size_t len);
// ��t�����ݿ�����s��
sds sdscpy(sds s, const char *t);
//ͨ��fmtָ������ʽ����ʽ���ַ���
sds sdscatvprintf(sds s, const char *fmt, va_list ap);
#ifdef __GNUC__
//����sdscatvprintf
sds sdscatprintf(sds s, const char *fmt, ...)
//ʹ��������麯�������ͺ���ʵ�ʵ��ò���֮��ĸ�ʽ���ַ����Ƿ�ƥ�䡣
//format���Ը��߱�����������printf, scanf�ȱ�׼C����������ʽ����Ըú����Ĳ������м�顣
//format (archetype, string-index, first-to-check)
//��archetype��ָ�������ַ��
//��string-index��ָ�����뺯���ĵڼ��������Ǹ�ʽ���ַ�����
//��first-to-check��ָ���Ӻ����ĵڼ���������ʼ������������м�顣
    __attribute__((format(printf, 2, 3)));
#else
sds sdscatprintf(sds s, const char *fmt, ...);
#endif

//��sdscatprintf����һ�����ٶȸ���
sds sdscatfmt(sds s, char const *fmt, ...);

//��s���������˽��вü���ȥ��csetָ�����ַ�
sds sdstrim(sds s, const char *cset);

//ͨ����������[start,end]����ȡ�ַ���
void sdsrange(sds s, int start, int end);
//�����ַ�����ռ�ÿռ�ĳ��ȴ�С������len��free���ڲ���\0�������
void sdsupdatelen(sds s);
//���ַ����ĵ�һ���ַ�����Ϊ'\0'��Ҳ���ǰ��ַ�����Ϊ���ַ���������û���ͷſռ�
void sdsclear(sds s);

//�Ƚ�����sds�Ƿ����(�ַ����Ƚ�)
int sdscmp(const sds s1, const sds s2);
//�ָlen�ܳ��ȣ�seplen�ָ������ȣ�*count�����и����
//Ĭ�Ϸ��������Ϊ5�� Ȼ����2�ı������������� �����������е��˷ѿռ䣬���Ǽӿ��ٶȣ���Ҫÿ�ָ����һ���ַ�����Ҫ����ռ䡣 
//�Ƚϵ�ʱ���seplenΪ1�ֳ����� Ҳ�Ǽӿ��ַ����Ƚ��ٶȵĿ��ǣ� �󲿷�ʱ��Ӧ����seplenΪ1��
//���ص����ڴ�й©����ʼ�����и�ͷ�洢�˿ռ��С
//  		        		len
//							free
//tokens --> tokens[0] -->  (sds) buf
//							len
//							free
//		     tokens[1] -->  (sds) buf
//tokens ָ��һ��һά���飬������*count��sds�ṹ��ÿ��sds�����ŵ����и�Ľ�������У�ÿ��sds�ṹ����һ��sdshdr�ṹ��ͷ
sds *sdssplitlen(const char *s, int len, const char *sep, int seplen, int *count);
//�ͷ�����tokens��*count��sds,����Ҳ����sdshdrͷ
void sdsfreesplitres(sds *tokens, int count);

//tolower
void sdstolower(sds s);
//toupper
void sdstoupper(sds s);
//long long ����ת��sds
//����sdsll2str
sds sdsfromlonglong(long long value);
//������Ϊlen���ַ���p�Դ����ŵĸ�ʽ׷�ӵ�s��ĩβ
//�� s = "abc" , p = "gbdf\n134"; ��ô�����ķ��ؽ��Ϊ ret = "abc\"gbdf\\n134\""
//������ת���ַ��������
sds sdscatrepr(sds s, const char *p, size_t len);
//��һ���ı��ָ�ɶ�������������ĸ�������argc
sds *sdssplitargs(const char *line, int *argc);
// ���ַ���s�У����ִ���from��ָ�����ַ�����ת����to�е��ַ���from��to����λ�ù�ϵ��
// ����from = "ckj", to = "345", ��ô��c���ͻ��ɡ�3��, 'k'�ͻ��ɡ�4��, �Դ�����
sds sdsmapchars(sds s, const char *from, const char *to, size_t setlen);
 //ͨ���ָ���sep���ַ�����argvƴ�ӳ�һ���ַ�������sdssplitlen��Ϊ�෴������
sds sdsjoin(char **argv, int argc, char *sep);

/* Low level functions exposed to the user API */
//���ַ�����������
//addlenС��1M�ռ䣬����1��������1M������1M�ռ�
sds sdsMakeRoomFor(sds s, size_t addlen);

//�ڲ����·���ռ�Ļ����ϣ����ַ�������incr����
//ֱ���޸�len��free��ֵ��������buf[sh->len]=0
void sdsIncrLen(sds s, int incr);
////����sdsʣ��Ŀռ����ݣ����ǲ����޸��ַ���������
sds sdsRemoveFreeSpace(sds s);
//���ظ�s������ڴ���ֽ���
//����ָ���С
size_t sdsAllocSize(sds s);

#endif
