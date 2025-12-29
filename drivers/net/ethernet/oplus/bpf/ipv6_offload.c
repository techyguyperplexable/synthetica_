// SPDX-License-Identifier: GPL-2.0
#include "bpf_shim.h"

struct tether_key6 {
    struct in6_addr neigh6;
    u8 dstMac[ETH_ALEN];
};

struct tether_val {
    int oif;
    struct ethhdr macHeader;
    u16 pmtu;
};

struct tether_stats {
    u32 rxPackets;
    u32 rxBytes;
    u32 rxErrors;
    u32 txPackets;
    u32 txBytes;
    u32 txErrors;
};

DEFINE_BPF_MAP_GRW(tether_downstream6_map, HASH, struct tether_key6, struct tether_val, 64, 0);
DEFINE_BPF_MAP_GRW(tether_stats_map, HASH, u32, struct tether_stats, 16, 0);

int sched_cls_tether_downstream6_ether(struct sk_buff *skb) {
    struct ipv6hdr *ip6;
    struct ethhdr *eth = (struct ethhdr *)skb->data;
    void *data_end = (void *)(long)skb->tail;
    struct tether_key6 key; /* Moved declaration up here for C90 compliance */
    struct tether_val *v;

    if (skb->protocol != htons(ETH_P_IPV6)) 
        return TC_ACT_PIPE;

    if ((void*)(eth + 1) > data_end) 
        return TC_ACT_PIPE;
        
    ip6 = (struct ipv6hdr *)(eth + 1);
    if ((void*)(ip6 + 1) > data_end) 
        return TC_ACT_PIPE;

    if (ip6->hop_limit <= 1) 
        return TC_ACT_PIPE; 

    /* Now we can use the variable declared above */
    key.neigh6 = ip6->daddr;
    /* We don't set dstMac in key because we rely on lookup failure anyway */
    
    v = bpf_map_lookup_elem(tether_downstream6_map, &key);
    
    if (!v) return TC_ACT_PIPE;

    ip6->hop_limit--;
    return TC_ACT_PIPE; 
}
