/*
 * sim_node_task.c — FreeRTOS 电力系统仿真任务 (v2)
 * ==================================================
 * 在 FreeRTOS 上运行 IEEE 5-Bus/2-Gen 动态仿真:
 *   1. RK4 积分转子运动方程 (2000Hz)
 *   2. 计算测量向量 h(x)
 *   3. 批量打包通过 RPMsg 发送到 Linux
 */

#include "FreeRTOS.h"
#include "task.h"
#include "platform_info.h"     /* platform_poll_nonblocking */
#include <openamp/open_amp.h>
#include <metal/alloc.h>
#include "rpmsg_service.h"     /* RPMSG_SERVICE_NAME */
#include "rpmsg_proto.h"       /* RpmsgPkt, RPMSG_PKT_HDR_SIZE */

#include "sim_node_task.h"
#include "sim_math.h"
#include "sim_params_5bus.h"
#include "shm_print.h"
#include "shm_data.h"

/* ─── 外部声明 (main.c 暴露) ─── */
extern struct remoteproc    g_rproc;
extern struct rpmsg_device *g_rpdev;

/* ─── 全局状态 ─── */
static struct rpmsg_endpoint sim_ept;
static int sim_running = 0;
static int sim_speed = 1;        /* 0=最快, 1=实时(默认), 2+半速 */
static unsigned int sim_total_sent = 0;

/* 直接取址宏 (3D 数组直接用 C 编译器索引) */
#define YR(i,j,ps) (SIM_5BUS_YBUS_REAL[i][j][ps])
#define YI(i,j,ps) (SIM_5BUS_YBUS_IMAG[i][j][ps])
#define RR(i,j,ps) (SIM_5BUS_RV_REAL[i][j][ps])
#define RI(i,j,ps) (SIM_5BUS_RV_IMAG[i][j][ps])

/* ========================================================================
 * RPMsg 回调
 * ======================================================================== */

static int sim_ept_cb(struct rpmsg_endpoint *ept, void *data,
                       size_t len, uint32_t src, void *priv)
{
    (void)priv;

    /* data 指向 RpmsgPkt (6字节头 + payload), 与 main.c 的 rpmsg_endpoint_cb 一致 */
    RpmsgPkt *pkt = (RpmsgPkt *)data;
    if (len < RPMSG_PKT_HDR_SIZE) return 0;

    switch (pkt->command) {
    case CMD_SIM_CTRL: {
        SimCtrlPkt *ctrl = (SimCtrlPkt *)pkt->data;
        if (ctrl->cmd == SIM_CTRL_START) {
            /* 每次收到 START 都更新 dest_addr 到最新发送者 */
            if (src != RPMSG_ADDR_ANY && ept) ept->dest_addr = src;
            sim_running = 1;
            sim_total_sent = 0;
            shm_spf("[SIM] START\n");
        } else if (ctrl->cmd == SIM_CTRL_STOP) {
            sim_running = 0;
            shm_spf("[SIM] STOP (sent=%u)\n", sim_total_sent);
        } else if (ctrl->cmd == SIM_CTRL_RESET) {
            sim_running = 0;
            sim_total_sent = 0;
            shm_spf("[SIM] RESET\n");
        } else if (ctrl->cmd == SIM_CTRL_SPEED) {
            sim_speed = (int)ctrl->data;
            shm_spf("[SIM] SPEED=%d\n", sim_speed);
        }
        break;
    }
    default:
        /* 可能是 ECHO_REQ 等绑定消息, 忽略 */
        break;
    }
    return 0;
}

static void sim_ept_unbind_cb(struct rpmsg_endpoint *ept)
{
    (void)ept;
    shm_spf("[SIM] endpoint unbound\n");
}

/* ========================================================================
 * 主任务
 * ======================================================================== */
void sim_node_task(void *pv)
{
    (void)pv;

    float x[4];
    float yr[2][2], yi[2][2], rr[5][2], ri[5][2];
    /* 用固定大小数组代替 packed struct 避免 alignment 问题 */
    static uint8_t batch_buf[500] __attribute__((aligned(4)));
    SimDataBatch *batch = (SimDataBatch *)batch_buf;
    int batch_idx = 0;
    unsigned int step = 0;
    int ps;

    /* ── 1. 初始化状态 ── */
    for (int i = 0; i < 4; i++) x[i] = SIM_5BUS_X0[i];

    shm_spf("[SIM] init: %d-gen/%d-bus, dt=%.4fs, %d steps\n",
            SIM_5BUS_N_GEN, SIM_5BUS_N_BUS,
            SIM_5BUS_DT, SIM_5BUS_NUM_STEPS);
    shm_init_region(SHM_5BUS_BASE, 64);

    /* ── 2. 创建 RPMsg endpoint (独立通道名, 避免与任务一冲突) ── */
    int ret = rpmsg_create_ept(&sim_ept, g_rpdev,
                                "rpmsg-sim-5bus",   /* 独立通道名 */
                                0,                    /* src */
                                RPMSG_ADDR_ANY,       /* dest */
                                sim_ept_cb,
                                sim_ept_unbind_cb);
    shm_spf("[SIM] ept: ret=%d src=0x%X\n", ret, sim_ept.addr);

    /* ── 3. 等待 Linux 绑定 ── */
    shm_spf("[SIM] waiting for Linux bind...\n");
    while (sim_ept.dest_addr == RPMSG_ADDR_ANY) {
        platform_poll_nonblocking(&g_rproc);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    shm_spf("[SIM] bound: dest=0x%X\n", sim_ept.dest_addr);

    /* ── 4. 等待 START ── */
    shm_spf("[SIM] ready, waiting for START...\n");
    while (!sim_running) {
        platform_poll_nonblocking(&g_rproc);
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    /* ── 5. 主仿真循环 ── */
    batch->node_id = 0;
    batch->gen_count = SIM_5BUS_N_GEN;
    batch->bus_count = SIM_5BUS_N_BUS;
    batch->frame_count = 0;
    batch->start_seq = 0;

    TickType_t last_hb_tick = xTaskGetTickCount();
    int last_ps = -1;

    while (sim_running && step < SIM_5BUS_NUM_STEPS) {
        float t = step * SIM_5BUS_DT;

        /* 确定故障状态 */
        ps = sim_get_fault_state(t, SIM_5BUS_FAULT_START, SIM_5BUS_FAULT_END);

        /* 仅当故障状态变化时重建 YBUS/RV 切片 */
        if (ps != last_ps) {
            for (int i = 0; i < 2; i++)
                for (int j = 0; j < 2; j++) {
                    yr[i][j] = YR(i,j,ps);
                    yi[i][j] = YI(i,j,ps);
                }
            for (int i = 0; i < 5; i++)
                for (int j = 0; j < 2; j++) {
                    rr[i][j] = RR(i,j,ps);
                    ri[i][j] = RI(i,j,ps);
                }
            last_ps = ps;
        }

        /* RK4 一步积分 */
        sim_rk4_step_5bus(x, SIM_5BUS_DT,
            yr, yi, SIM_5BUS_E_ABS, SIM_5BUS_PM,
            SIM_5BUS_M, SIM_5BUS_D);

        /* 计算测量向量 → 写入批量缓冲 */
        sim_compute_meas_5bus(x,
            batch->Z_batch[batch_idx], yr, yi, rr, ri, SIM_5BUS_E_ABS);

        /* 写共享内存 (降采样: 只影响SHM, 不影响RPMsg批处理) */
        if (step % SIM_5BUS_WRITE_DOWN == 0) {
            { u8 fbuf[64]; *(u32*)fbuf=(u32)(t*1000.0f); *(u16*)(fbuf+4)=(u16)step; fbuf[6]=0; fbuf[7]=SIM_5BUS_N_GEN; memcpy(fbuf+8,batch->Z_batch[batch_idx],14*sizeof(float)); shm_put_frame(SHM_5BUS_BASE, SHM_5BUS_SIZE, fbuf, 64); }
        }
        batch_idx++;
        batch->frame_count = (uint8_t)batch_idx;

        /* 批量满 → 发送 */
        if (batch_idx >= SIM_BATCH_SIZE) {
            batch->start_seq = (uint16_t)(step - SIM_BATCH_SIZE + 1);
            batch->ts_ms_start = (uint32_t)(t * 1000.0f);

            int batch_bytes = 10 + SIM_BATCH_SIZE * 14 * (int)sizeof(float);
            ret = rpmsg_send(&sim_ept, batch, (unsigned long)batch_bytes);
            if (ret >= 0) {
                sim_total_sent += SIM_BATCH_SIZE;
            }

            batch_idx = 0;
            batch->frame_count = 0;

            /* 速度控制: 0=高频(1ms), 1=实时(4ms), 2=半速(8ms) */
            if (sim_speed >= 2) {
                vTaskDelay(pdMS_TO_TICKS(8));
            } else if (sim_speed == 1) {
                vTaskDelay(pdMS_TO_TICKS(4));
            } else {
                vTaskDelay(pdMS_TO_TICKS(1)); /* speed=0: 1ms延迟, 稳定高频 */
            }

            /* 处理 RPMsg 消息 */
            platform_poll_nonblocking(&g_rproc);
        }

        step++;

        /* 心跳 (每秒) */
        if ((step % SIM_5BUS_FS) == 0) {
            TickType_t now = xTaskGetTickCount();
            shm_spf("[SIM] t=%.0fs step=%u sent=%u dt=%ums\n",
                    t, step, sim_total_sent,
                    (unsigned int)((now - last_hb_tick) * portTICK_PERIOD_MS));
            last_hb_tick = now;
        }
    }

    /* ── 6. 发送最后一批 ── */
    if (batch_idx > 0) {
        batch->start_seq = (uint16_t)(step - (unsigned int)batch_idx);
        batch->ts_ms_start = (uint32_t)((step - (unsigned int)batch_idx) * SIM_5BUS_DT * 1000.0f);
        int batch_bytes = 10 + batch_idx * 14 * (int)sizeof(float);
        rpmsg_send(&sim_ept, batch, (unsigned long)batch_bytes);
        sim_total_sent += (unsigned int)batch_idx;
    }

    /* ── 7. 完成通知 ── */
    SimAckPkt done;
    done.ack_cmd = CMD_SIM_DONE;
    done.status = 0;
    done.reserved = 0;
    rpmsg_send(&sim_ept, &done, sizeof(done));

    shm_spf("[SIM] DONE: %u of %d frames, %u steps\n",
            sim_total_sent, SIM_5BUS_NUM_STEPS, step);

    while (1) {
        platform_poll_nonblocking(&g_rproc);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
