# StreamingFramework

Maintainer: 星域   
Members：星域，网宿，七牛   

## Introduction

下一代直播CDN在推流这块仍使用RTMP作为上层协议，但对一些细节做了更详细的规定（例如要求加入延时计算所需的信息），因此七牛公开了一个符合上述规范的librtmp作为标准实现。而融合各厂商的私有传输协议也是下一代直播CDN组织的一个重要目标，因此需要基于上述librtmp标准实现发展出一个可以集成私有传输协议的框架。

推流框架下挂若干传输模块，每个传输模块对应一种传输协议（例如RTMP）。推流框架对应用层提供标准的librtmp接口，经过与Server端能力协商选用合适的传输方式进行传输交互。RTMP模块为默认模块，若Server端不支持能力协商或不支持SDK的第三方模块，则使用RTMP模块作为保底。

能力协商细节详见内附文档。RTMP Server端需要修改以支持能力协商

### 传输模块接口
通用的传输模块结构体，开发者可以定义自己的传输模块

结构体定义见PushModule.h

    typedef struct push_module_s {
        const char *module_name;
        
         /* (PILI_RTMP*, RTMP_Error*) */
        int (*init)(void*, void*);
        
        /* PILI_RTMP* */
        int (*release)(void*);
       
        /* PILI_RTMP*, const char*, int, RTMP_Error* */
        int (*push_message_push)(void *rtmp, void *buf, uint32_t size, void *err);        
    } push_module_t;

### 回调说明
    int (*init)(void*);

模块初始化，建立连接的过程应该包含在此函数内部;

传入参数为RTMP*

    int(*release)(void*);

模块的析构函数，推流结束时调用，内部应实现释放文件描述符清理内存的相关操作

传入参数为RTMP*
    
    int (*push_message_push)(void *rtmp, void *buf, uint32_t size, void *err);
数据推送接口，传入flv tag数据。模块内部为私有协议的主体部分，通过私有协议实现数据的发送。

传入参数为 RTMP*, const char*（实际数据）, uint32_t（数据长度）, void*（接收error的回调）。

### 传输模块注册
见PushModule.c

    push_module_t *global_modules[] = {
        &rtmppush_module,
        &examplepush_module
        /* 新增模块加在这里即可 */
    };

global_modules[]定义的全局变量，存储所有定义的模块，优先级从上至下依次减小。

### 模块开发示例
* 在模块代码内实例化模块结构体：

```
    push_module_t xypush_module = {
        "XYPushModule",  /* 此字串将放在negotiate字段中代表该模块 */
        xypush_module_init,
        xypush_module_release,
        xypush_module_push
    };
```

* 在模块代码内实现接口函数：

```
    int xypush_module_init(void *arg);
    int xypush_module_release(void *arg);
    int xypush_module_push (void*, void*, uint32_t, void*);
```

* 在PushModule.c包含模块头文件并注册模块

```
    #include “xypush_module.h” /* 注册新增 */
    extern push_module_t xypush_module; /* 注册新增 */
    push_module_t *global_modules[] = {
        &rtmppush_module,
        &examplepush_module,
        &xypush_module, /* 注册新增 */
        /* 新增模块加在这里即可 */
    };
```
