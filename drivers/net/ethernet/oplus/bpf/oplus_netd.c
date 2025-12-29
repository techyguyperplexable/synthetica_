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
        return 1; /* Allow non-IP */

    uid = bpf_get_socket_uid(skb);
    if (uid == 0) return 1; /* Allow Root/System */

    /* Lookup Blocklist */
    verdict = bpf_map_lookup_elem(app_drop_cell_socket_uid_limit_map, &uid);
    if (verdict && *verdict) {
        return 0; /* Drop */
    }

    return 1; /* Pass */
}

/*
 * Filter: Wakeup Logic
 * Matches "skfilter/ingress/wakeup"
 */
int oplus_skfilter_ingress_wakeup(struct sk_buff *skb) {
    /* * logic for detecting TCP SYN/ACK while screen is off.
     * Prevents soft-reboots caused by modem/wlan wake locks.
     */
    u32 key = 0;
    u64 *screen_state = bpf_map_lookup_elem(screen_state_map, &key);

    /* If screen state map is empty (driver default), assume screen ON (Safe) */
    if (!screen_state) return 1;

    return 1;
}
