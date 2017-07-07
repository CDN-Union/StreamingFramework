//
//  Rtmp_PushModule.h
//  XYPushTestDemo
//
//  Created by 李雪岩 on 2017/7/7.
//  Copyright © 2017年 hongduoxing. All rights reserved.
//

#ifndef Rtmp_PushModule_h
#define Rtmp_PushModule_h

#include <stdio.h>


#include "PushModule.h"
#include "rtmp.h"
#include "amf.h"
#include "log.h"



#endif /* Rtmp_PushModule_h */

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

