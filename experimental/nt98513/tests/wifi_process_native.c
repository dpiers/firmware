/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
#include "wifi.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/random.h>
static pid_t fake_waitpid(pid_t p, int *s, int options);
static ssize_t fake_getrandom(void *p, size_t n, unsigned flags);
static int fake_execl(const char *path,const char *arg,...);
#define waitpid fake_waitpid
#define getrandom fake_getrandom
#define execl fake_execl
#include "../package/nt98513-wifi-control/src/supervisor.c"
#undef execl
#undef waitpid
#undef getrandom
static pid_t fake_waitpid(pid_t p, int *s, int options)
{
 static int injected;
 if(!injected && getenv("TEST_WAIT_EINTR")){injected=1;errno=EINTR;return -1;}
 return waitpid(p,s,options);
}
static ssize_t fake_getrandom(void *p,size_t n,unsigned flags)
{
 if(getenv("TEST_ENTROPY_AGAIN")){errno=EAGAIN;return -1;}
 if(getenv("TEST_ENTROPY_FAIL")){errno=ENOSYS;return -1;}
 return getrandom(p,n,flags);
}
static int fake_execl(const char *path,const char *arg,...)
{
 if(access("/root/wifi-fixture/exec-fail",F_OK)==0){errno=ENOENT;return -1;}
 char *args[32];unsigned n=0;va_list ap;va_start(ap,arg);
 args[n++]="python3";
 args[n++]=strstr(path,"busybox") ? "/test/wifi_fake_dhcp.py":"/test/wifi_fake_wpa.py";
 const char *item=arg;
 do{assert(n<31);args[n++]=(char *)item;item=va_arg(ap,const char *);}while(item);
 va_end(ap);args[n]=NULL;execv("/usr/bin/python3",args);return -1;
}
static void trace(const char *event)
{
 FILE *f=fopen("/root/wifi-fixture/trace","a");assert(f);fprintf(f,"%s\n",event);fclose(f);
}
int wifi_net_admit(struct wifi_net *n,const char *interface)
{
 memset(n,0,sizeof(*n));n->resolver_dir=-1;n->ifindex=if_nametoindex(interface);
 strcpy(n->interface,interface);trace("admit");return 0;
}
int wifi_net_apply(struct wifi_net *n,const struct wifi_lease *l)
{(void)n;(void)l;if(access("/root/wifi-fixture/apply-delay",F_OK)==0)usleep(1400000);trace("apply");return 0;}
int wifi_net_clear(struct wifi_net *n)
{(void)n;trace("clear");return access("/root/wifi-fixture/clear-fail",F_OK)==0 ? -1:0;}
int wifi_net_close(struct wifi_net *n){(void)n;trace("close");return 0;}
int main(int argc,char **argv)
{
 if(argc==2&&!strcmp(argv[1],"entropy")){
  unsigned char bytes[16];return wifi_entropy(bytes,sizeof(bytes),30) ? 1:0;
 }
 if(argc==3&&!strcmp(argv[1],"config")){
  struct wifi_config cfg;return wifi_config_read(argv[2],&cfg) ? 1:0;
 }
 if(argc==2&&!strcmp(argv[1],"wait-eintr")){
  pid_t p=fork();assert(p>=0);if(!p){for(;;)pause();}
  pid_t saved=p;assert(reap(&p,false)==0);assert(p==saved);
  assert(reap(&p,true)==1);assert(!p);return 0;
 }
 if(argc==2&&!strcmp(argv[1],"run")){
  struct wifi_config c={.interface="lo",.country="US",.ssid={0x74},.ssid_len=1};
  return wifi_run(&c);
 }
 if(argc==2&&!strcmp(argv[1],"status"))return wifi_client("lo","STATUS");
 if(argc==2&&!strcmp(argv[1],"stop"))return wifi_client("lo","STOP");
 if(argc==3&&!strcmp(argv[1],"lease"))return wifi_lease_entry(argv[2]);
 return 2;
}
