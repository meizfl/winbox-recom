#ifndef WINBOX_NETWORK_ADDRESS_H
#define WINBOX_NETWORK_ADDRESS_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>

/* Windows has a 16-bit family field; Darwin has sin_len + an 8-bit family.
 * Marshal fields explicitly, including Windows AF_INET6=23. */
static inline int winsock_address_decode(const uint8_t *bytes,size_t size,struct sockaddr_storage *out,socklen_t *length) {
    if(size<2) return 0;unsigned family=bytes[0]|((unsigned)bytes[1]<<8);memset(out,0,sizeof *out);
    if(family==2 && size>=16) { struct sockaddr_in *a=(struct sockaddr_in *)out;a->sin_family=AF_INET;
#ifdef __APPLE__
        a->sin_len=sizeof *a;
#endif
        memcpy(&a->sin_port,bytes+2,2);memcpy(&a->sin_addr,bytes+4,4);*length=sizeof *a;return 1;
    }
    if(family==23 && size>=28) { struct sockaddr_in6 *a=(struct sockaddr_in6 *)out;a->sin6_family=AF_INET6;
#ifdef __APPLE__
        a->sin6_len=sizeof *a;
#endif
        memcpy(&a->sin6_port,bytes+2,2);memcpy(&a->sin6_flowinfo,bytes+4,4);memcpy(&a->sin6_addr,bytes+8,16);memcpy(&a->sin6_scope_id,bytes+24,4);*length=sizeof *a;return 1;
    }
    return 0;
}
static inline size_t winsock_address_encode(const struct sockaddr *source,uint8_t bytes[28]) {
    memset(bytes,0,28);
    if(source->sa_family==AF_INET) { const struct sockaddr_in *a=(const struct sockaddr_in *)source;bytes[0]=2;memcpy(bytes+2,&a->sin_port,2);memcpy(bytes+4,&a->sin_addr,4);return 16; }
    if(source->sa_family==AF_INET6) { const struct sockaddr_in6 *a=(const struct sockaddr_in6 *)source;bytes[0]=23;memcpy(bytes+2,&a->sin6_port,2);memcpy(bytes+4,&a->sin6_flowinfo,4);memcpy(bytes+8,&a->sin6_addr,16);memcpy(bytes+24,&a->sin6_scope_id,4);return 28; }
    return 0;
}
#endif
