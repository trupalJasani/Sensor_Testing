/* Simulated Wio-E5 (gateway side) for a PC run of main.c */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bsp_gw.h"
static uint32_t now_ms = 0; static char q[4096]; static size_t ql = 0;
static char cmd[256]; static size_t cl = 0; int sim_dead = 0;
typedef struct { uint32_t t; const char *info; const char *data; } Pkt;
static Pkt pk[] = {
 { 40000, "+TEST: LEN:16, RSSI:-47, SNR:10", "0000850991170000000000000080FF80" },
 {104300, "+TEST: LEN:16, RSSI:-49, SNR:9",  "0000780905180000000001000080FF80" },
 /* seq 2 lost */
 {233000, "+TEST: LEN:16, RSSI:-52, SNR:8",  "00007809051800000000030000800F80" },
 {297000, "+TEST: LEN:16, RSSI:-50, SNR:9",  "0000780905180000000000000080FF80" }, /* node restarted */
 {360000, NULL,                              "00007809" }, /* garbage short packet */
 {420000, "+TEST: LEN:18, RSSI:-48, SNR:9",  "0000780905180000000001000080FF80800E" }, /* 18-byte, battery 3.712 V */
};
static size_t next = 0;
static void push(const char *s){ size_t l=strlen(s); if(ql+l<sizeof q){memcpy(q+ql,s,l); ql+=l;} }
static void tick(void){ while(next < sizeof pk/sizeof pk[0] && now_ms >= pk[next].t){ if(pk[next].info){push(pk[next].info);push("\r\n");} push("+TEST: RX \""); push(pk[next].data); push("\"\r\n"); next++; } }
void BSP_Delay(uint32_t ms){ now_ms += ms; tick(); if(now_ms > 1000000) exit(0); }
uint32_t BSP_GetTick(void){ return now_ms; }
int32_t BSP_UartInit(void){ return 0; }
void BSP_UartFlush(void){ ql = 0; }
int32_t BSP_UartWrite(const char *t){
  if (sim_dead) return 0;
  for(const char *p=t;*p;p++){ unsigned char c=(unsigned char)*p; if(c==0xFF) continue;
    if(c=='\n'){ cmd[cl]=0; if(cl&&cmd[cl-1]=='\r') cmd[--cl]=0;
      if(!strcmp(cmd,"AT")) push("+AT: OK\r\n");
      else if(!strcmp(cmd,"AT+VER")) push("+VER: 4.0.11\r\n");
      else if(!strcmp(cmd,"AT+MODE=TEST")) push("+MODE: TEST\r\n");
      else if(!strncmp(cmd,"AT+TEST=RFCFG",13)) push("+TEST: RFCFG F:868100000, SF10, BW125K, TXPR:12, RXPR:15, POW:14dBm, CRC:ON, IQ:OFF, NET:OFF\r\n");
      else if(!strcmp(cmd,"AT+TEST=RXLRPKT")) push("+TEST: RXLRPKT\r\n");
      else push("+AT: ERROR(-1)\r\n");
      cl=0; } else if(cl<sizeof cmd-1) cmd[cl++]=(char)c; }
  return 0; }
int32_t BSP_UartRead(uint8_t *b, uint16_t m, uint32_t to){ if(ql==0){ BSP_Delay(to); if(ql==0) return 0; }
  size_t n = ql < m ? ql : m; memcpy(b,q,n); memmove(q,q+n,ql-n); ql-=n; return (int32_t)n; }
int main(void){ extern void app_main(void); app_main(); return 0; }
