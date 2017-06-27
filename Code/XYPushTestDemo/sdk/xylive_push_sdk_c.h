//
//  xylive_push_sdk_c.h
//  xy_living_push_sdk
//
//  Created by hongduoxing on 16/11/25.
//  Copyright © 2016年 hongduoxing. All rights reserved.
//

#ifndef xylive_push_sdk_c_h
#define xylive_push_sdk_c_h

#include <stdio.h>
#include <stdint.h>


#ifdef __cplusplus
extern "C" {
#endif


/*
 星域推流SDK支持rtmp server模式和自定义输入模式
 
 rtmp server模式
 SDK在本地启用精简的rtmp代理服务器,开发者需要
 通过rewritePushUrl接口重写推流地址,将流推到
 本地rtmp代理服务器，服务器以优化的传输协议转推
 出去。此模式接入非常简单,开发者只要调用接口重写
 地址，然后用重写后的地址推流即可,不会影响到开发
 者app的逻辑。
 
 
 自定义输入模式
 自定义输入模式通过XYPushSession结构实现，开发
 者需要通过push接口输入flv tag数据完成
 推流
 */

    
/*------rtmp server模式接口-----*/

/*
 初始化SDK的rtmp server
 在应用程序启动或恢复到前台时调用
 */
int XYLiveSDK_init();

/*
 终止SDK的rtmp server
 应用程序退出或进入后台时调用
 */
int XYLiveSDK_release();
    
/*
 重写推流地址
 参数:
 url: 原始的推流地址
 buf: 保存重写后推流地址的buf
 bufLen: buf的最大长度
 */
int XYLiveSDK_rewrite(const char *url, char *buf, uint32_t bufLen);

    
    
/*-----以下为自定义模式接口-----*/
    
struct XYPushSession;

/*
 推流码率回调函数
 */
typedef int (*XYPushSessionCallBackFunc)(void *, int);


/*
 初始化一个XYPushSession结构
 */
struct XYPushSession *XYPushSession_alloc();


/*
 释放一个XYPushSession结构
 返回值:
 0 成功
 < 0 失败
 */
int XYPushSession_release(struct XYPushSession *s);
    
/*
 设置SDK的回调函数和userData
 */
int XYPushSession_setCallBack(struct XYPushSession *s, XYPushSessionCallBackFunc func, void *userData);

/*
 发起推流连接
 参数:
 rtmpUrl: rtmp推流地址
 timeout: connect超时时长,单位毫秒
 返回值:
 0 成功
 < 0 失败
 */
int XYPushSession_connect(struct XYPushSession *s, const char *rtmpUrl, int timeout);

/*
 发送flv tag数据
 参数:
 tag: flv tag数据指针
 tagSize: tag长度,单位字节
 返回值:
 0 成功
 < 0 失败
 */
int XYPushSession_push(struct XYPushSession *s, uint8_t *flvTag, uint32_t tagLen);

/*
 关闭推流连接
 返回值:
 0 成功
 < 0 失败
 */
int XYPushSession_close(struct XYPushSession *s);

/*
 获取当前发送队列中待发送数据的总长度
 返回值:
 总大小,单位字节
 */
int XYPushSession_getSendQueueLen(struct XYPushSession *s);

/*
 获取当前会话连接的往返时延
 返回值:
 总大小,单位毫秒
 */
int XYPushSession_getCurrentRtt(struct XYPushSession *s);

/*
 获取当前会话连接的丢包率
 返回值:
 丢包百分比
 */
double XYPushSession_getCurrentLoss(struct XYPushSession *s);
 

#ifdef __cplusplus
}
#endif

#endif /* xylive_push_sdk_c_h */
