#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "rt.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#include <sys/time.h>
#include <ifaddrs.h>
#include <net/if.h>
#ifdef __linux__
#include <netpacket/packet.h>
#endif
#ifdef __APPLE__
#include <net/if_dl.h>
#endif
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif
#include "network_address.h"
static struct { int fd,used,events,armed;uint64_t hwnd;uint32_t message; } sockets[256];
static int guest_socket_address(CPU *c,uint64_t address,int length,struct sockaddr_storage *out,socklen_t *native_length) {
    if(!address || length<2) { c->last_error=10014;return 0; }unsigned family=load(c,address,16);size_t need=family==2 ? 16 : family==23 ? 28 : 0;if(!need) { c->last_error=10047;return 0; }if((size_t)length<need) { c->last_error=10014;return 0; }uint8_t *bytes=memory(c,address,need);return bytes && winsock_address_decode(bytes,need,out,native_length);
}
static int socket_index(uint64_t s) { return s>=0x40000000 && s<0x40000100 && sockets[s-0x40000000].used ? (int)(s-0x40000000) : -1; }
static int wsaerror(int e) { switch(e) { case EPERM:case EACCES:return 10013;case EBADF:case ENOTSOCK:return 10038;case EAFNOSUPPORT:return 10047;case EINVAL:return 10022;case EADDRNOTAVAIL:return 10049;case ECONNRESET:return 10054;case ENOTCONN:return 10057;case EHOSTUNREACH:return 10065;case EAGAIN:return 10035;case EINPROGRESS:return 10035;case ECONNREFUSED:return 10061;case ENETUNREACH:return 10051;case EADDRINUSE:return 10048;case ETIMEDOUT:return 10060;default:return e ? 10022 : 0; } }
extern void ui_post(uint64_t h,uint32_t msg,uint64_t wp,uint64_t lp);
void network_pump(void) {
    for(unsigned i=0;i<256;i++) if(sockets[i].used && sockets[i].hwnd && sockets[i].armed) {
        struct pollfd p={sockets[i].fd,0,0};if(sockets[i].armed&(1|8|32)) p.events|=POLLIN;if(sockets[i].armed&(2|16)) p.events|=POLLOUT;
        if(poll(&p,1,0)>0) { int ready=0;if(p.revents&POLLIN) ready|=sockets[i].armed&(1|8);if(p.revents&POLLOUT) ready|=sockets[i].armed&(2|16);if(p.revents&(POLLHUP|POLLERR)) ready|=sockets[i].armed&32;
            for(unsigned bit=1;bit<=32;bit<<=1) if(ready&bit) { int error=0;socklen_t n=sizeof error;if(bit==16) getsockopt(p.fd,SOL_SOCKET,SO_ERROR,&error,&n);ui_post(sockets[i].hwnd,sockets[i].message,0x40000000+i,bit|((uint32_t)wsaerror(error)<<16));sockets[i].armed&=~bit; }
        }
    }
}
void network_free(void) { for(unsigned i=0;i<256;i++) if(sockets[i].used) { close(sockets[i].fd);sockets[i].used=0; } }
#define IS(s) (!strcmp(name,s))
#define A(n) argument(c,n)
#define RET(x) do { *result=(uint64_t)(x);return 1; } while(0)
int network_api(CPU *c,const char *name,uint64_t *result) {
    if(IS("getaddrinfo")) {
        char *node=A(0) ? guest_string(c,A(0)) : NULL,*service=A(1) ? guest_string(c,A(1)) : NULL;
        char *host=node ? strdup(node) : NULL,*port=service ? strdup(service) : NULL;uint64_t output=A(3);struct addrinfo hints={0},*list=NULL;
        if(A(2)) { uint32_t flags=load(c,A(2),32);hints.ai_flags=(flags&1 ? AI_PASSIVE : 0)|(flags&2 ? AI_CANONNAME : 0)|(flags&4 ? AI_NUMERICHOST : 0)|(flags&8 ? AI_NUMERICSERV : 0);hints.ai_family=load(c,A(2)+4,32);if(hints.ai_family==23) hints.ai_family=AF_INET6;hints.ai_socktype=load(c,A(2)+8,32);hints.ai_protocol=load(c,A(2)+12,32); }
        extern unsigned vm_release(CPU *);extern void vm_restore(CPU *,unsigned);
        unsigned depth=vm_release(c);int rc=getaddrinfo(host,port,A(2) ? &hints : NULL,&list);vm_restore(c,depth);free(host);free(port);
        if(rc) { store(c,output,64,0);RET(rc==EAI_AGAIN ? 11002 : rc==EAI_MEMORY ? 8 : rc==EAI_FAMILY ? 10047 : 11001); }
        uint64_t head=0,previous=0;
        for(struct addrinfo *p=list;p;p=p->ai_next) {
            uint8_t encoded[28];size_t address_length=winsock_address_encode(p->ai_addr,encoded);if(!address_length) continue;size_t name_length=p->ai_canonname ? strlen(p->ai_canonname)+1 : 0;uint64_t entry=guest_alloc(c,48+address_length+name_length);if(!entry) { freeaddrinfo(list);store(c,output,64,0);RET(8); }
            store(c,entry,32,p->ai_flags);store(c,entry+4,32,p->ai_family==AF_INET6 ? 23 : p->ai_family);store(c,entry+8,32,p->ai_socktype);store(c,entry+12,32,p->ai_protocol);store(c,entry+16,64,address_length);store(c,entry+32,64,entry+48);
            uint8_t *address=writable_memory(c,entry+48,address_length);if(address) memcpy(address,encoded,address_length);
            if(name_length) { store(c,entry+24,64,entry+48+address_length);uint8_t *dest=writable_memory(c,entry+48+address_length,name_length);if(dest) memcpy(dest,p->ai_canonname,name_length); }
            if(previous) store(c,previous+40,64,entry);else head=entry;previous=entry;
        }freeaddrinfo(list);store(c,output,64,head);RET(0);
    }
    if(IS("freeaddrinfo")) { uint64_t entry=A(0);while(entry && !c->failed) { uint64_t next=load(c,entry+40,64);guest_free(c,entry);entry=next; }RET(0); }
    if(IS("GetAdaptersAddresses")) {
        struct ifaddrs *all=NULL;if(getifaddrs(&all)) RET(1);unsigned count=0;
        for(struct ifaddrs *p=all;p;p=p->ifa_next) if(p->ifa_addr && p->ifa_addr->sa_family==AF_INET) count++;
        size_t needed=(count ? count : 1)*1024;uint64_t capacity=load(c,A(4),32);store(c,A(4),32,needed);
        if(!A(3) || capacity<needed) { freeifaddrs(all);RET(111); }
        uint8_t *buffer=writable_memory(c,A(3),needed);if(!buffer) { freeifaddrs(all);RET(1); }memset(buffer,0,needed);unsigned index=0;
        for(struct ifaddrs *p=all;p;p=p->ifa_next) if(p->ifa_addr && p->ifa_addr->sa_family==AF_INET) {
            uint64_t base=A(3)+index*1024,unicast=base+448,sockaddr=base+512;unsigned ifindex=if_nametoindex(p->ifa_name);
            store(c,base,32,448);store(c,base+4,32,ifindex);store(c,base+8,64,index+1<count ? base+1024 : 0);store(c,base+16,64,base+600);store(c,base+24,64,unicast);
            store(c,base+56,64,base+700);store(c,base+64,64,base+704);store(c,base+72,64,base+704);store(c,base+96,32,1500);store(c,base+100,32,(p->ifa_flags&IFF_LOOPBACK) ? 24 : 6);store(c,base+104,32,(p->ifa_flags&IFF_UP) ? 1 : 2);store(c,base+108,32,ifindex);
            snprintf((char *)buffer+index*1024+600,100,"%s",p->ifa_name);for(unsigned j=0;p->ifa_name[j] && j<40;j++) store(c,base+704+2*j,16,(unsigned char)p->ifa_name[j]);
#ifdef __linux__
            for(struct ifaddrs *m=all;m;m=m->ifa_next) if(m->ifa_addr && m->ifa_addr->sa_family==AF_PACKET && !strcmp(m->ifa_name,p->ifa_name)) { struct sockaddr_ll *link=(struct sockaddr_ll *)m->ifa_addr;unsigned n=link->sll_halen>8 ? 8 : link->sll_halen;memcpy(buffer+index*1024+80,link->sll_addr,n);store(c,base+88,32,n);break; }
#elif defined(__APPLE__)
            for(struct ifaddrs *m=all;m;m=m->ifa_next) if(m->ifa_addr && m->ifa_addr->sa_family==AF_LINK && !strcmp(m->ifa_name,p->ifa_name)) { struct sockaddr_dl *link=(struct sockaddr_dl *)m->ifa_addr;unsigned n=link->sdl_alen>8 ? 8 : link->sdl_alen;memcpy(buffer+index*1024+80,LLADDR(link),n);store(c,base+88,32,n);break; }
#endif
            store(c,unicast,32,64);store(c,unicast+16,64,sockaddr);store(c,unicast+24,32,16);uint8_t *address=writable_memory(c,sockaddr,16);if(address) { uint8_t encoded[28];winsock_address_encode(p->ifa_addr,encoded);memcpy(address,encoded,16); }store(c,unicast+40,32,4);store(c,unicast+44,32,UINT32_MAX);store(c,unicast+48,32,UINT32_MAX);
            uint32_t mask=p->ifa_netmask ? ntohl(((struct sockaddr_in *)p->ifa_netmask)->sin_addr.s_addr) : 0;unsigned prefix=0;while(mask&0x80000000) { prefix++;mask<<=1; }store(c,unicast+56,8,prefix);index++;
        }freeifaddrs(all);RET(count ? 0 : 232);
    }
    if(IS("WSAStartup")) { uint8_t *p=writable_memory(c,A(1),408);if(p) { memset(p,0,408);store(c,A(1),16,A(0));store(c,A(1)+2,16,0x202); }RET(0); }
    if(IS("WSAGetLastError")) RET(c->last_error);
    if(IS("htonl") || IS("ntohl")) RET(htonl((uint32_t)A(0)));
    if(IS("htons") || IS("ntohs")) RET(htons((uint16_t)A(0)));
    if(IS("inet_ntoa")) { extern uint64_t thread_crt_buffer(CPU *,unsigned,size_t);uint64_t address=thread_crt_buffer(c,2,16);char *buffer=(char *)writable_memory(c,address,16);struct in_addr value={.s_addr=(uint32_t)A(0)};if(!buffer || !inet_ntop(AF_INET,&value,buffer,16)) { c->last_error=10022;RET(0); }RET(address); }
    if(IS("inet_addr")) { char *s=guest_string(c,A(0));RET(s ? inet_addr(s) : UINT32_MAX); }
    if(IS("socket")) { int family=(int32_t)A(0);if(family==23) family=AF_INET6;int fd=socket(family,(int)A(1),(int)A(2));if(fd<0) { c->last_error=wsaerror(errno);RET(UINT64_MAX); }
#ifdef SO_NOSIGPIPE
        int no_sigpipe=1;setsockopt(fd,SOL_SOCKET,SO_NOSIGPIPE,&no_sigpipe,sizeof no_sigpipe);
#endif
        for(unsigned i=0;i<256;i++) if(!sockets[i].used) { sockets[i].used=1;sockets[i].fd=fd;RET(0x40000000+i); }close(fd);RET(UINT64_MAX); }
    int i=socket_index(A(0));
    if(IS("closesocket")) { if(i<0) RET(-1);close(sockets[i].fd);memset(&sockets[i],0,sizeof sockets[i]);RET(0); }
    if(IS("WSAAsyncSelect")) { if(i<0) RET(-1);sockets[i].hwnd=A(1);sockets[i].message=A(2);sockets[i].events=A(3);sockets[i].armed=A(3);fcntl(sockets[i].fd,F_SETFL,fcntl(sockets[i].fd,F_GETFL)|O_NONBLOCK);RET(0); }
    if(IS("ioctlsocket")) { if(i<0) RET(-1);if((uint32_t)A(1)==0x8004667e) { int f=fcntl(sockets[i].fd,F_GETFL),on=load(c,A(2),32);RET(fcntl(sockets[i].fd,F_SETFL,on ? f|O_NONBLOCK : f&~O_NONBLOCK)); }c->last_error=10022;RET(-1); }
    if(IS("bind") || IS("connect")) { if(i<0) RET(-1);struct sockaddr_storage address;socklen_t length;if(!guest_socket_address(c,A(1),(int32_t)A(2),&address,&length)) RET(-1);int rc=IS("bind") ? bind(sockets[i].fd,(struct sockaddr *)&address,length) : connect(sockets[i].fd,(struct sockaddr *)&address,length);if(rc<0) c->last_error=wsaerror(errno);RET(rc); }
    if(IS("setsockopt")) { if(i<0) RET(-1);int level=(int)A(1),option=(int)A(2);if(level==0xffff) { level=SOL_SOCKET;switch(option) { case 4:option=SO_REUSEADDR;break;case 8:option=SO_KEEPALIVE;break;case 32:option=SO_BROADCAST;break;case 0x1001:option=SO_SNDBUF;break;case 0x1002:option=SO_RCVBUF;break;default:c->last_error=10042;RET(-1); } }uint8_t *p=memory(c,A(3),A(4));int rc=p ? setsockopt(sockets[i].fd,level,option,p,A(4)) : -1;if(rc<0) c->last_error=wsaerror(errno);RET(rc); }
    if(IS("send") || IS("sendto") || IS("recv") || IS("recvfrom")) {
        if(i<0) RET(-1);int length=(int32_t)A(2);if(length<0) { c->last_error=10022;RET(-1); }int receiving=IS("recv") || IS("recvfrom");uint8_t *p=receiving ? writable_memory(c,A(1),length) : memory(c,A(1),length);if(!p) RET(-1);ssize_t rc;
        if(IS("sendto")) { struct sockaddr_storage address;socklen_t n=0;struct sockaddr *destination=NULL;if(A(4)) { if(!guest_socket_address(c,A(4),(int32_t)A(5),&address,&n)) RET(-1);destination=(struct sockaddr *)&address; }rc=sendto(sockets[i].fd,p,length,(int)A(3)|MSG_NOSIGNAL,destination,n); }
        else if(IS("recvfrom")) { struct sockaddr_storage address;socklen_t n=sizeof address;uint32_t capacity=A(5) ? load(c,A(5),32) : 0;if(A(4) && (!A(5) || capacity<2)) { c->last_error=10014;RET(-1); }rc=recvfrom(sockets[i].fd,p,length,(int)A(3),A(4) ? (struct sockaddr *)&address : NULL,A(4) ? &n : NULL);if(rc>=0 && A(4)) { uint8_t encoded[28];size_t needed=winsock_address_encode((struct sockaddr *)&address,encoded);store(c,A(5),32,needed);if(needed>capacity) { c->last_error=10014;RET(-1); }uint8_t *out=writable_memory(c,A(4),needed);if(out) memcpy(out,encoded,needed); } }
        else rc=receiving ? recv(sockets[i].fd,p,length,(int)A(3)) : send(sockets[i].fd,p,length,(int)A(3)|MSG_NOSIGNAL);
        if(receiving) sockets[i].armed|=sockets[i].events&1;if(rc<0) { c->last_error=wsaerror(errno);if(!receiving && errno==EAGAIN) sockets[i].armed|=sockets[i].events&2; }RET(rc);
    }
    return 0;
}
