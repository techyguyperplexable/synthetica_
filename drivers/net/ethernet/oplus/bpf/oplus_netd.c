// SPDX-License-Identifier: GPL-2.0
#include "bpf_shim.h"

/* Maps */
DEFINE_BPF_MAP_GRW(app_drop_cell_socket_uid_limit_map, HASH, u32, u32, 500, 0);
DEFINE_BPF_MAP_GRW(screen_state_map, HASH, u32, u64, 1, 0);

/*
 * Filter: Drop Cellular Data by UID
 * Matches "skfilter/drop/cell/uid"
 */
int bpf_skfilter_drop_cell_uid(struct sk_buff *skb) {
    u32 uid;
    u32 *verdict;

    if (skb->protocol != htons(ETH_P_IP) && skb->protocol != htons(ETH_P_IPV6))
        return skb->len; /* Accept Non-IP (Full Length) */

    uid = bpf_get_socket_uid(skb);
    if (uid == 0) return skb->len; /* Accept Root/System */

    /* Lookup Blocklist */
    verdict = bpf_map_lookup_elem(app_drop_cell_socket_uid_limit_map, &uid);
    if (verdict && *verdict) {
        return 0; /* Drop (0 bytes) */
    }

    return skb->len; /* Accept (Full Length) - FIX: Was 1 */
}

/*
 * Filter: Wakeup Logic
 * Matches "skfilter/ingress/wakeup"
 */
int oplus_skfilter_ingress_wakeup(struct sk_buff *skb) {
    /* * Native shim: Always return full length to allow packet processing.
     * We don't do the actual wakeup logic here (complex to port),
     * so we just ensure we don't drop or truncate the packet.
     */
    return skb->len; 
}
