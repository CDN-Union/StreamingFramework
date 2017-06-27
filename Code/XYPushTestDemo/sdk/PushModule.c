//
//  PushModule.c
//  rtmpdump_test
//
//  Created by 李雪岩 on 2017/4/26.
//  Copyright © 2017年 hongduoxing. All rights reserved.
//

#include "PushModule.h"
#include "rtmp.h"
#include <string.h>
#include "rtmp.h"
#include "xylive_push_sdk_c.h"

extern push_module_t xypush_module;
extern push_module_t rtmppush_module;

/* 定义所有模块 */
push_module_t *global_modules[] = {
    &rtmppush_module,
    &xypush_module
    /* 其他厂商的模块加在这里即可 */
};



/*导入所有模块*/
int
expore_all_module(char *negotiate)
{
    int end = sizeof(global_modules)/sizeof(global_modules[0]);
    int i;
    for(i = 0; i < end; i++) {
        strcat(negotiate, global_modules[i]->module_name);
        if(i < end-1)
            strcat(negotiate, ",");
        PILI_RTMP_Log(PILI_RTMP_LOGDEBUG, "export module name=[%s].\n",global_modules[i]->module_name);
    }
    PILI_RTMP_Log(PILI_RTMP_LOGDEBUG, "negotiate: %s\n",negotiate);
    return 0;
    
}

#define FLV_HEADER_SIZE 11

int
rtmp_packet_to_flv(PILI_RTMPPacket *packet, char *flv_tag, int tag_size)
{
    if(tag_size != (1+3+3+1+3+packet->m_nBodySize+4)) {
        return FALSE;
    }
    uint32_t pre_size = tag_size;

    memcpy(flv_tag, &(packet->m_packetType), sizeof(packet->m_packetType));/*type*/
    PILI_RTMP_to_big_endian(flv_tag+1, &packet->m_nBodySize, 3, 4); /*datalen*/
    PILI_RTMP_to_big_endian(flv_tag+1+3, &packet->m_nTimeStamp, 3, 4); /*timestamp3 + extra1*/
    memcpy(flv_tag+1+3+3, (&packet->m_nTimeStamp)+3, 1);
    memset(flv_tag+1+3+4, 0, 3); /*stream id  always 0*/
    memcpy(flv_tag+FLV_HEADER_SIZE, (packet->m_body), packet->m_nBodySize); /*body*/
    PILI_RTMP_to_big_endian(flv_tag+(FLV_HEADER_SIZE+packet->m_nBodySize), &pre_size, 4, 4); /*timestamp3 + extra1*/

    return TRUE;
}

/* 根据服务器返回选择传输模块 */
push_module_t *
select_module(PILI_AVal *negotiate)
{
    int i;
    
    for (i = 0; i < sizeof(global_modules)/sizeof(global_modules[0]); ++i) {
        
        if(strncmp(global_modules[i]->module_name, negotiate->av_val, negotiate->av_len) == 0)
        {
            PILI_RTMP_Log(PILI_RTMP_LOGINFO, "Get module [%s].", global_modules[i]->module_name);
            return global_modules[i];
        }
        
    }
    return &rtmppush_module;
}

/* 定义星域推流模块 */
static int xypush_module_init(void *arg, void *err);
static int xypush_module_release(void *arg);
static int xypush_module_push(void*, void*, uint32_t, void*);

//static struct XYPushSession *s = NULL;

push_module_t xypush_module =
{
    "XYPushModule",
    xypush_module_init,
    xypush_module_release,
    xypush_module_push
};


static struct XYPushSession *s =NULL;
int xypush_module_init(void *arg, void *err)
{
    PILI_RTMP *r = (PILI_RTMP*)arg;
   
    char pushUrl[1024] = "rtmp://42.51.169.175:3005/live.test.com/live/stream1";
//    strcat(pushUrl, r->Link.tcUrl.av_val+7);
    PILI_RTMP_Log(PILI_RTMP_LOGDEBUG, "pushUrl: %s", pushUrl);
    s = XYPushSession_alloc();
    if(NULL == s)
    {
        goto err;
    }
    
    if(XYPushSession_connect(s, pushUrl, 3000))
    {
        PILI_RTMP_Log(PILI_RTMP_LOGERROR, "push session connect failed.");
        goto err;
    }
   
    return TRUE;
   
    err:
    
        if(s) {
            XYPushSession_close(s);
            XYPushSession_release(s);
        }
        return FALSE;
}

int xypush_module_release(void *arg)
{
    XYPushSession_close(s);
    XYPushSession_release(s);
    return 0;
}

int xypush_module_push(void *rtmp, void *buf, uint32_t size, void *err)
{
    int ret;
    ret = XYPushSession_push(s, (uint8_t*)buf, size);
    return (ret == 0);
}


/* 定义rtmp默认推流模块 */
static int rtmp_module_init(void *arg, void *err);
static int rtmp_module_release(void *arg);
static int rtmp_module_push(void*, void*, uint32_t, void*);

push_module_t rtmppush_module =
{
    "RTMPPushModule",
    rtmp_module_init,
    rtmp_module_release,
    rtmp_module_push
};


//return TRUE for ok, FALSE or other for err;
int rtmp_module_init(void *arg, void *err)
{
    return PILI_RTMP_ConnectStream_Module(arg, err);
}
int rtmp_module_release(void *arg)
{
    return 0;
}


int rtmp_module_push(void* rtmp, void* buf, uint32_t size, void* err)
{
    return PILI_RTMP_Write_Module(rtmp, buf, size, err);
}

