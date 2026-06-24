/*
 * Copyright (C) 2022, Phytium Technology Co., Ltd.   All Rights Reserved.
 *
 * Licensed under the BSD 3-Clause License (the "License"); you may not use
 * this file except in compliance with the License. You may obtain a copy of
 * the License at
 *
 *     https://opensource.org/licenses/BSD-3-Clause
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 *
 * FilePath: fgpio.h
 * Date: 2022-02-10 14:53:42
 * LastEditTime: 2022-02-18 08:25:35
 * Description:  This files is for GPIO user API definition
 *
 * Modify History:
 *  Ver   Who        Date         Changes
 * ----- ------     --------    --------------------------------------
 * 1.0   zhugengyu  2022/3/1     init commit
 * 2.0   zhugengyu  2022/7/1     support e2000
 * 3.0   zhugengyu  2024/5/7     modify interface to use gpio by pin
 */


#ifndef FGPIO_H
#define FGPIO_H

#include "fparameters.h"
#include "ftypes.h"
#include "fassert.h"
#include "ferror_code.h"

#ifdef __cplusplus
extern "C"
{
#endif

/************************** Constant Definitions *****************************/
#define FGPIO_SUCCESS           FT_SUCCESS
#define FGPIO_ERR_INVALID_PARA  FT_MAKE_ERRCODE(ErrModBsp, ErrBspGpio, 0x0)
#define FGPIO_ERR_INVALID_STATE FT_MAKE_ERRCODE(ErrModBsp, ErrBspGpio, 0x1)

typedef enum
{
    FGPIO_DIR_INPUT = 0, /* 输入 */
    FGPIO_DIR_OUTPUT     /* 输出 */
} FGpioDirection;        /* GPIO引脚的输入输出方向 */

typedef enum
{
    FGPIO_IRQ_TYPE_EDGE_FALLING = 0, /* 下降沿中断，引脚检测到电平从高变低时触发 */
    FGPIO_IRQ_TYPE_EDGE_RISING, /* 上升沿中断，引脚检测到电平从低变高时触发 */
    FGPIO_IRQ_TYPE_LEVEL_LOW, /* 低电平中断，引脚电平为低时触发 */
    FGPIO_IRQ_TYPE_LEVEL_HIGH /* 高电平中断，引脚电平为高时触发 */
} FGpioIrqType;               /* GPIO引脚中断类型 */

typedef enum
{
    FGPIO_PIN_LOW = 0, /* 低电平 */
    FGPIO_PIN_HIGH     /* 高电平 */
} FGpioVal;            /* GPIO引脚电平类型 */

typedef struct
{
    u32 id;            /* GPIO标号，0 ~ FGPIO_NUM */
    u32 ctrl;          /* GPIO所属的控制器，0 ~ FGPIO_CTRL_NUM */
    u32 port;          /* GPIO所属的Port, Port A, B */
    u32 pin;           /* GPIO的引脚号，0 ~ FGPIO_PIN_NUM */
    uintptr base_addr; /* GPIO控制器基地址 */
    u32 irq_num;       /* GPIO中断号，如果不支持中断，置位为 0 */
    u32 cap;           /* GPIO引脚能力集 */
} FGpioConfig;         /* GPIO引脚配置 */

typedef void (*FGpioInterruptCallback)(s32 vector, void *param); /* GPIO引脚中断回调函数类型 */

typedef struct
{
    FGpioConfig config;
    u32 is_ready;
} FGpio; /* GPIO引脚实例 */

typedef struct
{
    uintptr base_addr; /* 引脚所在控制器的基地址 */
    FGpioInterruptCallback irq_cbs[FGPIO_PIN_NUM * FGPIO_PORT_NUM]; /* 引脚中断回调 */
    void *irq_cb_params[FGPIO_PIN_NUM * FGPIO_PORT_NUM]; /* 引脚中断回调参数 */
} FGpioIntrMap; /* GPIO中断索引表，用于多个引脚共用一个中断号的中断处理 */

/************************** Function Prototypes ******************************/
/* 获取GPIO引脚的默认配置 */
const FGpioConfig *FGpioLookupConfig(u32 gpio_id);

/* 初始化GPIO引脚实例 */
FError FGpioCfgInitialize(FGpio *const pin, const FGpioConfig *const config);

/* 去初始化GPIO引脚实例 */
void FGpioDeInitialize(FGpio *const pin);

/* 设置GPIO引脚的输入输出方向 */
void FGpioSetDirection(FGpio *const pin, FGpioDirection dir);

/* 获取GPIO引脚的输入输出方向 */
FGpioDirection FGpioGetDirection(FGpio *const pin);

/* 设置GPIO引脚的输出值 */
FError FGpioSetOutputValue(FGpio *const pin, const FGpioVal output);

/* 获取GPIO引脚的输入值 */
FGpioVal FGpioGetInputValue(FGpio *const pin);

/* 获取GPIO引脚的中断屏蔽位 */
void FGpioGetInterruptMask(FGpio *const pin, u32 *mask, u32 *enabled);

/* 设置GPIO 引脚的中断屏蔽位 */
void FGpioSetInterruptMask(FGpio *const pin, boolean enable);

/* 获取GPIO 引脚的中断类型和中断极性 */
void FGpioGetInterruptType(FGpio *const pin, FGpioIrqType *type);

/* 设置GPIO 引脚的中断类型 */
void FGpioSetInterruptType(FGpio *const pin, const FGpioIrqType type);

/* GPIO 引脚中断处理函数 */
void FGpioInterruptHandler(s32 vector, void *param);

/* 注册GPIO引脚中断回调函数(引脚通过控制器统一上报中断，共用中断号) */
void FGpioRegisterInterruptCB(FGpio *const pin, FGpioInterruptCallback cb, void *cb_param);

/* 打印GPIO控制寄存器信息 */
void FGpioDumpRegisters(uintptr base_addr);

#ifdef __cplusplus
}
#endif

#endif
