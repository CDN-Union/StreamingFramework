//
//  XY_PushModule.c
//  XYPushTestDemo
//
//  Created by 李雪岩 on 2017/7/7.
//  Copyright © 2017年 hongduoxing. All rights reserved.
//

#include "XY_PushModule.h"


/* ----------------- */
/* 星域模块 --初始化--  */
/* ----------------- */
static int xypush_module_init(void *arg, void *err);
static int xypush_module_release(void *arg);
static int xypush_module_push(void*, void*, uint32_t, void*);

push_module_t xypush_module =
{
    "XYPushModule",
    xypush_module_init,
    xypush_module_release,
    xypush_module_push
};

static struct XYPushSession *s = NULL;

int xypush_module_init(void *arg, void *err)
{
    char pushUrl[1024] = {0};
    PILI_RTMP *r = (PILI_RTMP*)arg;

    strcpy(pushUrl, "rtmp://42.51.169.175:3080/live.test.com/live/stream1");
    strcat(pushUrl, r->Link.tcUrl.av_val+7);
    PILI_RTMP_Log(PILI_RTMP_LOGDEBUG, "pushUrl: %s", pushUrl);
    
    s = XYPushSession_alloc();
    if(NULL == s) {
        goto err;
    }
    if(XYPushSession_connect(s, pushUrl, 3000)) {
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

/* ------------------- */
/*   星域模块 --传输--   */
/* ------------------- */
int xypush_module_push(void *rtmp, void *buf, uint32_t size, void *err)
{
    int ret;
    ret = XYPushSession_push(s, (uint8_t*)buf, size);
    return (ret == 0);
}



/* ------------------- */
/* 星域模块 --release--  */
/* ------------------- */
int xypush_module_release(void *arg)
{
    XYPushSession_close(s);
    XYPushSession_release(s);
    return 0;
}
