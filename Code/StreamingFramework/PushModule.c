//
//  PushModule.c
//  rtmpdump_test
//
//  Created by 李雪岩 on 2017/4/26.
//  Copyright © 2017年 hongduoxing. All rights reserved.
//

#include <string.h>

#include "PushModule.h"
#include "rtmp.h"

/*引入模块头文件*/
#include "Rtmp_PushModule.h"
#include "XY_PushModule.h"

extern push_module_t xypush_module;
extern push_module_t rtmppush_module;

/* 定义所有模块，优先级高的在前 */
push_module_t *global_modules[] = {
    &rtmppush_module,
    &xypush_module,
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

