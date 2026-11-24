#include "network_address.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(value) do { if(!(value)) { fprintf(stderr,"sockaddr layout check failed at %d\n",__LINE__);return 1; } } while(0)
int main(void) {
    uint8_t ipv4[16]={2,0,0x20,0xfb,192,0,2,1},encoded[28];struct sockaddr_storage native;socklen_t size=0;
    CHECK(winsock_address_decode(ipv4,sizeof ipv4,&native,&size));struct sockaddr_in *v4=(struct sockaddr_in *)&native;CHECK(v4->sin_family==AF_INET && size==sizeof *v4);CHECK(!memcmp(&v4->sin_addr,ipv4+4,4));CHECK(winsock_address_encode((struct sockaddr *)v4,encoded)==16 && !memcmp(ipv4,encoded,16));
#ifdef __APPLE__
    CHECK(v4->sin_len==sizeof *v4);
#endif
    uint8_t ipv6[28]={23,0,0x20,0xfb,0,0,0,0,0xfe,0x80};ipv6[23]=1;ipv6[24]=7;
    CHECK(winsock_address_decode(ipv6,sizeof ipv6,&native,&size));struct sockaddr_in6 *v6=(struct sockaddr_in6 *)&native;CHECK(v6->sin6_family==AF_INET6 && size==sizeof *v6 && v6->sin6_scope_id==7);CHECK(winsock_address_encode((struct sockaddr *)v6,encoded)==28 && !memcmp(ipv6,encoded,28));
#ifdef __APPLE__
    CHECK(v6->sin6_len==sizeof *v6);
#endif
    CHECK(!winsock_address_decode(ipv6,16,&native,&size));ipv4[0]=255;CHECK(!winsock_address_decode(ipv4,sizeof ipv4,&native,&size));puts("Windows/Darwin/Linux sockaddr layouts passed");return 0;
}
