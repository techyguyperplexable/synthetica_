#ifndef _BPF_SHIM_H
#define _BPF_SHIM_H

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/if_ether.h>
#include <linux/uidgid.h>
#include <linux/user_namespace.h>
#include <net/sock.h>
#include <net/inet_sock.h>
#include <uapi/linux/pkt_cls.h>

#ifndef TC_ACT_PIPE
#define TC_ACT_PIPE    3
#endif

#ifndef TC_ACT_SHOT
#define TC_ACT_SHOT    2
#endif

#ifndef TC_ACT_OK
#define TC_ACT_OK      0
#endif

#define __be32 __be32
#define __be16 __be16

/* * FIX: Added '__maybe_unused' to prevent compiler errors 
 * when a map is defined but not accessed in the code.
 */
#define DEFINE_BPF_MAP_GRW(name, type, ktype, vtype, max, flags) \
    static void *name __maybe_unused = NULL

static inline void *bpf_map_lookup_elem(void *map, void *key) {
    return NULL; 
}

static inline u32 bpf_get_socket_uid(struct sk_buff *skb) {
    if (skb->sk && sk_fullsock(skb->sk)) {
        return from_kuid(&init_user_ns, skb->sk->sk_uid);
    }
    return 0;
}

static inline void bpf_skb_pull_data(struct sk_buff *skb, int len) { }
static inline void __sync_fetch_and_add(volatile u32 *ptr, u32 val) { }

#endif
