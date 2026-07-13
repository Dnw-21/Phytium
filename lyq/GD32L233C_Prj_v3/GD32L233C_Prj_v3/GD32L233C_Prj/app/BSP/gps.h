#ifndef __GPS_H
#define __GPS_H
#include "gd32l23x.h"
#include <stdint.h>

/* UART3 引脚配置 */
#define GPS_UART                        UART3
#define GPS_UART_CLK                    RCU_UART3
#define GPS_UART_IRQn                   UART3_IRQn
#define GPS_UART_IRQHandler             UART3_IRQHandler

#define GPS_TX_PORT                     GPIOB
#define GPS_TX_PIN                      GPIO_PIN_10
#define GPS_TX_CLK                      RCU_GPIOB
#define GPS_TX_AF                       GPIO_AF_7

#define GPS_RX_PORT                     GPIOB
#define GPS_RX_PIN                      GPIO_PIN_11
#define GPS_RX_CLK                      RCU_GPIOB
#define GPS_RX_AF                       GPIO_AF_7

/* 缓冲区大小 */
#define GPS_RX_BUF_SIZE                 1024
#define GPS_TX_BUF_SIZE                 1024
#define GPS_MAX_SEND_LEN                1024

/* 帧超时时间 (ms) */
#define GPS_FRAME_TIMEOUT_MS            50

/* 默认波特率: NEO-6M 默认为 9600, 部分模块已配置为 38400 */
#define GPS_DEFAULT_BAUDRATE            9600

/* GPS 配置存储到 MCU Flash (故障快照 0x08030000 + 40KB 之后) */
#define GPS_CFG_FLASH_ADDR              0x0803A000
#define GPS_CFG_FLASH_PAGE_SIZE         4096
#define GPS_CFG_MAGIC                   0x47505343  /* "GPSC" */

// 卫星信息
typedef struct __attribute__((__packed__))
{
    uint8_t num;        // 卫星编号
    uint8_t eledeg;     // 卫星仰角
    uint16_t azideg;    // 卫星方位角
    uint8_t sn;         // 信噪比
}nmea_slmsg;
// UTC时间信息
typedef struct __attribute__((__packed__))
{
    uint16_t year;  // 年
    uint8_t month;  // 月
    uint8_t date;   // 日
    uint8_t hour;   // 小时
    uint8_t min;    // 分钟
    uint8_t sec;    // 秒
}nmea_utc_time;
// NMEA 0183 协议解析后数据存放结构体
typedef struct __attribute__((__packed__))
{
    uint8_t svnum;                  // 可见卫星数
    nmea_slmsg slmsg[12];           // 最多12颗卫星
    nmea_utc_time utc;              // UTC时间
    uint32_t latitude;              // 纬度 分扩大100000倍,实际要除以100000
    uint8_t nshemi;                 // 北纬/南纬,N:北纬;S:南纬
    uint32_t longitude;             // 经度 分扩大100000倍,实际要除以100000
    uint8_t ewhemi;                 // 东经/西经,E:东经;W:西经
    uint8_t gpssta;                 // GPS状态:0,未定位;1,非差分定位;2,差分定位;6,正在估算.
    uint8_t posslnum;               // 用于定位的卫星数,0~12.
    uint8_t possl[12];              // 用于定位的卫星编号
    uint8_t fixmode;                // 定位模式:1,没有定位;2,2D定位;3,3D定位
    uint16_t pdop;                  // 位置精度因子 0~500,对应实际值0~50.0
    uint16_t hdop;                  // 水平精度因子 0~500,对应实际值0~50.0
    uint16_t vdop;                  // 垂直精度因子 0~500,对应实际值0~50.0

    int altitude;                   // 海拔高度,放大了10倍,实际除以10.单位:0.1m
    uint16_t speed;                 // 地面速率,放大了1000倍,实际除以10.单位:0.001公里/小时
}nmea_msg;

////////////////////////////////////////////////////////////////////////////////////////////////////
// UBLOX NEO-6M 配置(保存,加载,重启等)结构体
typedef struct __attribute__((__packed__))
{
    uint16_t header;                // cfg header,固定为0X62B5(小端模式)
    uint16_t id;                    // CFG CFG ID:0X0906 (小端模式)
    uint16_t dlength;               // 数据长度 12/13
    uint32_t clearmask;              // 子区域清除掩码(1有效)
    uint32_t savemask;               // 子区域保存掩码
    uint32_t loadmask;               // 子区域加载掩码
    uint8_t  devicemask;            // 目标器件选择位   b0:BK RAM;b1:FLASH;b2,EEPROM;b4,SPI FLASH
    uint8_t  cka;                   // 校验CK_A
    uint8_t  ckb;                   // 校验CK_B
}_ublox_cfg_cfg;

// UBLOX NEO-6M 信息设置结构体
typedef struct __attribute__((__packed__))
{
    uint16_t header;                // cfg header,固定为0X62B5(小端模式)
    uint16_t id;                    // CFG MSG ID:0X0106 (小端模式)
    uint16_t dlength;               // 数据长度 8
    uint8_t  msgclass;              // 信息类别(F0 代表NMEA信息格式)
    uint8_t  msgid;                 // 信息 ID
    uint8_t  iicset;                // IIC输出使能    0,关闭;1,使能.
    uint8_t  uart1set;              // UART1输出使能   0,关闭;1,使能.
    uint8_t  uart2set;              // UART2输出使能   0,关闭;1,使能.
    uint8_t  usbset;                // USB输出使能    0,关闭;1,使能.
    uint8_t  spiset;                // SPI输出使能    0,关闭;1,使能.
    uint8_t  ncset;                 // 未知输出使能   默认为1使能.
    uint8_t  cka;                   // 校验CK_A
    uint8_t  ckb;                   // 校验CK_B
}_ublox_cfg_msg;

// UBLOX NEO-6M UART端口设置结构体
typedef struct __attribute__((__packed__))
{
    uint16_t header;                // cfg header,固定为0X62B5(小端模式)
    uint16_t id;                    // CFG PRT ID:0X0006 (小端模式)
    uint16_t dlength;               // 数据长度 20
    uint8_t  portid;                // 端口号,0=IIC;1=UART1;2=UART2;3=USB;4=SPI;
    uint8_t  reserved;              // 保留,设置为0
    uint16_t txready;              // TX Ready引脚设置,默认为0
    uint32_t mode;                  // 串口工作模式设置,如奇偶校验,停止位,字节长度等的设置.
    uint32_t baudrate;              // 通信波特率设置
    uint16_t inprotomask;          // 输入协议激活位 默认设置为0X07 0X00即可.
    uint16_t outprotomask;         // 输出协议激活位 默认设置为0X07 0X00即可.
    uint16_t reserved4;             // 保留,设置为0
    uint16_t reserved5;             // 保留,设置为0
    uint8_t  cka;                   // 校验CK_A
    uint8_t  ckb;                   // 校验CK_B
}_ublox_cfg_prt;

// UBLOX NEO-6M 时钟脉冲设置结构体
typedef struct __attribute__((__packed__))
{
    uint16_t header;                // cfg header,固定为0X62B5(小端模式)
    uint16_t id;                    // CFG TP ID:0X0706 (小端模式)
    uint16_t dlength;               // 数据长度
    uint32_t interval;              // 时钟脉冲间隔,单位为us
    uint32_t length;                // 脉冲宽度,单位为us
    signed char status;             // 时钟脉冲状态:1,高电平有效;0,关闭;-1,低电平有效.
    uint8_t timeref;                // 参考时间:0,UTC时间;1,GPS时间;2,本地时间.
    uint8_t flags;                  // 时钟脉冲配置标志
    uint8_t reserved;               // 保留
    signed short antdelay;          // 天线延时
    signed short rfdelay;           // RF延时
    signed int userdelay;           // 用户延时
    uint8_t cka;                    // 校验CK_A
    uint8_t ckb;                    // 校验CK_B
}_ublox_cfg_tp;

// UBLOX NEO-6M 更新速率设置结构体
typedef struct __attribute__((__packed__))
{
    uint16_t header;                // cfg header,固定为0X62B5(小端模式)
    uint16_t id;                    // CFG RATE ID:0X0806 (小端模式)
    uint16_t dlength;               // 数据长度
    uint16_t measrate;              // 测量时间间隔，单位为ms，最少不能小于200ms（5Hz）
    uint16_t navrate;               // 导航速率（周期），固定为1
    uint16_t timeref;               // 参考时间：0=UTC Time；1=GPS Time；
    uint8_t  cka;                   // 校验CK_A
    uint8_t  ckb;                   // 校验CK_B
}_ublox_cfg_rate;

/* GPS 配置持久化结构体 (存入 MCU Flash) */
typedef struct __attribute__((__packed__))
{
    uint32_t magic;             // 魔数 GPS_CFG_MAGIC, 用于校验有效性
    uint32_t baudrate;          // 当前波特率
    uint16_t measrate;          // 定位更新间隔 (ms)
    uint8_t  nmea_enable[6];    // NMEA 消息使能: [GPGGA, GPGLL, GPGSA, GPGSV, GPRMC, GPVTG]
    uint8_t  reserved;          // 保留对齐
    uint32_t crc;               // 简单校验 (各字段异或)
} gps_cfg_t;

/*===================================================================
 * 外部接口函数
 *===================================================================*/

/* --- 一体化初始化 --- */
void gps_init(void);                             // GPS 完整初始化 (硬件+UBX配置)

/* --- 硬件初始化 --- */
void gps_uart3_init(uint32_t baudrate);         // UART3 初始化和 GPS 硬件初始化
void gps_start_recv(void);                       // 启动中断接收

/* --- GPS 帧处理 (需在主循环或任务中周期调用) --- */
void gps_recv_dealwith(void);                    // 帧边界检测(每1ms调用或定时调用)
uint8_t gps_frame_available(void);               // 是否有完整帧可读
uint16_t gps_read_frame(uint8_t *buf, uint16_t maxLen); // 读取一帧数据

/* --- NMEA 解析 --- */
void GPS_Analysis(nmea_msg *gpsx, uint8_t *buf);
void GpsDataRead(nmea_msg *gpsx, uint32_t now_ms); // 读取解析GPS数据, now_ms用于时间同步(填0跳过)
void GpsDataShow(nmea_msg *gpsx);               // 通过printf显示GPS信息(替代OLED)

/* --- GPS 时间同步 (GPS秒 + FreeRTOS ms = 高精度时间戳) --- */
void     gps_sync_from_utc(nmea_utc_time *utc, uint32_t now_ms);  // 收到GPS帧后调用, 以RTOS ms校准基准
uint32_t gps_get_timestamp_ms(uint32_t now_ms);  // 返回基于GPS时间的ms级时间戳
uint8_t  gps_time_is_valid(void);                // GPS时间是否已同步
void     gps_get_utc_time(nmea_utc_time *utc, uint16_t *ms_offset, uint32_t now_ms);

/* --- UBX 协议配置 --- */
uint8_t Ublox_Cfg_Msg(uint8_t msgid, uint8_t uart1set);
uint8_t Ublox_Cfg_Rate(uint16_t measrate, uint8_t reftime);
uint8_t Ublox_Cfg_Cfg_Save(void);
uint8_t gps_cfg_load(gps_cfg_t *cfg);            // 从 Flash 读取配置
uint8_t gps_cfg_is_valid(void);                  // 检查 Flash 中配置是否有效
uint8_t Ublox_Cfg_Prt(uint32_t baudrate);
uint8_t Ublox_Cfg_Tp(uint32_t interval, uint32_t length, signed char status);

/* --- UART3 收发底层 --- */
void gps_uart3_send(uint8_t *data, uint16_t len);  // 阻塞发送
void gps_uart3_clear_rx(void);                      // 清空接收缓冲

#endif
