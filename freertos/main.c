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
 * FilePath: main.c
 * Created Date: 2022-02-24 16:56:46
 * Last Modified: 2024-07-17 09:15:15
 * Description:  This file is for This file is for AMP example that running rpmsg_echo_task and open scheduler
 * 
 * Modify History: 
 *  Ver   Who        Date         Changes
 * ----- ------     --------    --------------------------------------
 *  1.0 huanghe    2022/03/25  first commit
 *  1.1 huanghe    2023/03/09  Adapt OpenAMP routines based on e2000D/Q
 *  1.2 liusm      2023/11/20  Update example
 */
#include "ftypes.h"
#include "fpsci.h"
#include "fsleep.h"
#include "fprintk.h"
#include "fdebug.h"
#include "portmacro.h"
#include "FreeRTOS.h"
#include "task.h"
#include "slaver_00_example.h"
#include "master.h"
#include "chaos_encrypt.h"
#include "log.h"

static void master_task_create(void)
{
    BaseType_t ret;

    ret = xTaskCreate(master_recv_task,
                      "MasterRecv",
                      MASTER_RECV_STK_SIZE,
                      NULL,
                      MASTER_RECV_TASK_PRIO,
                      NULL);
    if (ret != pdPASS) f_printk("Failed to create master_recv_task\r\n");

    ret = xTaskCreate(master_judge_task,
                      "MasterJudge",
                      MASTER_JUDGE_STK_SIZE,
                      NULL,
                      MASTER_JUDGE_TASK_PRIO,
                      NULL);
    if (ret != pdPASS) f_printk("Failed to create master_judge_task\r\n");

    ret = xTaskCreate(master_cmd_task,
                      "MasterCmd",
                      MASTER_CMD_STK_SIZE,
                      NULL,
                      MASTER_CMD_TASK_PRIO,
                      NULL);
    if (ret != pdPASS) f_printk("Failed to create master_cmd_task\r\n");
}

int main(void)
{
    BaseType_t ret;
    f_printk("freertos %s ,%s \r\n",__DATE__, __TIME__);

    log_init(LOG_LEVEL_INFO);
    log_info("=== Phytium PE2204 Master Controller ===");
    log_info("Build: %s %s", __DATE__, __TIME__);

    chaos_init(0x12345678);
    log_info("Chaos initialized");

    master_init();
    log_info("Master system initialized");

    master_task_create();
    log_info("Master tasks created");

    rpmsg_echo_task();
    vTaskStartScheduler();
    while (1);

FAIL_EXIT:
    f_printk("failed 0x%x \r\n", ret);
    return 0;
}

