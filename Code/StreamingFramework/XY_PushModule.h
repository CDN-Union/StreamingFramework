//
//  XY_PushModule.h
//  XYPushTestDemo
//
//  Created by 李雪岩 on 2017/7/7.
//  Copyright © 2017年 hongduoxing. All rights reserved.
//

#ifndef XY_PushModule_h
#define XY_PushModule_h

#include <stdio.h>
#include "PushModule.h"
#include "rtmp.h"
#include "amf.h"
#include "log.h"
#include "xylive_push_sdk_c.h"
#include <string.h>

/* 定义星域推流模块 */
int xypush_module_init(void *arg, void *err);
int xypush_module_release(void *arg);
int xypush_module_push(void*, void*, uint32_t, void*);



push_module_t xypush_module =
{
    "XYPushModule",
    xypush_module_init,
    xypush_module_release,
    xypush_module_push
};



#endif /* XY_PushModule_h */
