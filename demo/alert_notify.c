/*
 * 电网安全预警通知模块 v2.0
 * 支持: 飞书Bot / 企业微信Bot / 邮件 / 本地日志
 *
 * 配置: 修改下方 WEBHOOK_URL 和 PLATFORM 即可
 */
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ═══════════ 配置 ═══════════ */
#define PLATFORM_FEISHU   1   /* 飞书 */
#define PLATFORM_WECOM    2   /* 企业微信 */
#define PLATFORM          PLATFORM_FEISHU  /* ← 选择平台 */

#define FEISHU_URL   ""  /* 飞书Bot Webhook */
#define WECOM_URL    ""  /* 企业微信Bot Webhook */
#define LOG_FILE     "/tmp/grid_alerts.log"
#define MAX_LOG_LINES 500

/* ═══════════ 告警级别 ═══════════ */
typedef enum { LEVEL_NORMAL=0, LEVEL_WARN=1, LEVEL_DANGER=2 } AlertLevel;

/* ═══════════ 告警消息模板 ═══════════ */
typedef struct {
    AlertLevel level;
    char source[32];      /* 告警来源: weather/sensor/system */
    char title[128];      /* 告警标题 */
    char detail[512];     /* 详细信息 */
    char action[256];     /* 建议措施 */
    time_t timestamp;
} AlertMsg;

/* ─── 通过curl发送HTTP请求 ─── */
static int curl_post(const char *url, const char *json) {
    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
        "curl -s -X POST '%s' -H 'Content-Type: application/json' "
        "-d '%s' --connect-timeout 5 2>/dev/null &", url, json);
    return system(cmd);
}

/* ─── 获取当前时间字符串 ─── */
static const char *time_str(const AlertMsg *a) {
    static char buf[32];
    strftime(buf, sizeof(buf), "%m-%d %H:%M:%S", localtime(&a->timestamp));
    return buf;
}

/* ─── 飞书Bot通知 (卡片消息) ─── */
static int feishu_notify(const AlertMsg *a) {
    const char *url = FEISHU_URL;
    if (!url || !url[0]) { printf("[WARN] FEISHU_URL 未配置\n"); return -1; }

    const char *colors[] = {"green", "yellow", "red"};
    const char *titles[] = {"🟢 电网运行正常", "🟡 电网预警", "🔴 电网危险告警"};
    const char *color = colors[a->level > 2 ? 0 : a->level];
    const char *title = titles[a->level > 2 ? 0 : a->level];

    char json[2048];
    snprintf(json, sizeof(json),
        "{\"msg_type\":\"interactive\",\"card\":{"
        "\"header\":{\"title\":{\"content\":\"%s\",\"tag\":\"plain_text\"},"
        "\"template\":\"%s\"},"
        "\"elements\":["
        "{\"tag\":\"div\",\"text\":{\"tag\":\"lark_md\","
        "\"content\":\"**告警来源**: %s\\n**详情**: %s\\n**建议措施**: %s\\n**时间**: %s\"}},"
        "{\"tag\":\"hr\"},"
        "{\"tag\":\"note\",\"elements\":[{\"tag\":\"plain_text\","
        "\"content\":\"Phytium Pi 电网安全监测系统\"}]}]}}",
        title, color, a->source, a->detail, a->action, time_str(a));

    return curl_post(url, json);
}

/* ─── 企业微信Bot通知 ─── */
static int wecom_notify(const AlertMsg *a) {
    const char *url = WECOM_URL;
    if (!url || !url[0]) { printf("[WARN] WECOM_URL 未配置\n"); return -1; }

    const char *icons[] = {"🟢", "🟡", "🔴"};
    char json[2048];
    snprintf(json, sizeof(json),
        "{\"msgtype\":\"markdown\",\"markdown\":{\"content\":\""
        "%s **电网安全告警**\\n"
        ">来源: <font color=\\\"warning\\\">%s</font>\\n"
        ">级别: <font color=\\\"%s\\\">%s</font>\\n"
        ">详情: %s\\n"
        ">建议: %s\\n"
        ">时间: %s\"}}",
        icons[a->level > 2 ? 0 : a->level],
        a->source,
        a->level == 2 ? "red" : "warning",
        a->level == 2 ? "🔴危险" : (a->level == 1 ? "🟡预警" : "🟢正常"),
        a->detail, a->action, time_str(a));

    return curl_post(url, json);
}

/* ─── 发送通知 (根据平台自动选择) ─── */
static int push_notify(const AlertMsg *a) {
#if PLATFORM == PLATFORM_FEISHU
    return feishu_notify(a);
#elif PLATFORM == PLATFORM_WECOM
    return wecom_notify(a);
#else
    return -1;
#endif
}

/* ─── 日志记录 ─── */
static void log_alert(const AlertMsg *a) {
    FILE *fp = fopen(LOG_FILE, "a");
    if (!fp) return;
    fprintf(fp, "[%s] %s | %s | %s | %s\n",
            ctime(&a->timestamp),
            a->level == 2 ? "DANGER" : (a->level == 1 ? "WARN" : "NORMAL"),
            a->source, a->title, a->detail);
    fclose(fp);

    /* 日志滚动 */
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "tail -%d %s > %s.tmp && mv %s.tmp %s",
             MAX_LOG_LINES, LOG_FILE, LOG_FILE, LOG_FILE, LOG_FILE);
    system(cmd);
}

/* ─── 发送告警 (统一入口) ─── */
int alert_send(AlertLevel level, const char *source,
               const char *title, const char *detail, const char *action) {
    if (level == LEVEL_NORMAL) return 0; /* 正常不推送 */

    AlertMsg a = {
        .level = level, .timestamp = time(NULL)
    };
    snprintf(a.source, sizeof(a.source), "%s", source);
    snprintf(a.title, sizeof(a.title), "%s", title);
    snprintf(a.detail, sizeof(a.detail), "%s", detail);
    snprintf(a.action, sizeof(a.action), "%s", action);

    log_alert(&a);
    int ret = push_notify(&a);

    printf("[ALERT] %s | %s → %s (push:%d)\n",
           a.level == 2 ? "🔴" : "🟡", a.source, a.title, ret);
    return ret;
}

/* ─── 天气告警 (供 weather_monitor 集成调用) ─── */
int alert_weather(int level, const char *msg, const char *suggest) {
    return alert_send(level, "weather",
                      "气象电网风险", msg, suggest);
}

/* ─── 传感器告警 ─── */
int alert_sensor(int level, int node_id, const char *param, float value, float threshold) {
    char title[128], detail[256], action[128];
    snprintf(title, sizeof(title), "节点%d %s异常", node_id, param);
    snprintf(detail, sizeof(detail), "节点%d %s=%.2f, 阈值=%.2f", node_id, param, value, threshold);
    snprintf(action, sizeof(action), "检查节点%d硬件状态, 必要时切备用电源", node_id);
    return alert_send(level, "sensor", title, detail, action);
}

/* ═══════════ 测试 ═══════════ */
int main(int argc, char *argv[]) {
    printf("=== 电网预警通知模块测试 ===\n\n");

    /* 测试1: 配置检查 */
#if PLATFORM == PLATFORM_FEISHU
    printf("[PLATFORM] 飞书\n");
    if (!FEISHU_URL[0]) {
        printf("[CONFIG] FEISHU_URL 未配置\n");
        printf("  获取: 飞书 → 群设置 → 群机器人 → 添加 → 复制Webhook\n\n");
    }
#elif PLATFORM == PLATFORM_WECOM
    printf("[PLATFORM] 企业微信\n");
    if (!WECOM_URL[0]) {
        printf("[CONFIG] WECOM_URL 未配置\n");
        printf("  获取: 企业微信 → 群设置 → 群机器人 → 添加 → 复制Webhook\n\n");
    }
#endif

    /* 测试2: 各级别告警 (日志记录, WeCom仅在有Webhook时发送) */
    printf("─── 天气预警 (WARN) ───\n");
    alert_weather(LEVEL_WARN, "风速35km/h, 注意线路摆动",
                  "关注频率波动, 准备降功率");

    printf("\n─── 传感器告警 (DANGER) ───\n");
    alert_sensor(LEVEL_DANGER, 3, "电压", 245.0, 230.0);

    printf("\n─── 天气危险 (DANGER) ───\n");
    alert_weather(LEVEL_DANGER, "雷暴+大风55km/h, 断线风险",
                  "分布式电源切孤岛模式, 启动应急抢修");

    printf("\n─── 正常状态 (不推送) ───\n");
    alert_weather(LEVEL_NORMAL, "天气正常", "");

    printf("\n[INFO] 告警日志: %s\n", LOG_FILE);
    printf("[INFO] 如需测试企业微信推送, 请:\n");
    printf("  1. 企业微信创建群机器人\n");
    printf("  2. 修改 WEBHOOK_URL\n");
    printf("  3. 重新编译运行\n");

    return 0;
}
