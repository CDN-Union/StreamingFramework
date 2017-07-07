//
//  Rtmp_PushModule.c
//  XYPushTestDemo
//
//  Created by 李雪岩 on 2017/7/7.
//  Copyright © 2017年 hongduoxing. All rights reserved.
//

#include "Rtmp_PushModule.h"


/* ------------------ */
/* RTMP模块 --初始化--  */
/* ------------------ */
//return TRUE for ok, FALSE or other for err;
int rtmp_module_init(void *arg, void *err)
{
    return PILI_RTMP_ConnectStream_Module(arg, err);
}


/* ----------------------- */
/*   RTMP模块 --release--   */
/* ----------------------- */

int rtmp_module_release(void *arg)
{
    return 0;
}

/* ------------------ */
/* RTMP模块  --传输--   */
/* ------------------ */
int rtmp_module_push(void* rtmp, void* buf, uint32_t size, void* err)
{
    return PILI_RTMP_Write_Module(rtmp, buf, size, err);
}

