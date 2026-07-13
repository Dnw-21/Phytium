#include "gps.h"
#include "systick.h"
#include <string.h>
#include <stdio.h>

/*===================================================================
 * UART3 接收缓冲 (中断驱动 + 帧超时检测)
 *===================================================================*/
static uint8_t  gps_rx_int_buf[GPS_RX_BUF_SIZE];   // 中断接收缓冲
static volatile uint16_t gps_rx_index;             // 中断写入位置
static volatile uint8_t  gps_rx_flag;              // 接收活动标志
static volatile uint16_t gps_rx_int_len;           // 已接收字节数(用于超时比较)
static volatile uint16_t gps_rx_timer;             // 超时计时器
static volatile uint8_t  gps_rx_ok_flag;           // 帧就绪标志

static uint8_t  gps_rx_frame_buf[GPS_RX_BUF_SIZE]; // 就绪帧数据拷贝
static volatile uint16_t gps_rx_frame_len;         // 就绪帧长度

/*===================================================================
 * UART3 发送缓冲
 *===================================================================*/
static uint8_t gps_tx_buf[GPS_TX_BUF_SIZE] __attribute__((aligned(8)));

/*===================================================================
 * UART3 中断服务函数
 *===================================================================*/
void UART3_IRQHandler(void)
{
    if (usart_interrupt_flag_get(GPS_UART, USART_INT_FLAG_RBNE) != RESET) {
        uint8_t data = usart_data_receive(GPS_UART);
        gps_rx_flag = 0x55;                        // 标记有数据活动
        if (gps_rx_index < GPS_RX_BUF_SIZE) {
            gps_rx_int_buf[gps_rx_index] = data;
            gps_rx_index++;
        } else {
            gps_rx_index = 0;                      // 溢出回绕
        }
        usart_interrupt_flag_clear(GPS_UART, USART_INT_FLAG_RBNE);
    }
    if (usart_flag_get(GPS_UART, USART_FLAG_ORERR) != RESET) {
        usart_flag_clear(GPS_UART, USART_FLAG_ORERR);
    }
    usart_interrupt_flag_clear(GPS_UART, USART_INT_FLAG_ERR_NERR);
}

/*===================================================================
 * 一体化初始化: 硬件 + UBX 模块配置
 *===================================================================*/

/**
 * @brief GPS 模块完整初始化
 *        第1步: 初始化 UART3 硬件
 *        第2步: 启动中断接收
 *        第3步: 配置 NEO-6M (关闭非必要 NMEA 输出, 设置 1Hz 更新速率, 保存配置)
 *        第4步: 清空缓冲, 准备接收数据
 */
void gps_init(void)
{
    uint8_t key = 1;  // 重试标志

    /* 第1步: UART3 硬件初始化 */
    gps_uart3_init(GPS_DEFAULT_BAUDRATE);

    /* 第2步: 启动中断接收 */
    gps_start_recv();

    /* 第3步: 配置 NEO-6M 模块 (UBX 协议) */

    /* ① 关闭不需要的 NMEA 输出, 只保留 GPRMC 和 GPGGA */
    Ublox_Cfg_Msg(1, 0);   // 关闭 GPGLL
    cpu_delay_ms(500);
    Ublox_Cfg_Msg(2, 0);   // 关闭 GPGSA
    cpu_delay_ms(500);
    Ublox_Cfg_Msg(3, 0);   // 关闭 GPGSV
    cpu_delay_ms(500);

    /* ② 设置定位更新速率 1000ms (1Hz), 同时检测模块是否在位 */
    if (Ublox_Cfg_Rate(1000, 1) != 0) {
        while ((Ublox_Cfg_Rate(1000, 1) != 0) && key) {
            // key = Ublox_Cfg_Cfg_Save();  // 保存配置到模块 EEPROM
            // cpu_delay_ms(100);
        }
        cpu_delay_ms(500);
    }

    /* 第4步: 清空接收缓冲, 准备接收 NMEA 数据 */
    gps_uart3_clear_rx();
}

/*===================================================================
 * 硬件初始化
 *===================================================================*/

/**
 * @brief UART3 初始化 (用于 GPS 模块通信)
 * @param baudrate 波特率 (如 38400)
 */
void gps_uart3_init(uint32_t baudrate)
{
    /* 时钟使能 */
    rcu_periph_clock_enable(GPS_UART_CLK);
    rcu_periph_clock_enable(GPS_TX_CLK);
    rcu_periph_clock_enable(GPS_RX_CLK);

    /* GPIO AF 配置 */
    gpio_af_set(GPS_TX_PORT, GPS_TX_AF, GPS_TX_PIN);
    gpio_af_set(GPS_RX_PORT, GPS_RX_AF, GPS_RX_PIN);

    /* GPIO 模式: TX 推挽输出, RX 上拉输入 */
    gpio_mode_set(GPS_TX_PORT, GPIO_MODE_AF, GPIO_PUPD_PULLUP, GPS_TX_PIN);
    gpio_mode_set(GPS_RX_PORT, GPIO_MODE_AF, GPIO_PUPD_PULLUP, GPS_RX_PIN);
    gpio_output_options_set(GPS_TX_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, GPS_TX_PIN);

    /* UART3 参数: 8N1 */
    usart_deinit(GPS_UART);
    usart_baudrate_set(GPS_UART, baudrate);
    usart_word_length_set(GPS_UART, USART_WL_8BIT);
    usart_stop_bit_set(GPS_UART, USART_STB_1BIT);
    usart_parity_config(GPS_UART, USART_PM_NONE);
    usart_receive_config(GPS_UART, USART_RECEIVE_ENABLE);
    usart_transmit_config(GPS_UART, USART_TRANSMIT_ENABLE);
    usart_enable(GPS_UART);

    /* 使能接收中断 */
    nvic_irq_enable(GPS_UART_IRQn, 2);
    usart_interrupt_enable(GPS_UART, USART_INT_RBNE);
}

/**
 * @brief 启动中断接收 (清空缓冲并开始)
 */
void gps_start_recv(void)
{
    gps_uart3_clear_rx();
}

/*===================================================================
 * 底层收发
 *===================================================================*/

/**
 * @brief UART3 阻塞发送
 */
void gps_uart3_send(uint8_t *data, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++) {
        while (usart_flag_get(GPS_UART, USART_FLAG_TBE) == RESET);
        usart_data_transmit(GPS_UART, data[i]);
    }
}

/**
 * @brief 清空接收缓冲
 */
void gps_uart3_clear_rx(void)
{
    gps_rx_ok_flag = 0;
    gps_rx_frame_len = 0;
    gps_rx_int_len = 0;
    gps_rx_index = 0;
    gps_rx_flag = 0;
    gps_rx_timer = 0;
}

/*===================================================================
 * GPS 帧处理 (帧边界 = 50ms 超时无新字节)
 * 需在主循环/任务中周期调用，建议每 1ms 调用一次
 *===================================================================*/

/**
 * @brief 帧边界检测。在 SysTick 或定时任务中每 1ms 调用
 */
void gps_recv_dealwith(void)
{
    if (gps_rx_flag == 0x55) {
        if (gps_rx_int_len < gps_rx_index) {
            // 有新字节到达, 更新计数
            gps_rx_int_len = gps_rx_index;
        } else {
            // 无新字节, 累计超时
            gps_rx_timer++;
            if (gps_rx_timer >= GPS_FRAME_TIMEOUT_MS) {
                // 超时 → 帧就绪
                gps_rx_timer = 0;
                gps_rx_flag = 0;
                gps_rx_ok_flag = 0x55;
                gps_rx_frame_len = gps_rx_int_len;
                memcpy(gps_rx_frame_buf, gps_rx_int_buf, gps_rx_frame_len);
                gps_rx_index = 0;
                gps_rx_int_len = 0;
            }
        }
    }
}

/**
 * @brief 查询是否有完整帧可读
 * @return 1=有帧, 0=无
 */
uint8_t gps_frame_available(void)
{
    if (gps_rx_ok_flag == 0x55) {
        gps_rx_ok_flag = 0;
        return 1;
    }
    return 0;
}

/**
 * @brief 读取一帧数据
 * @param buf 目标缓冲区
 * @param maxLen 最大读取字节数
 * @return 实际读取字节数
 */
uint16_t gps_read_frame(uint8_t *buf, uint16_t maxLen)
{
    uint16_t len = (gps_rx_frame_len < maxLen) ? gps_rx_frame_len : maxLen;
    memcpy(buf, gps_rx_frame_buf, len);
    gps_rx_frame_len = 0;
    return len;
}

/*===================================================================
 * NMEA-0183 协议解析函数 (从 STM32 原版移植, 算法不变)
 *===================================================================*/

/**
 * @brief 从buf里面得到第cx个逗号所在的位置
 * @return 0~0XFE:逗号所在位置; 0XFF:不存在第cx个逗号
 */
static uint8_t NMEA_Comma_Pos(uint8_t *buf, uint8_t cx)
{
    uint8_t *p = buf;
    while (cx) {
        if (*buf == '*' || *buf < ' ' || *buf > 'z') return 0XFF;
        if (*buf == ',') cx--;
        buf++;
    }
    return (uint8_t)(buf - p);
}

/**
 * @brief m^n 次方
 */
static uint32_t NMEA_Pow(uint8_t m, uint8_t n)
{
    uint32_t result = 1;
    while (n--) result *= m;
    return result;
}

/**
 * @brief 字符串转数值 (以','或'*'结束)
 * @param dx 返回小数位数
 */
int NMEA_Str2num(uint8_t *buf, uint8_t *dx)
{
    uint8_t *p = buf;
    uint32_t ires = 0, fres = 0;
    uint8_t ilen = 0, flen = 0, i;
    uint8_t mask = 0;
    int res;

    while (1) {
        if (*p == '-') { mask |= 0X02; p++; }
        if (*p == ',' || (*p == '*')) break;
        if (*p == '.') { mask |= 0X01; p++; }
        else if (*p > '9' || (*p < '0')) {
            ilen = 0;
            flen = 0;
            break;
        }
        if (mask & 0X01) flen++;
        else ilen++;
        p++;
    }
    if (mask & 0X02) buf++;
    for (i = 0; i < ilen; i++) {
        ires += NMEA_Pow(10, ilen - 1 - i) * (buf[i] - '0');
    }
    if (flen > 5) flen = 5;
    *dx = flen;
    for (i = 0; i < flen; i++) {
        fres += NMEA_Pow(10, flen - 1 - i) * (buf[ilen + 1 + i] - '0');
    }
    res = ires * NMEA_Pow(10, flen) + fres;
    if (mask & 0X02) res = -res;
    return res;
}

/**
 * @brief 分析 GPGSV 信息
 */
static void NMEA_GPGSV_Analysis(nmea_msg *gpsx, uint8_t *buf)
{
    uint8_t *p, *p1, dx;
    uint8_t len, i, j, slx = 0;
    uint8_t posx;

    p = buf;
    p1 = (uint8_t *)strstr((const char *)p, "$GPGSV");
    if (p1 == NULL) return;
    len = p1[7] - '0';
    posx = NMEA_Comma_Pos(p1, 3);
    if (posx != 0XFF) gpsx->svnum = NMEA_Str2num(p1 + posx, &dx);

    for (i = 0; i < len; i++) {
        p1 = (uint8_t *)strstr((const char *)p, "$GPGSV");
        if (p1 == NULL) break;
        for (j = 0; j < 4; j++) {
            posx = NMEA_Comma_Pos(p1, 4 + j * 4);
            if (posx != 0XFF) gpsx->slmsg[slx].num = NMEA_Str2num(p1 + posx, &dx);
            else break;
            posx = NMEA_Comma_Pos(p1, 5 + j * 4);
            if (posx != 0XFF) gpsx->slmsg[slx].eledeg = NMEA_Str2num(p1 + posx, &dx);
            else break;
            posx = NMEA_Comma_Pos(p1, 6 + j * 4);
            if (posx != 0XFF) gpsx->slmsg[slx].azideg = NMEA_Str2num(p1 + posx, &dx);
            else break;
            posx = NMEA_Comma_Pos(p1, 7 + j * 4);
            if (posx != 0XFF) gpsx->slmsg[slx].sn = NMEA_Str2num(p1 + posx, &dx);
            else break;
            slx++;
        }
        p = p1 + 1;
    }
}

/**
 * @brief 分析 GPGGA 信息
 */
static void NMEA_GPGGA_Analysis(nmea_msg *gpsx, uint8_t *buf)
{
    uint8_t *p1, dx;
    uint8_t posx;

    p1 = (uint8_t *)strstr((const char *)buf, "$GPGGA");
    if (p1 == NULL) return;
    posx = NMEA_Comma_Pos(p1, 6);
    if (posx != 0XFF) gpsx->gpssta = NMEA_Str2num(p1 + posx, &dx);
    posx = NMEA_Comma_Pos(p1, 7);
    if (posx != 0XFF) gpsx->posslnum = NMEA_Str2num(p1 + posx, &dx);
    posx = NMEA_Comma_Pos(p1, 9);
    if (posx != 0XFF) gpsx->altitude = NMEA_Str2num(p1 + posx, &dx);
}

/**
 * @brief 分析 GPGSA 信息
 */
static void NMEA_GPGSA_Analysis(nmea_msg *gpsx, uint8_t *buf)
{
    uint8_t *p1, dx;
    uint8_t posx;
    uint8_t i;

    p1 = (uint8_t *)strstr((const char *)buf, "$GPGSA");
    if (p1 == NULL) return;
    posx = NMEA_Comma_Pos(p1, 2);
    if (posx != 0XFF) gpsx->fixmode = NMEA_Str2num(p1 + posx, &dx);
    for (i = 0; i < 12; i++) {
        posx = NMEA_Comma_Pos(p1, 3 + i);
        if (posx != 0XFF) gpsx->possl[i] = NMEA_Str2num(p1 + posx, &dx);
        else break;
    }
    posx = NMEA_Comma_Pos(p1, 15);
    if (posx != 0XFF) gpsx->pdop = NMEA_Str2num(p1 + posx, &dx);
    posx = NMEA_Comma_Pos(p1, 16);
    if (posx != 0XFF) gpsx->hdop = NMEA_Str2num(p1 + posx, &dx);
    posx = NMEA_Comma_Pos(p1, 17);
    if (posx != 0XFF) gpsx->vdop = NMEA_Str2num(p1 + posx, &dx);
}

/**
 * @brief GPRMC 校验和检查
 * @return 1=校验成功
 */
static uint16_t NMEA_GPRMC_CheckSum(uint8_t *buf)
{
    uint8_t i, j, ch1, ch2;
    uint16_t CheckSum = 0;
    uint16_t CheckData;

    for (i = 0; i < 120; i++) {
        if (*(buf + i) == '*') break;
    }
    for (j = 0; j < i; j++) {
        CheckSum ^= *(buf + j);
    }
    ch1 = *(buf + i + 1);
    ch2 = *(buf + i + 2);
    CheckData = (ch1 >= '0' && ch1 <= '9') ? (ch1 - '0') : (ch1 - 'A' + 10);
    CheckData = (CheckData << 4) | ((ch2 >= '0' && ch2 <= '9') ? (ch2 - '0') : (ch2 - 'A' + 10));

    if (CheckSum == CheckData) return 1;
    return 0;
}

/**
 * @brief 分析 GPRMC 信息 (经纬度、时间日期)
 */
static void NMEA_GPRMC_Analysis(nmea_msg *gpsx, uint8_t *buf)
{
    uint8_t *p1, dx;
    uint8_t posx;
    uint32_t temp;
    float rs;

    p1 = (uint8_t *)strstr((const char *)buf, "GPRMC");
    if (p1 == NULL) return;
    if (NMEA_GPRMC_CheckSum(p1) == 0) return;

    posx = NMEA_Comma_Pos(p1, 1);
    if (posx != 0XFF) {
        temp = NMEA_Str2num(p1 + posx, &dx) / NMEA_Pow(10, dx);
        gpsx->utc.hour = temp / 10000;
        gpsx->utc.min = (temp / 100) % 100;
        gpsx->utc.sec = temp % 100;
    }
    posx = NMEA_Comma_Pos(p1, 3);
    if (posx != 0XFF) {
        temp = NMEA_Str2num(p1 + posx, &dx);
        gpsx->latitude = temp / NMEA_Pow(10, dx + 2);
        rs = temp % NMEA_Pow(10, dx + 2);
        gpsx->latitude = gpsx->latitude * NMEA_Pow(10, 5) + (rs * NMEA_Pow(10, 5 - dx)) / 60;
    }
    posx = NMEA_Comma_Pos(p1, 4);
    if (posx != 0XFF) gpsx->nshemi = *(p1 + posx);
    posx = NMEA_Comma_Pos(p1, 5);
    if (posx != 0XFF) {
        temp = NMEA_Str2num(p1 + posx, &dx);
        gpsx->longitude = temp / NMEA_Pow(10, dx + 2);
        rs = temp % NMEA_Pow(10, dx + 2);
        gpsx->longitude = gpsx->longitude * NMEA_Pow(10, 5) + (rs * NMEA_Pow(10, 5 - dx)) / 60;
    }
    posx = NMEA_Comma_Pos(p1, 6);
    if (posx != 0XFF) gpsx->ewhemi = *(p1 + posx);
    posx = NMEA_Comma_Pos(p1, 9);
    if (posx != 0XFF) {
        temp = NMEA_Str2num(p1 + posx, &dx);
        gpsx->utc.date = temp / 10000;
        gpsx->utc.month = (temp / 100) % 100;
        gpsx->utc.year = 2000 + temp % 100;
    }
}

/**
 * @brief 分析 GPVTG 信息 (地面速度)
 */
static void NMEA_GPVTG_Analysis(nmea_msg *gpsx, uint8_t *buf)
{
    uint8_t *p1, dx;
    uint8_t posx;

    p1 = (uint8_t *)strstr((const char *)buf, "$GPVTG");
    if (p1 == NULL) return;
    posx = NMEA_Comma_Pos(p1, 7);
    if (posx != 0XFF) {
        gpsx->speed = NMEA_Str2num(p1 + posx, &dx);
        if (dx < 3) gpsx->speed *= NMEA_Pow(10, 3 - dx);
    }
}

/**
 * @brief 提取 NMEA-0183 信息
 */
void GPS_Analysis(nmea_msg *gpsx, uint8_t *buf)
{
    NMEA_GPGGA_Analysis(gpsx, buf);
    NMEA_GPRMC_Analysis(gpsx, buf);
    NMEA_GPVTG_Analysis(gpsx, buf);
}

/*===================================================================
 * UBLOX UBX 协议配置函数 (适配 GD32)
 *===================================================================*/

/**
 * @brief UBX 校验和计算
 */
static void Ublox_CheckSum(uint8_t *buf, uint16_t len, uint8_t *cka, uint8_t *ckb)
{
    uint16_t i;
    *cka = 0; *ckb = 0;
    for (i = 0; i < len; i++) {
        *cka = *cka + buf[i];
        *ckb = *ckb + *cka;
    }
}

/**
 * @brief UBX ACK 等待检测
 * @return 0=ACK成功, 1=超时, 2=未找到同步字符, 3=收到NACK
 */
static uint8_t Ublox_Cfg_Ack_Check(void)
{
    uint16_t len = 0, i;
    uint8_t rval = 0;
    uint8_t rxbuf[256];
    uint16_t rxlen;

    /* 等待有帧到达 (最长 500ms) */
    while (gps_frame_available() == 0 && len < 100) {
        len++;
        cpu_delay_ms(5);
    }
    if (gps_frame_available()) {
        rxlen = gps_read_frame(rxbuf, sizeof(rxbuf));
        for (i = 0; i < rxlen; i++)
            if (rxbuf[i] == 0xB5) break;   // 查找同步字符 0xB5
        if (i == rxlen) rval = 2;            // 未找到同步字符
        else if (rxbuf[i + 3] == 0x00) rval = 3;  // NACK
        else rval = 0;                       // ACK
    } else {
        rval = 1;                            // 超时
    }
    return rval;
}

/**
 * @brief 保存 GPS 配置到 MCU Flash (故障快照区域之后)
 *        同时发送 UBX 命令将配置保存到 GPS 模块 EEPROM
 * @return 0=成功, 1=失败
 */
uint8_t Ublox_Cfg_Cfg_Save(void)
{
    gps_cfg_t cfg;
    uint32_t crc;
    uint8_t i;

    /* 填充配置结构体 */
    memset(&cfg, 0, sizeof(cfg));
    cfg.magic    = GPS_CFG_MAGIC;
    cfg.baudrate = GPS_DEFAULT_BAUDRATE;
    cfg.measrate = 1000;                        // 1Hz 更新速率
    cfg.nmea_enable[0] = 1;                     // GPGGA: 开启
    cfg.nmea_enable[1] = 0;                     // GPGLL: 关闭
    cfg.nmea_enable[2] = 0;                     // GPGSA: 关闭
    cfg.nmea_enable[3] = 0;                     // GPGSV: 关闭
    cfg.nmea_enable[4] = 1;                     // GPRMC: 开启
    cfg.nmea_enable[5] = 0;                     // GPVTG: 关闭

    /* 计算 CRC */
    crc = 0;
    for (i = 0; i < sizeof(cfg) - 4; i++)
        crc ^= ((uint8_t *)&cfg)[i];
    cfg.crc = crc;

    /* 先发送 UBX 保存命令到 GPS 模块 EEPROM */
    _ublox_cfg_cfg *ubx_cfg = (_ublox_cfg_cfg *)gps_tx_buf;
    ubx_cfg->header  = 0x62B5;
    ubx_cfg->id      = 0x0906;
    ubx_cfg->dlength = 13;
    ubx_cfg->clearmask  = 0;
    ubx_cfg->savemask   = 0xFFFF;
    ubx_cfg->loadmask   = 0;
    ubx_cfg->devicemask = 4;                    // GPS 模块 EEPROM
    Ublox_CheckSum((uint8_t *)(&ubx_cfg->id), sizeof(_ublox_cfg_cfg) - 4,
                   &ubx_cfg->cka, &ubx_cfg->ckb);
    gps_uart3_clear_rx();
    gps_uart3_send((uint8_t *)gps_tx_buf, sizeof(_ublox_cfg_cfg));

    /* 写入 MCU Flash */
    fmc_unlock();
    fmc_page_erase(GPS_CFG_FLASH_ADDR);
    {
        const uint32_t *src = (const uint32_t *)&cfg;
        uint16_t words = (sizeof(cfg) + 3) / 4;
        for (i = 0; i < words; i++)
            fmc_word_program(GPS_CFG_FLASH_ADDR + i * 4, src[i]);
    }
    fmc_lock();

    /* 等待 GPS 模块 ACK */
    for (i = 0; i < 6; i++)
        if (Ublox_Cfg_Ack_Check() == 0) break;
    return (i == 6) ? 1 : 0;
}

/**
 * @brief 从 MCU Flash 读取 GPS 配置
 * @param cfg 输出配置结构体
 * @return 0=成功, 1=魔数无效
 */
uint8_t gps_cfg_load(gps_cfg_t *cfg)
{
    memcpy(cfg, (void *)GPS_CFG_FLASH_ADDR, sizeof(gps_cfg_t));
    if (cfg->magic != GPS_CFG_MAGIC) return 1;

    /* 校验 CRC */
    uint32_t crc = 0;
    uint8_t i;
    for (i = 0; i < sizeof(gps_cfg_t) - 4; i++)
        crc ^= ((uint8_t *)cfg)[i];
    if (crc != cfg->crc) return 1;

    return 0;
}

/**
 * @brief 检查 MCU Flash 中 GPS 配置是否有效
 * @return 1=有效, 0=无效
 */
uint8_t gps_cfg_is_valid(void)
{
    gps_cfg_t cfg;
    return (gps_cfg_load(&cfg) == 0) ? 1 : 0;
}

/**
 * @brief 配置 NMEA 消息输出
 * @param msgid NMEA消息ID (0=GPGGA, 1=GPGLL, 2=GPGSA, 3=GPGSV, 4=GPRMC, 5=GPVTG)
 * @param uart1set 0=关闭, 1=开启
 * @return 0=成功, 其他=失败
 */
uint8_t Ublox_Cfg_Msg(uint8_t msgid, uint8_t uart1set)
{
    _ublox_cfg_msg *cfg_msg = (_ublox_cfg_msg *)gps_tx_buf;
    cfg_msg->header = 0x62B5;
    cfg_msg->id = 0x0106;
    cfg_msg->dlength = 8;
    cfg_msg->msgclass = 0xF0;            // NMEA 消息
    cfg_msg->msgid = msgid;
    cfg_msg->iicset = 1;
    cfg_msg->uart1set = uart1set;        // 目标 UART 输出
    cfg_msg->uart2set = 1;
    cfg_msg->usbset = 1;
    cfg_msg->spiset = 1;
    cfg_msg->ncset = 1;

    Ublox_CheckSum((uint8_t *)(&cfg_msg->id), sizeof(_ublox_cfg_msg) - 4, &cfg_msg->cka, &cfg_msg->ckb);
    gps_uart3_clear_rx();
    gps_uart3_send((uint8_t *)gps_tx_buf, sizeof(_ublox_cfg_msg));
    return Ublox_Cfg_Ack_Check();
}

/**
 * @brief 配置 UART 端口参数
 * @param baudrate 波特率
 * @return 0=成功, 其他=失败
 */
uint8_t Ublox_Cfg_Prt(uint32_t baudrate)
{
    _ublox_cfg_prt *cfg_prt = (_ublox_cfg_prt *)gps_tx_buf;
    cfg_prt->header = 0x62B5;
    cfg_prt->id = 0x0006;
    cfg_prt->dlength = 20;
    cfg_prt->portid = 1;                 // UART1
    cfg_prt->reserved = 0;
    cfg_prt->txready = 0;
    cfg_prt->mode = 0x08D0;              // 8位, 1停止位, 无校验
    cfg_prt->baudrate = baudrate;
    cfg_prt->inprotomask = 0x0007;
    cfg_prt->outprotomask = 0x0007;
    cfg_prt->reserved4 = 0;
    cfg_prt->reserved5 = 0;

    Ublox_CheckSum((uint8_t *)(&cfg_prt->id), sizeof(_ublox_cfg_prt) - 4, &cfg_prt->cka, &cfg_prt->ckb);
    gps_uart3_clear_rx();
    gps_uart3_send((uint8_t *)gps_tx_buf, sizeof(_ublox_cfg_prt));
    cpu_delay_ms(200);

    /* 重新初始化 UART3 以适应新波特率 */
    gps_uart3_init(baudrate);
    return Ublox_Cfg_Ack_Check();        // 注意: 此时模块已切波特率, ACK可能收不到
}

/**
 * @brief 配置时钟脉冲输出
 */
uint8_t Ublox_Cfg_Tp(uint32_t interval, uint32_t length, signed char status)
{
    _ublox_cfg_tp *cfg_tp = (_ublox_cfg_tp *)gps_tx_buf;
    cfg_tp->header = 0x62B5;
    cfg_tp->id = 0x0706;
    cfg_tp->dlength = 20;
    cfg_tp->interval = interval;
    cfg_tp->length = length;
    cfg_tp->status = status;
    cfg_tp->timeref = 0;                 // UTC 时间
    cfg_tp->flags = 0;
    cfg_tp->reserved = 0;
    cfg_tp->antdelay = 820;              // 天线延时 820ns
    cfg_tp->rfdelay = 0;
    cfg_tp->userdelay = 0;

    Ublox_CheckSum((uint8_t *)(&cfg_tp->id), sizeof(_ublox_cfg_tp) - 4, &cfg_tp->cka, &cfg_tp->ckb);
    gps_uart3_clear_rx();
    gps_uart3_send((uint8_t *)gps_tx_buf, sizeof(_ublox_cfg_tp));
    return Ublox_Cfg_Ack_Check();
}

/**
 * @brief 配置定位更新速率
 * @param measrate 测量间隔(ms), 最小 200ms
 * @param reftime 参考时间: 0=UTC Time, 1=GPS Time
 * @return 0=成功, 其他=失败
 */
uint8_t Ublox_Cfg_Rate(uint16_t measrate, uint8_t reftime)
{
    _ublox_cfg_rate *cfg_rate = (_ublox_cfg_rate *)gps_tx_buf;
    if (measrate < 200) return 1;        // 最小 200ms

    cfg_rate->header = 0x62B5;
    cfg_rate->id = 0x0806;
    cfg_rate->dlength = 6;
    cfg_rate->measrate = measrate;
    cfg_rate->navrate = 1;
    cfg_rate->timeref = reftime;

    Ublox_CheckSum((uint8_t *)(&cfg_rate->id), sizeof(_ublox_cfg_rate) - 4, &cfg_rate->cka, &cfg_rate->ckb);
    gps_uart3_clear_rx();
    gps_uart3_send((uint8_t *)gps_tx_buf, sizeof(_ublox_cfg_rate));
    return Ublox_Cfg_Ack_Check();
}

/*===================================================================
 * GPS 数据读取与显示
 *===================================================================*/

/**
 * @brief 读取并解析 GPS 数据, 同时以 now_ms 为基准同步 GPS 时间
 * @param gpsx   NMEA 解析结果结构体指针
 * @param now_ms 当前 RTOS 毫秒值 (xTaskGetTickCount() * portTICK_PERIOD_MS), 填0则不更新时间基准
 */
void GpsDataRead(nmea_msg *gpsx, uint32_t now_ms)
{
    uint16_t rxlen;
    static uint8_t parse_buf[GPS_RX_BUF_SIZE];

    if (gps_frame_available()) {
        rxlen = gps_read_frame(parse_buf, sizeof(parse_buf));
        if (rxlen > 0) {
            parse_buf[rxlen] = 0;        // 添加结束符
            GPS_Analysis(gpsx, parse_buf);
            /* GPS 时间同步: GPS 秒 + RTOS ms 偏移 → 高精度时间戳 */
            if (now_ms != 0 && gpsx->utc.year >= 2000) {
                gps_sync_from_utc(&gpsx->utc, now_ms);
            }
        }
    }
}

/**
 * @brief 通过 printf 显示 GPS 定位信息 (替代 OLED 显示)
 */
void GpsDataShow(nmea_msg *gpsx)
{
    float tp;
    uint8_t beijing_hour;

    tp = gpsx->longitude;
    printf("GPS Longitude: %.5f %c\r\n", tp / 100000.0f, gpsx->ewhemi);

    tp = gpsx->latitude;
    printf("GPS Latitude:  %.5f %c\r\n", tp / 100000.0f, gpsx->nshemi);

    if (gpsx->fixmode <= 3) {
        beijing_hour = gpsx->utc.hour + 8;
        if (beijing_hour >= 24) beijing_hour -= 24;
        printf("GPS Time: %02d:%02d:%02d (Beijing)\r\n",
               beijing_hour, gpsx->utc.min, gpsx->utc.sec);
    }

    printf("GPS Fix: %d, Satellites: %d, Altitude: %.1fm, Speed: %.3fkm/h\r\n",
           gpsx->fixmode, gpsx->posslnum,
           gpsx->altitude / 10.0f, gpsx->speed / 1000.0f);
}

/*===================================================================
 * GPS 时间同步: GPS 秒级时间 + RTOS ms偏移 = 高精度时间戳
 *===================================================================*/

static uint32_t gps_epoch_sec;      // GPS UTC 时间 (1970-01-01 起算的秒数)
static uint32_t gps_sync_tick_ms;   // GPS 帧到达时的 RTOS 毫秒值
static uint8_t  gps_time_synced;    // 是否已同步
static nmea_utc_time gps_last_utc;  // 最近一次 GPS 解析到的 UTC 时间

/**
 * @brief 将 UTC 时间转换为 Unix 时间戳 (秒, 1970-01-01 起算)
 */
static uint32_t utc_to_epoch_sec(nmea_utc_time *utc)
{
    uint32_t y = utc->year;
    uint32_t m = utc->month;
    uint32_t d = utc->date;

    /* 月份修正 (1月/2月视为上一年的13月/14月) */
    if (m <= 2) { m += 12; y--; }

    /* Zeller 公式计算: 1970-01-01 以来的天数 */
    uint32_t days = (d - 1)
                  + (153 * (m + 1)) / 5
                  + 365 * y
                  + y / 4
                  - y / 100
                  + y / 400
                  - 719528;  /* 1970-01-01 的修正天数 */

    return days * 86400UL + utc->hour * 3600UL + utc->min * 60UL + utc->sec;
}

/**
 * @brief 收到 GPS 帧后调用, 以当前 RTOS 毫秒值为基准记录时间
 * @param utc   NMEA 解析得到的 UTC 时间
 * @param now_ms 当前 RTOS 时间 (ms), 如 xTaskGetTickCount() * portTICK_PERIOD_MS
 */
void gps_sync_from_utc(nmea_utc_time *utc, uint32_t now_ms)
{
    gps_epoch_sec    = utc_to_epoch_sec(utc);
    gps_sync_tick_ms = now_ms;
    gps_time_synced  = 1;
    gps_last_utc     = *utc;          // 保存 UTC 结构体副本
}

/**
 * @brief 获取基于 GPS 时间的毫秒级时间戳
 *        = GPS UTC epoch秒 × 1000 + (传入now_ms - 同步时刻now_ms)
 * @param now_ms 当前 RTOS 时间 (ms), 用于计算与GPS基准时刻的偏移
 * @return GPS 时间戳 (ms)
 */
uint32_t gps_get_timestamp_ms(uint32_t now_ms)
{
    if (!gps_time_synced) {
        /* GPS 未同步时, 回退为纯 RTOS ms */
        return now_ms;
    }
    return gps_epoch_sec * 1000UL + (now_ms - gps_sync_tick_ms);
}

/**
 * @brief GPS 时间是否已同步
 * @return 1=已同步, 0=未同步
 */
uint8_t gps_time_is_valid(void)
{
    return gps_time_synced;
}

/**
 * @brief 获取当前 GPS UTC 时间 + ms 偏移 (用于 NodeSample_t 时间戳)
 *        UTC 取自最近一次 GpsDataRead 解析结果,
 *        ms_offset = 当前 RTOS ms - GPS 同步时刻 RTOS ms
 * @param utc       输出 UTC 时间
 * @param ms_offset 输出 ms 偏移 (同步后 < 1000ms)
 * @param now_ms    当前 RTOS 毫秒值
 */
void gps_get_utc_time(nmea_utc_time *utc, uint16_t *ms_offset, uint32_t now_ms)
{
    if (!gps_time_synced) {
        memset(utc, 0, sizeof(nmea_utc_time));
        *ms_offset = 0;
        return;
    }
    *utc       = gps_last_utc;
    *ms_offset = (uint16_t)(now_ms - gps_sync_tick_ms);
}
