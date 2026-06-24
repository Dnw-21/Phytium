#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/mman.h>

#define GPIO_DR_OFFSET   0x00
#define GPIO_DDR_OFFSET  0x04
#define GPIO_EXT_OFFSET  0x08
#define GPIO2_BASE       0x28036000U
#define GPIO3_BASE       0x28037000U
#define FIOPAD_BASE      0x32B30000U

static volatile uint32_t *g2, *g3, *fio;
static uint32_t r(volatile uint32_t *b, uint32_t o) { return *(volatile uint32_t*)((uintptr_t)b+o); }
static void w(volatile uint32_t *b, uint32_t o, uint32_t v) { *(volatile uint32_t*)((uintptr_t)b+o)=v; }

int main(void)
{
    setbuf(stdout,NULL);
    int fd=open("/dev/mem",O_RDWR|O_SYNC);
    g2=mmap(NULL,0x1000,PROT_READ|PROT_WRITE,MAP_SHARED,fd,GPIO2_BASE&~0xFFFUL);
    g3=mmap(NULL,0x1000,PROT_READ|PROT_WRITE,MAP_SHARED,fd,GPIO3_BASE&~0xFFFUL);
    fio=mmap(NULL,0x1000,PROT_READ|PROT_WRITE,MAP_SHARED,fd,FIOPAD_BASE&~0xFFFUL);

    /* Set IOMUX */
    w(fio,0xC4,(r(fio,0xC4)&~7)|6);
    w(fio,0xE0,(r(fio,0xE0)&~7)|6);

    printf("=== AUX Pin Monitor (GPIO2_10, Pin7) ===\n\n");

    /* Check all GPIO2_EXT bits */
    printf("[READ] GPIO2 full regs:\n");
    printf("  GPIO2_DR  = 0x%04X\n", r(g2,GPIO_DR_OFFSET));
    printf("  GPIO2_DDR = 0x%04X\n", r(g2,GPIO_DDR_OFFSET));
    printf("  GPIO2_EXT = 0x%04X\n", r(g2,GPIO_EXT_OFFSET));
    printf("  bit10(AUX)=%d  bit7=%d  bit8=%d  bit9=%d  bit11=%d\n",
           (r(g2,GPIO_EXT_OFFSET)>>10)&1,
           (r(g2,GPIO_EXT_OFFSET)>>7)&1,
           (r(g2,GPIO_EXT_OFFSET)>>8)&1,
           (r(g2,GPIO_EXT_OFFSET)>>9)&1,
           (r(g2,GPIO_EXT_OFFSET)>>11)&1);

    /* Set MD0 to output, keep low */
    w(g3,GPIO_DDR_OFFSET, r(g3,GPIO_DDR_OFFSET)|(1U<<1));
    w(g3,GPIO_DR_OFFSET, r(g3,GPIO_DR_OFFSET)&~(1U<<1));
    printf("\n[MD0] LOW (data mode)\n");

    /* Monitor AUX for 5 seconds in data mode */
    printf("[AUX] Monitoring 5s in data mode:\n");
    int changes=0, last=-1;
    for(int i=0;i<50;i++){
        int a=(r(g2,GPIO_EXT_OFFSET)>>10)&1;
        if(a!=last){changes++; last=a; printf("  t=%d.%ds AUX=%d CHANGE!\n",i/10,i%10,a);}
        usleep(100000);
    }
    printf("[AUX] data mode: changes=%d last=%d\n",changes,last);

    /* Now enter AT mode */
    w(g3,GPIO_DR_OFFSET, r(g3,GPIO_DR_OFFSET)|(1U<<1));
    printf("\n[MD0] HIGH (AT mode)\n");

    printf("[AUX] Monitoring 5s in AT mode:\n");
    changes=0; last=-1;
    for(int i=0;i<50;i++){
        int a=(r(g2,GPIO_EXT_OFFSET)>>10)&1;
        if(a!=last){changes++; last=a; printf("  t=%d.%ds AUX=%d CHANGE!\n",i/10,i%10,a);}
        usleep(100000);
    }
    printf("[AUX] AT mode: changes=%d last=%d\n",changes,last);

    /* Back to data mode */
    w(g3,GPIO_DR_OFFSET, r(g3,GPIO_DR_OFFSET)&~(1U<<1));
    printf("\n[MD0] LOW (back to data mode)\n");

    printf("[AUX] Monitoring 10s after exit AT:\n");
    changes=0; last=-1;
    for(int i=0;i<100;i++){
        int a=(r(g2,GPIO_EXT_OFFSET)>>10)&1;
        if(a!=last){changes++; last=a; printf("  t=%d.%ds AUX=%d CHANGE!\n",i/10,i%10,a);}
        usleep(100000);
    }
    printf("[AUX] after exit: changes=%d last=%d\n",changes,last);

    /* Final register dump */
    printf("\n[FINAL] GPIO2_EXT=0x%04X  AUX=%d\n", r(g2,GPIO_EXT_OFFSET), (r(g2,GPIO_EXT_OFFSET)>>10)&1);

    munmap((void*)g2,0x1000); munmap((void*)g3,0x1000); munmap((void*)fio,0x1000);
    close(fd);
    return 0;
}
