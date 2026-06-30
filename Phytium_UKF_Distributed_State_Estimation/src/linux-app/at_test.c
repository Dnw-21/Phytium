#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdint.h>
#include <sys/select.h>
#include <sys/mman.h>
#include <termios.h>

#define GPIO_DR_OFFSET   0x00
#define GPIO_DDR_OFFSET  0x04
#define GPIO_EXT_OFFSET  0x08
#define GPIO3_BASE       0x28037000U
#define GPIO2_BASE       0x28036000U
#define FIOPAD_BASE      0x32B30000U
#define FIOPAD_A37_REG0_OFF  0x00C4U
#define FIOPAD_C49_REG0_OFF  0x00E0U
#define MD0_PIN 1
#define AUX_PIN 10

static int mem_fd;
static volatile uint32_t *g3, *g2, *fio;
static int uart_fd;

static void gpio_wr(volatile uint32_t *b, uint32_t o, uint32_t v) { *(volatile uint32_t *)((uintptr_t)b+o)=v; }
static uint32_t gpio_rd(volatile uint32_t *b, uint32_t o) { return *(volatile uint32_t *)((uintptr_t)b+o); }
static void md0(int h) {
    gpio_wr(g3, GPIO_DDR_OFFSET, gpio_rd(g3, GPIO_DDR_OFFSET)|(1U<<MD0_PIN));
    uint32_t dr = gpio_rd(g3, GPIO_DR_OFFSET);
    gpio_wr(g3, GPIO_DR_OFFSET, h?(dr|(1U<<MD0_PIN)):(dr&~(1U<<MD0_PIN)));
}
static int aux(void) { return (gpio_rd(g2, GPIO_EXT_OFFSET)>>AUX_PIN)&1; }

static int at_send(const char *cmd, int timeout_ms)
{
    unsigned char resp[256];
    tcflush(uart_fd, TCIOFLUSH);
    write(uart_fd, cmd, strlen(cmd));
    write(uart_fd, "\r\n", 2);
    usleep(150000);
    fd_set fds; struct timeval tv;
    int total=0, first_to=timeout_ms;
    while(total<255){
        FD_ZERO(&fds); FD_SET(uart_fd, &fds);
        tv.tv_sec=first_to/1000; tv.tv_usec=(first_to%1000)*1000;
        if(select(uart_fd+1, &fds, NULL, NULL, &tv)<=0) break;
        int n=read(uart_fd, resp+total, 255-total);
        if(n>0) total+=n; else break;
        first_to=100;
    }
    if(total>0){
        resp[total]='\0';
        while(total>0&&(resp[total-1]=='\r'||resp[total-1]=='\n')) resp[--total]='\0';
        printf("  <<< %s\n", resp);
        return (strstr((char*)resp,"OK")||strstr((char*)resp,"+OK"))?1:0;
    }
    printf("  <<< (timeout)\n");
    return 0;
}

int main(void)
{
    setbuf(stdout, NULL);

    mem_fd = open("/dev/mem", O_RDWR|O_SYNC);
    g3=mmap(NULL,0x1000,PROT_READ|PROT_WRITE,MAP_SHARED,mem_fd,GPIO3_BASE&~0xFFFUL);
    g2=mmap(NULL,0x1000,PROT_READ|PROT_WRITE,MAP_SHARED,mem_fd,GPIO2_BASE&~0xFFFUL);
    fio=mmap(NULL,0x1000,PROT_READ|PROT_WRITE,MAP_SHARED,mem_fd,FIOPAD_BASE&~0xFFFUL);

    gpio_wr(fio, FIOPAD_C49_REG0_OFF, (gpio_rd(fio, FIOPAD_C49_REG0_OFF)&~7)|6);
    gpio_wr(fio, FIOPAD_A37_REG0_OFF, (gpio_rd(fio, FIOPAD_A37_REG0_OFF)&~7)|6);

    uart_fd = open("/dev/ttyAMA2", O_RDWR|O_NOCTTY);
    struct termios tty; memset(&tty,0,sizeof(tty));
    tcgetattr(uart_fd, &tty);
    cfsetospeed(&tty,B115200); cfsetispeed(&tty,B115200);
    tty.c_cflag &= ~(PARENB|CSTOPB|CSIZE|CRTSCTS);
    tty.c_cflag |= CS8|CREAD|CLOCAL;
    tty.c_lflag &= ~(ICANON|ECHO|ECHOE|ISIG);
    tty.c_iflag &= ~(IXON|IXOFF|IXANY|ICRNL|INLCR);
    tty.c_oflag &= ~OPOST;
    tty.c_cc[VMIN]=0; tty.c_cc[VTIME]=1;
    tcsetattr(uart_fd,TCSANOW,&tty);
    tcflush(uart_fd, TCIOFLUSH);

    printf("=== AT Command Tester ===\n\n");

    printf("[1] Enter AT mode (MD0=HIGH)...\n");
    md0(1); usleep(500000);

    printf("AT test: "); at_send("AT",500);

    printf("\n=== Testing CHANNEL commands ===\n");
    printf("AT+CHN=23: "); at_send("AT+CHN=23",500);
    printf("AT+CHANNEL=23: "); at_send("AT+CHANNEL=23",500);
    printf("AT+WLRATE=23,5: "); at_send("AT+WLRATE=23,5",500);

    printf("\n=== Testing RATE commands ===\n");
    printf("AT+WLRATE=5: "); at_send("AT+WLRATE=5",500);
    printf("AT+AIRRATE=5: "); at_send("AT+AIRRATE=5",500);

    printf("\n=== Testing POWER commands ===\n");
    printf("AT+POWER=4: "); at_send("AT+POWER=4",500);
    printf("AT+TPOWER=4: "); at_send("AT+TPOWER=4",500);

    printf("\n=== Testing BAUD commands ===\n");
    printf("AT+BPS=7: "); at_send("AT+BPS=7",500);
    printf("AT+BAUD=7: "); at_send("AT+BAUD=7",500);
    printf("AT+UART=7,0: "); at_send("AT+UART=7,0",500);

    printf("\n=== Testing QUERY commands ===\n");
    printf("AT+ADDR?: "); at_send("AT+ADDR?",1000);
    printf("AT+CFG?: "); at_send("AT+CFG?",1500);
    printf("AT+VER?: "); at_send("AT+VER?",500);
    printf("AT+VER=?: "); at_send("AT+VER=?",500);

    printf("\n[OK] MD0=LOW (data mode)\n");
    md0(0); usleep(1000000);
    printf("AUX=%d\n", aux());

    close(uart_fd);
    munmap((void*)g3,0x1000); munmap((void*)g2,0x1000); munmap((void*)fio,0x1000);
    close(mem_fd);
    return 0;
}
