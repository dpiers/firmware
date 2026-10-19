/* SPDX-License-Identifier: MIT
 * Tests production network/resolver code. All socket and ioctl entry points
 * are link-wrapped: no request can reach a host network stack. The fake kernel
 * stores wire messages, not a replacement implementation of ownership policy.
 */
#define _GNU_SOURCE
#include "wifi.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <net/if.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL %s:%d: %s\n",__func__,__LINE__,#x); exit(1); } } while (0)
#define MAX_MSG 24
struct wire { unsigned char data[512]; size_t size; };
static struct wire objects[MAX_MSG], pending;
static unsigned object_count, changes, deletions, nextfd=20000, current_index=7;
static int failure_type, failure_count, wrong_sequence, io_failure, dump_interrupted, dump_truncated, flag_failure, kernel_metadata;
static const char baseline[]="# Ethernet owner\nnameserver 10.0.0.1\nnameserver 10.0.0.2\nnameserver 10.0.0.3\nnameserver 10.0.0.4\nsearch owner.example\n";
static char fixture[128], resolver_path[256];
static struct wifi_net owner;
static struct wifi_lease lease;

static struct nlmsghdr *header(struct wire *w) { return (void *)w->data; }
static uint32_t *attribute(struct wire *w, unsigned type, int route)
{
 struct nlmsghdr *h=header(w); int len=route ? RTM_PAYLOAD(h) : IFA_PAYLOAD(h);
 struct rtattr *a=route ? RTM_RTA((struct rtmsg *)NLMSG_DATA(h)) : IFA_RTA((struct ifaddrmsg *)NLMSG_DATA(h));
 for (; RTA_OK(a,len); a=RTA_NEXT(a,len)) if (a->rta_type==type && RTA_PAYLOAD(a)==4) return RTA_DATA(a);
 return NULL;
}
static int is_route(struct wire *w) { return header(w)->nlmsg_type==RTM_NEWROUTE; }
static int same_object(struct wire *a,struct wire *b)
{
 return a->size==b->size && !memcmp(a->data+NLMSG_HDRLEN,b->data+NLMSG_HDRLEN,a->size-NLMSG_HDRLEN);
}
/* Kernel dumps add metadata absent from the original mutation request.
 * Only the normalized-dump fixture enables this fake kernel behavior. */
static int deletion_matches(struct wire *stored,struct wire *request)
{
 if(!kernel_metadata) return same_object(stored,request);
 struct wire normalized=*stored;
 if(normalized.size<request->size) return 0;
 normalized.size=request->size;
 if(is_route(&normalized)) ((struct rtmsg *)NLMSG_DATA(header(&normalized)))->rtm_flags=0;
 else ((struct ifaddrmsg *)NLMSG_DATA(header(&normalized)))->ifa_flags=0;
 return same_object(&normalized,request);
}
static void append_metadata(struct wire *w,unsigned type,const void *value,size_t size)
{
 struct nlmsghdr *h=header(w);size_t offset=NLMSG_ALIGN(h->nlmsg_len);CHECK(offset+RTA_SPACE(size)<=sizeof(w->data));
 struct rtattr *a=(void *)(w->data+offset);a->rta_type=(unsigned short)type;a->rta_len=(unsigned short)RTA_LENGTH(size);
 memcpy(RTA_DATA(a),value,size);h->nlmsg_len=(unsigned)(offset+RTA_SPACE(size));w->size=h->nlmsg_len;
}
int __wrap_socket(int domain,int type,int protocol)
{
 CHECK((domain==AF_NETLINK && (type&SOCK_RAW) && protocol==NETLINK_ROUTE) || (domain==AF_INET && (type&SOCK_DGRAM)));
 if(io_failure) { errno=EMFILE; return -1; } return (int)nextfd++;
}
int __wrap_bind(int fd,const struct sockaddr *a,socklen_t n) { CHECK(fd>=20000); CHECK(a->sa_family==AF_NETLINK); (void)n; return 0; }
int __wrap_setsockopt(int fd,int level,int option,const void *v,socklen_t n) { CHECK(fd>=20000); (void)level;(void)option;(void)v;(void)n;return 0; }
int __real_close(int);
int __wrap_close(int fd) { return fd>=20000 ? 0 : __real_close(fd); }
unsigned __wrap_if_nametoindex(const char *name) { CHECK(!strcmp(name,"wlan0")); return current_index; }
int __wrap_ioctl(int fd,unsigned long op,...)
{
 CHECK(fd>=20000); CHECK(op==SIOCGIFFLAGS || op==SIOCSIFFLAGS);
 va_list ap; va_start(ap,op);struct ifreq *r=va_arg(ap,struct ifreq *);va_end(ap);
 if(op==SIOCSIFFLAGS && flag_failure) { errno=EPERM;return -1; }
 if(op==SIOCGIFFLAGS) r->ifr_flags=IFF_UP;
 return 0;
}
ssize_t __wrap_sendto(int fd,const void *data,size_t size,int flags,const struct sockaddr *dest,socklen_t len)
{
 CHECK(fd>=20000); CHECK(dest->sa_family==AF_NETLINK); CHECK(size<=sizeof(pending.data));
 (void)flags;(void)len;memcpy(pending.data,data,size);pending.size=size;
 return (ssize_t)size;
}
ssize_t __wrap_recv(int fd,void *data,size_t size,int flags)
{
 CHECK(fd>=20000);(void)flags; struct nlmsghdr *q=header(&pending);size_t used=0;
 if(q->nlmsg_type==RTM_GETADDR || q->nlmsg_type==RTM_GETROUTE) {
  for(unsigned i=0;i<object_count;i++) if((q->nlmsg_type==RTM_GETROUTE)==is_route(&objects[i])) {
   CHECK(used+objects[i].size+NLMSG_SPACE(0)<=size);memcpy((char *)data+used,objects[i].data,objects[i].size);
   struct nlmsghdr *h=(void *)((char *)data+used); h->nlmsg_seq=q->nlmsg_seq; used+=NLMSG_ALIGN(h->nlmsg_len);
  }
  struct nlmsghdr done={.nlmsg_len=NLMSG_LENGTH(0),.nlmsg_type=NLMSG_DONE,.nlmsg_seq=q->nlmsg_seq};
  if(dump_interrupted) done.nlmsg_flags=NLM_F_DUMP_INTR;
  memcpy((char *)data+used,&done,sizeof(done));return dump_truncated ? (ssize_t)size+1 : (ssize_t)(used+sizeof(done));
 }
 CHECK(q->nlmsg_flags & NLM_F_ACK); int error=0;
 if(failure_count && q->nlmsg_type==failure_type) { error=-EACCES;failure_count--; }
 if(wrong_sequence) { wrong_sequence=0;error=-EPERM; }
 else if(!error) {
  changes++;
  if(q->nlmsg_type==RTM_NEWADDR || q->nlmsg_type==RTM_NEWROUTE) {
   CHECK(q->nlmsg_flags & NLM_F_EXCL);CHECK(object_count<MAX_MSG);objects[object_count++]=pending;
  } else {
   CHECK(q->nlmsg_type==RTM_DELADDR || q->nlmsg_type==RTM_DELROUTE);deletions++;
   for(unsigned i=0;i<object_count;i++) if(deletion_matches(&objects[i],&pending)) { objects[i]=objects[--object_count];break; }
  }
 }
 struct { struct nlmsghdr h; struct nlmsgerr e; } reply={0};
 reply.h.nlmsg_len=NLMSG_LENGTH(sizeof(reply.e));reply.h.nlmsg_type=NLMSG_ERROR;
 reply.h.nlmsg_seq=error==-EPERM ? q->nlmsg_seq+1 : q->nlmsg_seq;reply.e.error=error;
 CHECK(size>=sizeof(reply));memcpy(data,&reply,sizeof(reply));return sizeof(reply);
}
static void write_file(const char *path,const char *data,size_t n,mode_t mode)
{
 int fd=open(path,O_WRONLY|O_CREAT|O_TRUNC,mode);CHECK(fd>=0);CHECK(write(fd,data,n)==(ssize_t)n);CHECK(!close(fd));CHECK(!chmod(path,mode));
}
static char *read_file(void)
{
 static char data[70000];int fd=open(resolver_path,O_RDONLY);CHECK(fd>=0);ssize_t n=read(fd,data,sizeof(data)-1);CHECK(n>=0);data[n]=0;CHECK(!close(fd));return data;
}
static void setup(int open_resolver)
{
 CHECK(geteuid()==0);strcpy(fixture,"/root/wifi-net-native-XXXXXX");CHECK(mkdtemp(fixture));
 CHECK(!chmod(fixture,0700));snprintf(resolver_path,sizeof(resolver_path),"%s/resolv.conf",fixture);
 memset(&owner,0,sizeof(owner));owner.resolver_dir=-1;owner.ifindex=7;strcpy(owner.interface,"wlan0");
 CHECK(!wifi_lease_parse("192.168.4.20","255.255.255.0","192.168.4.1","1.1.1.1 9.9.9.9","3600",&lease));
 write_file(resolver_path,baseline,sizeof(baseline)-1,0644);
 if(open_resolver) CHECK(!wifi_resolver_open(&owner,resolver_path));
}
static void exact_baseline(void) { CHECK(!strcmp(read_file(),baseline)); }
static void check_owned_dns(void)
{
 char *data=read_file();char *first=strstr(data,"nameserver ");CHECK(first);CHECK(!strncmp(first,"nameserver 1.1.1.1\n",19));
 CHECK(strstr(data,baseline));CHECK(strlen(data)>=sizeof(baseline));CHECK(!memcmp(data+strlen(data)-(sizeof(baseline)-1),baseline,sizeof(baseline)-1));
}
static void happy(void)
{
 setup(1);CHECK(!wifi_net_apply(&owner,&lease));CHECK(object_count==2);CHECK(owner.address && owner.route && owner.dns_owned);check_owned_dns();
 CHECK(!wifi_net_clear(&owner));CHECK(!object_count);CHECK(!owner.address && !owner.route && !owner.dns_owned);exact_baseline();
 unsigned saved=changes;CHECK(!wifi_net_clear(&owner));CHECK(saved==changes);
}
static void renew(void)
{
 setup(1);CHECK(!wifi_net_apply(&owner,&lease));lease.address=inet_addr("192.168.4.21");lease.dns[0]=inet_addr("8.8.8.8");
 CHECK(!wifi_net_apply(&owner,&lease));CHECK(object_count==2);CHECK(*attribute(&objects[0],IFA_LOCAL,0)==lease.address);
 CHECK(strstr(read_file(),"nameserver 8.8.8.8\n"));CHECK(!strstr(read_file(),"nameserver 1.1.1.1\n"));CHECK(!wifi_net_clear(&owner));exact_baseline();
}
static void duplicate(void)
{
 setup(1);CHECK(!wifi_net_apply(&owner,&lease));unsigned before=changes;struct stat a,b;CHECK(!stat(resolver_path,&a));
 CHECK(!wifi_net_apply(&owner,&lease));CHECK(changes==before);CHECK(!stat(resolver_path,&b));CHECK(a.st_ino==b.st_ino);
}
static void rollback(int type)
{
 setup(1);failure_type=type;failure_count=1;CHECK(wifi_net_apply(&owner,&lease));CHECK(!object_count);CHECK(!owner.address && !owner.route && !owner.dns_owned);exact_baseline();
}
static void dns_rollback(void)
{
 setup(1);write_file(resolver_path,"foreign\n",8,0644);CHECK(wifi_net_apply(&owner,&lease));CHECK(!object_count);CHECK(!strcmp(read_file(),"foreign\n"));
}
static void cleanup_ack(void)
{
 setup(1);CHECK(!wifi_net_apply(&owner,&lease));failure_type=RTM_DELROUTE;failure_count=1;CHECK(wifi_net_clear(&owner));CHECK(owner.route);CHECK(!owner.address);CHECK(object_count==1);
 CHECK(!wifi_net_clear(&owner));CHECK(!object_count);exact_baseline();
}
static void foreign(void)
{
 setup(1);CHECK(!wifi_net_apply(&owner,&lease));objects[2]=objects[0];((struct ifaddrmsg *)NLMSG_DATA(header(&objects[2])))->ifa_index=88;
 objects[3]=objects[1];*attribute(&objects[3],RTA_OIF,1)=88;object_count=4;
 struct wire a=objects[2],r=objects[3];CHECK(!wifi_net_clear(&owner));CHECK(object_count==2);CHECK(deletions==2);
 CHECK((same_object(&objects[0],&a) && same_object(&objects[1],&r)) || (same_object(&objects[1],&a) && same_object(&objects[0],&r)));exact_baseline();
}
static void normalized_dump(void)
{
 setup(1);CHECK(!wifi_net_apply(&owner,&lease));kernel_metadata=1;
 struct ifaddrmsg *a=NLMSG_DATA(header(&objects[0]));a->ifa_flags=IFA_F_PERMANENT;
 uint32_t address_flags=IFA_F_PERMANENT,table=RT_TABLE_MAIN;
 struct ifa_cacheinfo cache={.ifa_prefered=UINT32_MAX,.ifa_valid=UINT32_MAX,.cstamp=17,.tstamp=18};
 append_metadata(&objects[0],IFA_FLAGS,&address_flags,sizeof(address_flags));
 append_metadata(&objects[0],IFA_CACHEINFO,&cache,sizeof(cache));
 append_metadata(&objects[0],IFA_LABEL,"wlan0",6);
 ((struct rtmsg *)NLMSG_DATA(header(&objects[1])))->rtm_flags=RTNH_F_LINKDOWN|RTNH_F_DEAD;
 append_metadata(&objects[1],RTA_TABLE,&table,sizeof(table));
 CHECK(!wifi_net_clear(&owner));CHECK(!object_count);CHECK(deletions==2);exact_baseline();
}
static void foreign_same_interface(void)
{
 setup(1);CHECK(!wifi_net_apply(&owner,&lease));objects[2]=objects[0];*attribute(&objects[2],IFA_LOCAL,0)=inet_addr("192.168.4.44");
 *attribute(&objects[2],IFA_ADDRESS,0)=inet_addr("192.168.4.44");objects[3]=objects[1];*attribute(&objects[3],RTA_PRIORITY,1)=20;object_count=4;
 struct wire a=objects[2],r=objects[3];CHECK(!wifi_net_clear(&owner));CHECK(object_count==2);CHECK(deletions==2);
 CHECK((same_object(&objects[0],&a) && same_object(&objects[1],&r)) || (same_object(&objects[1],&a) && same_object(&objects[0],&r)));exact_baseline();
}
static void binary_baseline(void)
{
 static const char binary[]="# retained bytes\n\0nameserver 10.9.8.7\n";
 setup(0);write_file(resolver_path,binary,sizeof(binary)-1,0600);CHECK(!wifi_resolver_open(&owner,resolver_path));
 CHECK(!wifi_resolver_set(&owner,&lease));CHECK(!wifi_resolver_clear(&owner));
 char actual[sizeof(binary)];int fd=open(resolver_path,O_RDONLY);CHECK(fd>=0);CHECK(read(fd,actual,sizeof(actual))==(ssize_t)sizeof(binary)-1);CHECK(!memcmp(actual,binary,sizeof(binary)-1));CHECK(!close(fd));
}
static void replacement(const char *field)
{
 setup(1);CHECK(!wifi_net_apply(&owner,&lease));struct ifaddrmsg *a=NLMSG_DATA(header(&objects[0]));struct rtmsg *r=NLMSG_DATA(header(&objects[1]));
 int route_case=!strncmp(field,"route_",6);unsigned idx=route_case?1:0;
 if(!strcmp(field,"route_metric")) *attribute(&objects[1],RTA_PRIORITY,1)=11;
 else if(!strcmp(field,"route_gateway")) *attribute(&objects[1],RTA_GATEWAY,1)=inet_addr("192.168.4.2");
 else if(!strcmp(field,"route_protocol")) r->rtm_protocol=RTPROT_BOOT;
 else if(!strcmp(field,"route_type")) r->rtm_type=RTN_BLACKHOLE;
 else if(!strcmp(field,"route_scope")) r->rtm_scope=RT_SCOPE_LINK;
 else if(!strcmp(field,"route_family")) r->rtm_family=AF_INET6;
 else if(!strcmp(field,"addr_local")) *attribute(&objects[0],IFA_LOCAL,0)=inet_addr("192.168.4.22");
 else if(!strcmp(field,"addr_prefix")) a->ifa_prefixlen=25;
 else if(!strcmp(field,"addr_scope")) a->ifa_scope=RT_SCOPE_LINK;
 else if(!strcmp(field,"addr_family")) a->ifa_family=AF_INET6;
 else if(!strcmp(field,"addr_peer")) *attribute(&objects[0],IFA_ADDRESS,0)=inet_addr("192.168.4.23");
 else CHECK(0);
 struct wire saved=objects[idx];int rc=wifi_net_clear(&owner);CHECK(deletions==1);CHECK(object_count==1);CHECK(same_object(&objects[0],&saved));CHECK(rc!=0);exact_baseline();
}
static void resolver_refuse(const char *kind)
{
 setup(0);char other[256];snprintf(other,sizeof(other),"%s/other",fixture);
 if(!strcmp(kind,"symlink")) { CHECK(!rename(resolver_path,other));CHECK(!symlink(other,resolver_path)); }
 else if(!strcmp(kind,"parent_symlink")) { CHECK(!symlink(fixture,other));snprintf(resolver_path,sizeof(resolver_path),"%s/other/resolv.conf",fixture); }
 else if(!strcmp(kind,"mode")) CHECK(!chmod(resolver_path,0666));
 else if(!strcmp(kind,"parent")) CHECK(!chmod(fixture,0777));
 else if(!strcmp(kind,"hardlink")) CHECK(!link(resolver_path,other));
 else if(!strcmp(kind,"fifo")) { CHECK(!rename(resolver_path,other));CHECK(!mkfifo(resolver_path,0600)); }
 else if(!strcmp(kind,"oversize")) { int fd=open(resolver_path,O_WRONLY);CHECK(fd>=0);CHECK(!ftruncate(fd,WIFI_RESOLVER_MAX+1));CHECK(!close(fd)); }
 CHECK(wifi_resolver_open(&owner,resolver_path));
}
static void conflict(const char *kind)
{
 setup(1);CHECK(!wifi_net_apply(&owner,&lease));char other[256];snprintf(other,sizeof(other),"%s/external",fixture);
 char *saved=strdup(read_file());CHECK(saved);
 if(!strcmp(kind,"content")) write_file(resolver_path,"foreign\n",8,0644);
 else if(!strcmp(kind,"inode")) { write_file(other,saved,strlen(saved),0644);CHECK(!rename(other,resolver_path)); }
 else { write_file(other,"foreign\n",8,0644);CHECK(!unlink(resolver_path));CHECK(!symlink(other,resolver_path)); }
 struct stat before,after;CHECK(!lstat(resolver_path,&before));char *expected=strdup(read_file());CHECK(expected);
 CHECK(wifi_net_clear(&owner));CHECK(!object_count);CHECK(owner.dns_owned);CHECK(!lstat(resolver_path,&after));CHECK(before.st_ino==after.st_ino);CHECK(!strcmp(read_file(),expected));free(saved);free(expected);
}
static void absent(void)
{
 setup(0);CHECK(!unlink(resolver_path));CHECK(!wifi_resolver_open(&owner,resolver_path));CHECK(!wifi_resolver_set(&owner,&lease));CHECK(!wifi_resolver_clear(&owner));CHECK(!strcmp(read_file(),""));
}
static void ifindex_changed(void)
{
 setup(1);CHECK(!wifi_net_apply(&owner,&lease));current_index=8;unsigned before=changes;CHECK(wifi_net_apply(&owner,&lease));CHECK(!wifi_net_clear(&owner));CHECK(changes==before);CHECK(object_count==2);exact_baseline();
}
static void close_failure(int flag)
{
 setup(1);CHECK(!wifi_net_apply(&owner,&lease));
 if(flag) { owner.raised=true;flag_failure=1; }
 else { failure_type=RTM_DELROUTE;failure_count=1; }
 CHECK(wifi_net_close(&owner));CHECK(owner.baseline==NULL && owner.expected==NULL);
 CHECK(object_count==(flag ? 0u : 1u));exact_baseline();
}
static void truncated_dump(void)
{
 setup(1);CHECK(!wifi_net_apply(&owner,&lease));dump_truncated=1;unsigned before=changes;
 CHECK(wifi_net_clear(&owner));CHECK(changes==before);CHECK(owner.address && owner.route);CHECK(object_count==2);
}
static void interrupted_dump(void)
{
 setup(1);CHECK(!wifi_net_apply(&owner,&lease));dump_interrupted=1;unsigned before=changes;
 CHECK(wifi_net_clear(&owner));CHECK(changes==before);CHECK(owner.address && owner.route);CHECK(object_count==2);
}
static void renew_error(void)
{
 setup(1);CHECK(!wifi_net_apply(&owner,&lease));lease.address=inet_addr("192.168.4.21");failure_type=RTM_NEWROUTE;failure_count=1;
 CHECK(wifi_net_apply(&owner,&lease));CHECK(!object_count);CHECK(!owner.address && !owner.route && !owner.dns_owned);exact_baseline();
}
static void no_router(void)
{
 setup(1);lease.router=0;CHECK(!wifi_net_apply(&owner,&lease));CHECK(object_count==1);CHECK(owner.address && !owner.route);check_owned_dns();
 CHECK(!wifi_net_clear(&owner));CHECK(!object_count);exact_baseline();
}
static void tmp_collision(void)
{
 setup(1);char path[256];snprintf(path,sizeof(path),"%s/.nt98513-wifi-resolv.tmp",fixture);write_file(path,"foreign",7,0600);
 CHECK(wifi_net_apply(&owner,&lease));CHECK(!object_count);exact_baseline();int fd=open(path,O_RDONLY);CHECK(fd>=0);char data[8]={0};CHECK(read(fd,data,7)==7);CHECK(!strcmp(data,"foreign"));CHECK(!close(fd));
}
static void sequence(void) { setup(1);wrong_sequence=1;CHECK(!wifi_net_apply(&owner,&lease));CHECK(object_count==2);CHECK(!wifi_net_clear(&owner)); }
static void no_socket(void) { setup(1);io_failure=1;CHECK(wifi_net_apply(&owner,&lease));CHECK(!changes);exact_baseline(); }
static void invalid_lease(void)
{
 struct { const char *ip,*mask,*router,*dns,*seconds; } invalid[]={
 {"192.168.4.0","255.255.255.0","192.168.4.1","1.1.1.1","60"},
 {"192.168.4.255","255.255.255.0","192.168.4.1","1.1.1.1","60"},
 {"192.168.4.20","255.0.255.0","192.168.4.1","1.1.1.1","60"},
 {"192.168.4.20","255.255.255.0","192.168.5.1","1.1.1.1","60"},
 {"192.168.4.20","255.255.255.0","192.168.4.1","224.1.1.1","60"},
 {"192.168.4.20","255.255.255.0","192.168.4.1","1.1.1.1 2.2.2.2 3.3.3.3 4.4.4.4","60"},
 {"192.168.4.20","255.255.255.0","192.168.4.1","1.1.1.1\nsearch injected","60"},
 {"192.168.4.20","255.255.255.0","192.168.4.1","1.1.1.1","0"},
 {"192.168.4.20","255.255.255.0","192.168.4.1","1.1.1.1","604801"},
 {"192.168.4.20","255.255.255.0","192.168.4.1","1.1.1.1","18446744073709551616"},
 {"192.168.4.20","255.255.255.0","192.168.4.1","1.1.1.1","-1"},
 {"192.168.4.20","255.255.255.0","192.168.4.1","1.1.1.1","+60"}};
 for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++) CHECK(wifi_lease_parse(invalid[i].ip,invalid[i].mask,invalid[i].router,invalid[i].dns,invalid[i].seconds,&lease));
 CHECK(!changes);
}
int main(int argc,char **argv)
{
 CHECK(argc==2);const char *t=argv[1];
 if(!strcmp(t,"happy")) happy();else if(!strcmp(t,"renew")) renew();else if(!strcmp(t,"duplicate")) duplicate();
 else if(!strcmp(t,"address_ack")) rollback(RTM_NEWADDR);else if(!strcmp(t,"route_ack")) rollback(RTM_NEWROUTE);
 else if(!strcmp(t,"dns_rollback")) dns_rollback();else if(!strcmp(t,"cleanup_ack")) cleanup_ack();else if(!strcmp(t,"foreign")) foreign();else if(!strcmp(t,"normalized_dump")) normalized_dump();else if(!strcmp(t,"foreign_same_interface")) foreign_same_interface();else if(!strcmp(t,"binary_baseline")) binary_baseline();
 else if(!strncmp(t,"route_",6) || !strncmp(t,"addr_",5)) replacement(t);
 else if(!strncmp(t,"refuse_",7)) resolver_refuse(t+7);else if(!strncmp(t,"conflict_",9)) conflict(t+9);
 else if(!strcmp(t,"absent")) absent();else if(!strcmp(t,"ifindex_changed")) ifindex_changed();else if(!strcmp(t,"sequence")) sequence();
 else if(!strcmp(t,"close_clear_failure")) close_failure(0);else if(!strcmp(t,"close_flag_failure")) close_failure(1);else if(!strcmp(t,"truncated_dump")) truncated_dump();
 else if(!strcmp(t,"interrupted_dump")) interrupted_dump();else if(!strcmp(t,"renew_error")) renew_error();else if(!strcmp(t,"no_router")) no_router();else if(!strcmp(t,"tmp_collision")) tmp_collision();
 else if(!strcmp(t,"no_socket")) no_socket();else if(!strcmp(t,"invalid_lease")) invalid_lease();else CHECK(0);
 printf("PASS %s (wire changes=%u deletes=%u)\n",t,changes,deletions);return 0;
}
