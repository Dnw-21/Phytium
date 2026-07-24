/* sim_node_39bus.c — FreeRTOS 10-Gen/39-Bus 仿真任务 */
#include "FreeRTOS.h"
#include "task.h"
#include "platform_info.h"
#include <openamp/open_amp.h>
#include <metal/alloc.h>
#include "rpmsg_service.h"
#include "rpmsg_proto.h"
#include "sim_node_task.h"   /* SimCtrlPkt, SimAckPkt, CMD_SIM_DONE */
#include "sim_params_39bus.h"
#include <math.h>
#include "shm_data.h"

extern struct remoteproc    g_rproc;
extern struct rpmsg_device *g_rpdev;

static struct rpmsg_endpoint sim39_ept;
static int sim39_running = 0;
static int sim39_speed = 0;  /* 0=最快, 1=实时, 2+=半速 */
static unsigned int sim39_sent = 0;

#define N_GEN  10
#define N_BUS  39
#define N_STATE 20
#define N_MEAS 98
#define BATCH39_SIZE 8

/* YR/YI/RR/RI 宏: 3D数组直接索引 */
#define YR(i,j,ps) (SIM_39BUS_YBUS_REAL[i][j][ps])
#define YI(i,j,ps) (SIM_39BUS_YBUS_IMAG[i][j][ps])
#define RR(i,j,ps) (SIM_39BUS_RV_REAL[i][j][ps])
#define RI(i,j,ps) (SIM_39BUS_RV_IMAG[i][j][ps])

/* 简易复数矩阵运算 (内联) */
static void cmul_vec(int n, const float *Yr, const float *Yi,
    const float *Er, const float *Ei, float *Ir, float *Ii) {
    for(int i=0;i<n;i++){Ir[i]=Ii[i]=0;
        for(int j=0;j<n;j++){float a=Yr[i*n+j],b=Yi[i*n+j],c=Er[j],d=Ei[j];
            Ir[i]+=a*c-b*d; Ii[i]+=a*d+b*c;}}
}
static void cmul_rv(int nb,int ng,const float *Rr,const float *Ri,
    const float *Er,const float *Ei,float *Vr,float *Vi){
    for(int i=0;i<nb;i++){Vr[i]=Vi[i]=0;
        for(int j=0;j<ng;j++){float a=Rr[i*ng+j],b=Ri[i*ng+j],c=Er[j],d=Ei[j];
            Vr[i]+=a*c-b*d; Vi[i]+=a*d+b*c;}}
}

/* 导数函数 */
static void deriv(const float x[N_STATE], float dx[N_STATE],
    const float Yr[N_GEN][N_GEN], const float Yi[N_GEN][N_GEN], int ps) {
    float Er[N_GEN],Ei[N_GEN],Ir[N_GEN],Ii[N_GEN];
    for(int i=0;i<N_GEN;i++){Er[i]=SIM_39BUS_E_ABS[i]*cosf(x[i]);Ei[i]=SIM_39BUS_E_ABS[i]*sinf(x[i]);}
    cmul_vec(N_GEN,(float*)Yr,(float*)Yi,Er,Ei,Ir,Ii);
    for(int i=0;i<N_GEN;i++){
        float Pe=Er[i]*Ir[i]+Ei[i]*Ii[i];
        dx[i]=x[N_GEN+i];
        dx[N_GEN+i]=(SIM_39BUS_PM[i]-Pe-SIM_39BUS_D[i]*x[N_GEN+i])/SIM_39BUS_M[i];
    }
}

/* RK4 单步 */
static void rk4_step(float x[N_STATE], float dt,
    const float Yr[N_GEN][N_GEN], const float Yi[N_GEN][N_GEN], int ps) {
    float k1[N_STATE],k2[N_STATE],k3[N_STATE],k4[N_STATE],xt[N_STATE];
    deriv(x,k1,Yr,Yi,ps);
    for(int i=0;i<N_STATE;i++){k1[i]*=dt;xt[i]=x[i]+0.5f*k1[i];}
    deriv(xt,k2,Yr,Yi,ps);
    for(int i=0;i<N_STATE;i++){k2[i]*=dt;xt[i]=x[i]+0.5f*k2[i];}
    deriv(xt,k3,Yr,Yi,ps);
    for(int i=0;i<N_STATE;i++){k3[i]*=dt;xt[i]=x[i]+k3[i];}
    deriv(xt,k4,Yr,Yi,ps);
    for(int i=0;i<N_STATE;i++){k4[i]*=dt;x[i]+=(k1[i]+2*k2[i]+2*k3[i]+k4[i])/6.0f;}
}

/* 测量函数 */
static void meas(const float x[N_STATE], float Z[N_MEAS],
    const float Yr[N_GEN][N_GEN], const float Yi[N_GEN][N_GEN],
    const float Rr[N_BUS][N_GEN], const float Ri[N_BUS][N_GEN], int ps) {
    float Er[N_GEN],Ei[N_GEN],Ir[N_GEN],Ii[N_GEN],Vr[N_BUS],Vi[N_BUS];
    for(int i=0;i<N_GEN;i++){Er[i]=SIM_39BUS_E_ABS[i]*cosf(x[i]);Ei[i]=SIM_39BUS_E_ABS[i]*sinf(x[i]);}
    cmul_vec(N_GEN,(float*)Yr,(float*)Yi,Er,Ei,Ir,Ii);
    for(int i=0;i<N_GEN;i++){Z[i]=Er[i]*Ir[i]+Ei[i]*Ii[i];Z[N_GEN+i]=Ei[i]*Ir[i]-Er[i]*Ii[i];}
    cmul_rv(N_BUS,N_GEN,(float*)Rr,(float*)Ri,Er,Ei,Vr,Vi);
    for(int i=0;i<N_BUS;i++){Z[2*N_GEN+i]=sqrtf(Vr[i]*Vr[i]+Vi[i]*Vi[i]);Z[2*N_GEN+N_BUS+i]=atan2f(Vi[i],Vr[i]);}
}

/* ─── RPMsg 回调 ─── */
static int sim39_cb(struct rpmsg_endpoint *ept, void *data, size_t len, uint32_t src, void *priv) {
    (void)priv;
    RpmsgPkt *pkt = (RpmsgPkt *)data;
    if (len < RPMSG_PKT_HDR_SIZE) return 0;
    if (pkt->command == 0x0060) { /* CMD_SIM39_CTRL */
        uint8_t cmd = pkt->data[0];
        if (cmd == 1) {
            if (src != RPMSG_ADDR_ANY && ept) ept->dest_addr = src;
            sim39_running = 1; sim39_sent = 0; }
        else if (cmd == 0) { sim39_running = 0; }
        else if (cmd == 3) { sim39_speed = (int)((SimCtrlPkt *)pkt->data)->data; }
    }
    return 0;
}
static void sim39_unbind(struct rpmsg_endpoint *ept) { (void)ept; }

/* 主任务入口 */
void sim_node_39bus_task(void *pv) {
    (void)pv;
    float x[N_STATE]; for(int i=0;i<N_STATE;i++) x[i]=SIM_39BUS_X0[i];
    float Yr[N_GEN][N_GEN], Yi[N_GEN][N_GEN], Rr[N_BUS][N_GEN], Ri[N_BUS][N_GEN];

    if (!g_rpdev) { while(1) vTaskDelay(pdMS_TO_TICKS(1000)); }

    shm_init_region(SHM_39BUS_BASE, 400);  /* 400B per frame, ri=0 */

    int ret = rpmsg_create_ept(&sim39_ept, g_rpdev, "rpmsg-sim-39bus", 10, RPMSG_ADDR_ANY, sim39_cb, sim39_unbind);
    if (ret) { while(1) vTaskDelay(pdMS_TO_TICKS(1000)); }

    while (sim39_ept.dest_addr == RPMSG_ADDR_ANY) { platform_poll_nonblocking(&g_rproc); vTaskDelay(pdMS_TO_TICKS(10)); }

    while (!sim39_running) { platform_poll_nonblocking(&g_rproc); vTaskDelay(pdMS_TO_TICKS(100)); }

    int last_ps = -1; unsigned int step = 0;
    int batch39 = 0;

    while (sim39_running && step < SIM_39BUS_NUM_STEPS) {
        float t = step * SIM_39BUS_DT;
        int ps = (t < SIM_39BUS_FAULT_START) ? 0 : ((t <= SIM_39BUS_FAULT_END) ? 1 : 2);

        if (ps != last_ps) {
            for(int i=0;i<N_GEN;i++) for(int j=0;j<N_GEN;j++) { Yr[i][j]=YR(i,j,ps); Yi[i][j]=YI(i,j,ps); }
            for(int i=0;i<N_BUS;i++) for(int j=0;j<N_GEN;j++) { Rr[i][j]=RR(i,j,ps); Ri[i][j]=RI(i,j,ps); }
            last_ps = ps;
        }

        rk4_step(x, SIM_39BUS_DT, Yr, Yi, ps);
        float Z[N_MEAS]; meas(x, Z, Yr, Yi, Rr, Ri, ps);
        if (step % SIM_39BUS_WRITE_DOWN == 0) {
            u8 fbuf[400];
            *(u32*)fbuf=(u32)(t*1000.0f);
            *(u16*)(fbuf+4)=(u16)step;
            fbuf[6]=0; fbuf[7]=N_GEN;
            memcpy(fbuf+8, Z, N_MEAS*sizeof(float));
            /* 背压: SHM 满时阻塞等待, 直到 Linux UKF 消费数据 */
            u32 base = SHM_39BUS_BASE;
            u32 buf_size = SHM_39BUS_SIZE;
            u32 fsz  = SHM_FSZ(base);
            u32 data_size = buf_size - 16;
            u32 cap = data_size / fsz;
            while (1) {
                u32 cnt = SHM_CNT(base);
                u32 ri  = SHM_RI(base);
                if ((cnt - ri) < cap) break;
                vTaskDelay(pdMS_TO_TICKS(1));
            }
            shm_put_frame(SHM_39BUS_BASE, SHM_39BUS_SIZE, fbuf, 400);
        }

        step++;
        sim39_sent++;
        batch39++;

        /* 批量速度控制 (仿照 5bus, 每 8 帧延迟一次) */
        if (batch39 >= BATCH39_SIZE || step >= SIM_39BUS_NUM_STEPS) {
            platform_poll_nonblocking(&g_rproc);
            if (sim39_speed >= 2) {
                vTaskDelay(pdMS_TO_TICKS(8));
            } else if (sim39_speed == 1) {
                vTaskDelay(pdMS_TO_TICKS(4));
            } else {
                vTaskDelay(1);  /* speed=0: 最小 yield, 防止饥饿 */
            }
            batch39 = 0;
        }

    }
    /* ── 发送 SIM_DONE ── */
    {
        SimAckPkt done;
        done.ack_cmd = CMD_SIM_DONE;
        done.status = 0;
        done.reserved = 0;
        rpmsg_send(&sim39_ept, &done, sizeof(done));
    }
    while(1) vTaskDelay(pdMS_TO_TICKS(1000));
}
