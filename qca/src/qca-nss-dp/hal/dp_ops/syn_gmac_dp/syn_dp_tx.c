/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2021 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <linux/netdevice.h>
#include <nss_dp_dev.h>
#include <asm/cacheflush.h>
#include "syn_dma_reg.h"

/*
 * syn_dp_tx_error_cnt
 *	Set the error counters.
 */
static inline void syn_dp_tx_error_cnt(struct syn_dp_info_tx *tx_info, uint32_t status)
{
	atomic64_inc((atomic64_t *)&tx_info->tx_stats.tx_errors);
	(status & DESC_TX_TIMEOUT) ? atomic64_inc((atomic64_t *)&tx_info->tx_stats.tx_jabber_timeout_errors) : 0;
	(status & DESC_TX_FRAME_FLUSHED) ? atomic64_inc((atomic64_t *)&tx_info->tx_stats.tx_frame_flushed_errors) : 0;
	(status & DESC_TX_LOST_CARRIER) ? atomic64_inc((atomic64_t *)&tx_info->tx_stats.tx_loss_of_carrier_errors) : 0;
	(status & DESC_TX_NO_CARRIER) ? atomic64_inc((atomic64_t *)&tx_info->tx_stats.tx_no_carrier_errors) : 0;
	(status & DESC_TX_LATE_COLLISION) ? atomic64_inc((atomic64_t *)&tx_info->tx_stats.tx_late_collision_errors) : 0;
	(status & DESC_TX_EXC_COLLISIONS) ? atomic64_inc((atomic64_t *)&tx_info->tx_stats.tx_excessive_collision_errors) : 0;
	(status & DESC_TX_EXC_DEFERRAL) ? atomic64_inc((atomic64_t *)&tx_info->tx_stats.tx_excessive_deferral_errors) : 0;
	(status & DESC_TX_UNDERFLOW) ? atomic64_inc((atomic64_t *)&tx_info->tx_stats.tx_underflow_errors) : 0;
	(status & DESC_TX_IPV4_CHK_ERROR) ? atomic64_inc((atomic64_t *)&tx_info->tx_stats.tx_ip_header_errors) : 0;
	(status & DESC_TX_PAY_CHK_ERROR) ? atomic64_inc((atomic64_t *)&tx_info->tx_stats.tx_ip_payload_errors) : 0;
}

/*
 * syn_dp_tx_clear_buf_entry
 *	Clear the Tx info after Tx is over.
 */
static inline void syn_dp_tx_clear_buf_entry(struct syn_dp_info_tx *tx_info, uint32_t tx_skb_index)
{
	tx_info->tx_buf_pool[tx_skb_index].len = 0;
	tx_info->tx_buf_pool[tx_skb_index].skb = NULL;
}

/*
 * syn_dp_tx_set_desc
 *	Populate the tx desc structure with the buffer address.
 */
static inline void syn_dp_tx_set_desc(struct syn_dp_info_tx *tx_info,
					   uint32_t buffer, struct sk_buff *skb, uint32_t offload_needed,
					   uint32_t status)
{
	uint32_t tx_idx = tx_info->tx_idx;
	struct dma_desc_tx *txdesc = tx_info->tx_desc + tx_idx;
	unsigned int length = skb->len;

#ifdef SYN_DP_DEBUG
	BUG_ON(atomic_read((atomic_t *)&tx_info->busy_tx_desc_cnt) > SYN_DP_TX_DESC_SIZE);
	BUG_ON(txdesc != (tx_info->tx_desc + tx_idx));
	BUG_ON(!syn_dp_gmac_is_tx_desc_empty(txdesc));
	BUG_ON(syn_dp_gmac_is_tx_desc_owned_by_dma(txdesc));
#endif

	if (likely(length <= SYN_DP_MAX_DESC_BUFF_LEN)) {
		txdesc->length = ((length << DESC_SIZE1_SHIFT) & DESC_SIZE1_MASK);
		txdesc->buffer2 = 0;
	} else {
		txdesc->length = (SYN_DP_MAX_DESC_BUFF_LEN << DESC_SIZE1_SHIFT) & DESC_SIZE1_MASK;
		txdesc->length |= ((length - SYN_DP_MAX_DESC_BUFF_LEN) << DESC_SIZE2_SHIFT) & DESC_SIZE2_MASK;
		txdesc->buffer2 = buffer + SYN_DP_MAX_DESC_BUFF_LEN;
	}

	txdesc->buffer1 = buffer;

	tx_info->tx_buf_pool[tx_idx].skb = skb;
	tx_info->tx_buf_pool[tx_idx].len = length;

	/*
	 * Ensure all write completed before setting own by dma bit so when gmac
	 * HW takeover this descriptor, all the fields are filled correctly
	 */
	wmb();

	txdesc->status = (status | ((offload_needed) ? DESC_TX_CIS_TCP_PSEUDO_CS : 0) | ((tx_idx == (SYN_DP_TX_DESC_SIZE - 1)) ? DESC_TX_DESC_END_OF_RING : 0));

	tx_info->tx_idx = syn_dp_tx_inc_index(tx_idx, 1);
}

/*
 * syn_dp_tx_complete
 *	Xmit complete, clear descriptor and free the skb
 */
int syn_dp_tx_complete(struct syn_dp_info_tx *tx_info, int budget)
{
	int busy;
	uint32_t status;
	struct dma_desc_tx *desc = NULL;
	struct sk_buff *skb;
	uint32_t tx_skb_index, len;
	uint32_t tx_packets = 0, total_len = 0;
	uint32_t skb_free_start_idx;
	uint32_t skb_free_end_idx = SYN_DP_TX_INVALID_DESC_INDEX;
	uint32_t skb_free_abs_end_idx = 0;


	busy = atomic_read((atomic_t *)&tx_info->busy_tx_desc_cnt);

	if (unlikely(!busy)) {

		/*
		 * No descriptors are held by GMAC DMA, we are done
		 */
		netdev_dbg(tx_info->netdev, "No descriptors held by DMA");
		return 0;
	}

	if (likely(busy > budget)) {
		busy = budget;
	}

	skb_free_start_idx = syn_dp_tx_comp_index_get(tx_info);
	do {
		desc = syn_dp_tx_comp_desc_get(tx_info);
		status = desc->status;
		if (unlikely(syn_dp_gmac_is_tx_desc_owned_by_dma(status))) {

			/*
			 * Descriptor still held by gmac dma, so we are done.
			 */
			break;
		}

		tx_skb_index = syn_dp_tx_comp_index_get(tx_info);
		skb = tx_info->tx_buf_pool[tx_skb_index].skb;
		len = tx_info->tx_buf_pool[tx_skb_index].len;

		if (likely(status & DESC_TX_LAST)) {
			if (likely(!(status & DESC_TX_ERROR))) {
#ifdef SYN_DP_DEBUG
				BUG_ON(!skb);
#endif
				/*
				 * No error, recored tx pkts/bytes and collision.
				 */
				tx_packets++;
				total_len += len;
			} else {

				/*
				 * Some error happen, collect error statistics.
				 */
				syn_dp_tx_error_cnt(tx_info, status);
			}
		}
		skb_free_end_idx = tx_info->tx_comp_idx;
		tx_info->tx_comp_idx = syn_dp_tx_inc_index(tx_info->tx_comp_idx, 1);

		/*
		 * Busy tx descriptor is reduced by one as
		 * it will be handed over to Processor now.
		 */
		atomic_dec((atomic_t *)&tx_info->busy_tx_desc_cnt);
	} while (--busy);

	/*
	 * Descriptors still held by DMA. We are done.
	 */
	if (skb_free_end_idx == SYN_DP_TX_INVALID_DESC_INDEX) {
		goto done;
	}

	if (skb_free_end_idx < skb_free_start_idx) {

		/*
		 * If the ring is wrapped around, get the absolute end index.
		 */
		skb_free_abs_end_idx = skb_free_end_idx;
		skb_free_end_idx = SYN_DP_TX_DESC_MAX_INDEX;
	}

	while (skb_free_end_idx >= skb_free_start_idx) {
		dev_kfree_skb_any(tx_info->tx_buf_pool[skb_free_start_idx].skb);
		syn_dp_tx_clear_buf_entry(tx_info, skb_free_start_idx);
		skb_free_start_idx++;
	}

	if (skb_free_abs_end_idx) {
		skb_free_start_idx = 0;
		while (skb_free_start_idx <= skb_free_abs_end_idx) {
			dev_kfree_skb_any(tx_info->tx_buf_pool[skb_free_start_idx].skb);
			syn_dp_tx_clear_buf_entry(tx_info, skb_free_start_idx);
			skb_free_start_idx++;
		}
	}
done:
	atomic64_add(tx_packets, (atomic64_t *)&tx_info->tx_stats.tx_packets);
	atomic64_add(total_len, (atomic64_t *)&tx_info->tx_stats.tx_bytes);

	return budget - busy;
}

/*
 * syn_dp_tx
 *	TX routine for Synopsys GMAC
 */
int syn_dp_tx(struct syn_dp_info_tx *tx_info, struct sk_buff *skb)
{
	struct net_device *netdev = tx_info->netdev;
	dma_addr_t dma_addr;

	/*
	 * If we don't have enough tx descriptor for this pkt, return busy.
	 */
	if (unlikely((SYN_DP_TX_DESC_SIZE - atomic_read((atomic_t *)&tx_info->busy_tx_desc_cnt)) < 1)) {
		atomic_inc((atomic_t *)&tx_info->tx_stats.tx_desc_not_avail);
		netdev_dbg(netdev, "Not enough descriptors available");
		return -1;
	}

	dma_addr = (dma_addr_t)virt_to_phys(skb->data);

	dmac_clean_range((void *)skb->data, (void *)(skb->data + skb->len));

	/*
	 * Queue packet to the GMAC rings
	 */
	syn_dp_tx_set_desc(tx_info, dma_addr, skb, (skb->ip_summed == CHECKSUM_PARTIAL),
				(DESC_TX_LAST | DESC_TX_FIRST | DESC_TX_INT_ENABLE | DESC_OWN_BY_DMA));

	syn_resume_dma_tx(tx_info->mac_base);
	atomic_inc((atomic_t *)&tx_info->busy_tx_desc_cnt);

	return 0;
}
