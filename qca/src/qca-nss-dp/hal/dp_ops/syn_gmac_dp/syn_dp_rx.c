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

#include <asm/cacheflush.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/netdevice.h>
#include <linux/debugfs.h>

#include <nss_dp_dev.h>
#include "syn_dma_reg.h"

/*
 * syn_dp_rx_reset_one_desc()
 *	Reset the descriptor after Rx is over.
 */
static inline void syn_dp_rx_reset_one_desc(struct dma_desc_rx *rx_desc)
{
	rx_desc->status = 0;
	rx_desc->length &= DESC_RX_DESC_END_OF_RING;
	rx_desc->buffer1 = 0;
	rx_desc->buffer2 = 0;
	rx_desc->extstatus = 0;
}

/*
 * syn_dp_rx_refill_one_desc()
 *	Prepares the descriptor to receive packets.
 */
static inline void syn_dp_rx_refill_one_desc(struct dma_desc_rx *rx_desc,
					uint32_t buffer1, uint32_t length1)
{
#ifdef SYN_DP_DEBUG
	BUG_ON(!syn_dp_gmac_is_rx_desc_empty(rx_desc));
	BUG_ON(syn_dp_gmac_is_rx_desc_owned_by_dma(rx_desc->status));
#endif

	if (likely(length1 <= SYN_DP_MAX_DESC_BUFF_LEN)) {
		rx_desc->length |= ((length1 << DESC_SIZE1_SHIFT) & DESC_SIZE1_MASK);
		rx_desc->buffer2 = 0;
	} else {
		rx_desc->length |= (SYN_DP_MAX_DESC_BUFF_LEN << DESC_SIZE1_SHIFT) & DESC_SIZE1_MASK;
		rx_desc->length |= ((length1 - SYN_DP_MAX_DESC_BUFF_LEN) << DESC_SIZE2_SHIFT) & DESC_SIZE2_MASK;

		/*
		 * Program second buffer address if using two buffers.
		 */
		rx_desc->buffer2 = buffer1 + SYN_DP_MAX_DESC_BUFF_LEN;
	}

	rx_desc->buffer1 = buffer1;
	rx_desc->status = DESC_OWN_BY_DMA;
}

/*
 * syn_dp_rx_refill()
 *	Refill the RX descrptor
 */
void syn_dp_rx_refill(struct syn_dp_info_rx *rx_info)
{
	int empty_count = SYN_DP_RX_DESC_SIZE - atomic_read((atomic_t *)&rx_info->busy_rx_desc_cnt);

	struct net_device *netdev = rx_info->netdev;
	int i;
	dma_addr_t dma_addr;
	struct dma_desc_rx *rx_desc;
	struct sk_buff *skb;
	uint32_t rx_refill_idx;
	uint32_t start, end;

	start = rx_info->rx_refill_idx;
	end = syn_dp_rx_inc_index(start, empty_count);

	for (i = 0; i < empty_count; i++) {
		skb = __netdev_alloc_skb(netdev, SYN_DP_SKB_ALLOC_SIZE, GFP_ATOMIC);
		if (unlikely(skb == NULL)) {
			netdev_dbg(netdev, "Unable to allocate skb, will try next time\n");
			atomic64_inc((atomic64_t *)&rx_info->rx_stats.rx_skb_alloc_errors);
			break;
		}

		skb_reserve(skb, SYN_DP_SKB_HEADROOM + NET_IP_ALIGN);

		dma_addr = (dma_addr_t)virt_to_phys(skb->data);
		dmac_inv_range((void *)skb->data, (void *)(skb->data + SYN_DP_SKB_DATA_INVAL_SIZE));
		rx_refill_idx = rx_info->rx_refill_idx;
		rx_desc = rx_info->rx_desc + rx_refill_idx;

		/*
		 * Set Rx descriptor variables
		 */
		syn_dp_rx_refill_one_desc(rx_desc, dma_addr, SYN_DP_SKB_DATA_INVAL_SIZE);
		rx_info->rx_buf_pool[rx_refill_idx].skb = skb;
		rx_info->rx_buf_pool[rx_refill_idx].map_addr_virt = (size_t)(skb->data);
		rx_info->rx_refill_idx = syn_dp_rx_inc_index(rx_refill_idx, 1);
		atomic_inc((atomic_t *)&rx_info->busy_rx_desc_cnt);
	}

	/*
	 * Batched flush and invalidation of the rx descriptors
	 */
	if (end > start) {
		dmac_flush_range((void *)&rx_info->rx_desc[start], (void *)&rx_info->rx_desc[end] + sizeof(struct dma_desc_rx));
	} else {
		dmac_flush_range((void *)&rx_info->rx_desc[start], (void *)&rx_info->rx_desc[SYN_DP_RX_DESC_MAX_INDEX] + sizeof(struct dma_desc_rx));
		dmac_flush_range((void *)&rx_info->rx_desc[0], (void *)&rx_info->rx_desc[end] + sizeof(struct dma_desc_rx));
	}

	syn_resume_dma_rx(rx_info->mac_base);
}

/*
 * syn_dp_rx()
 *	Process RX packets
 */
int syn_dp_rx(struct syn_dp_info_rx *rx_info, int budget)
{
	struct dma_desc_rx *rx_desc = NULL;
	int frame_length, busy, rx_idx, rx_next_idx;
	uint32_t status, extstatus;
	struct sk_buff *rx_skb;
	struct net_device *netdev = rx_info->netdev;
	uint32_t rx_packets = 0, rx_bytes = 0;
	uint32_t start, end;
	struct syn_dp_rx_buf *rx_buf;
	struct dma_desc_rx *rx_desc_next = NULL;
	uint8_t *next_skb_ptr;

	busy = atomic_read((atomic_t *)&rx_info->busy_rx_desc_cnt);
	if (unlikely(!busy)) {
		/*
		 * No desc are held by GMAC DMA, we are done
		 */
		atomic64_inc((atomic64_t *)&rx_info->rx_stats.rx_no_buffer_errors);
		return 0;
	}

	if (likely(busy > budget)) {
		busy = budget;
	}

	start = rx_info->rx_idx;
	rx_desc = rx_info->rx_desc + start;

	/*
	 * Invalidate all the descriptors we can read in one go.
	 * This could mean we re invalidating more than what we could
	 * have got from hardware, but that should be ok.
	 *
	 * It is expected that speculative prefetches are disabled while
	 * this code is executing.
	 */
	end = syn_dp_rx_inc_index(rx_info->rx_idx, busy);
	if (end > start) {
		dmac_inv_range((void *)&rx_info->rx_desc[start], (void *)&rx_info->rx_desc[end] + sizeof(struct dma_desc_rx));
	} else {
		dmac_inv_range((void *)&rx_info->rx_desc[start], (void *)&rx_info->rx_desc[SYN_DP_RX_DESC_MAX_INDEX] + sizeof(struct dma_desc_rx));
		dmac_inv_range((void *)&rx_info->rx_desc[0], (void *)&rx_info->rx_desc[end] + sizeof(struct dma_desc_rx));
	}

	do {
		status = rx_desc->status;
		if (syn_dp_gmac_is_rx_desc_owned_by_dma(status)) {

			/*
			 * Rx descriptor still hold by GMAC DMA, so we are done
			 */
			break;
		}

		rx_idx = rx_info->rx_idx;
		rx_next_idx = syn_dp_rx_inc_index(rx_idx, 1);
		rx_desc_next = rx_info->rx_desc + rx_next_idx;

		/*
		 * Prefetch the next descriptor, assuming the next descriptor is available
		 * for us to read.
		 */
		prefetch(rx_desc_next);
		rx_buf = &rx_info->rx_buf_pool[rx_idx];

		/*
		 * Prefetch a cacheline (64B) of packet header data for the current SKB.
		 */
		prefetch((void *)rx_buf->map_addr_virt);
		rx_skb = rx_buf->skb;
		next_skb_ptr = (uint8_t *)rx_info->rx_buf_pool[rx_next_idx].skb;
		extstatus = rx_desc->extstatus;
		if (likely(syn_dp_gmac_is_rx_desc_valid(status) && !(extstatus & (DESC_RX_IP_HEADER_ERROR | DESC_RX_IP_PAYLOAD_ERROR)))) {
			/*
			 * We have a pkt to process get the frame length
			 */
			frame_length = syn_dp_gmac_get_rx_desc_frame_length(status);

			/*
			 * Valid packet, collect stats
			 */
			rx_packets++;
			rx_bytes += frame_length;

			/*
			 * Type_trans and deliver to linux
			 */
			skb_put(rx_skb, frame_length);
			rx_skb->protocol = eth_type_trans(rx_skb, netdev);
			if (likely(!(extstatus & DESC_RX_CHK_SUM_BYPASS))) {
				rx_skb->ip_summed = CHECKSUM_UNNECESSARY;
			}

			/*
			 * Size of sk_buff is 184B, which requires 3 cache lines
			 * in ARM core (Each cache line is of size 64B). napi_gro_receive
			 * and skb_put are majorly using variables from sk_buff structure
			 * which falls on either first or third cache lines. So, prefetching
			 * first and third cache line provides better performance.
			 */
			if (likely(next_skb_ptr)) {
				prefetch(next_skb_ptr + SYN_DP_RX_SKB_DATA_OFFSET_CACHE_LINE1);
				prefetch(next_skb_ptr + SYN_DP_RX_SKB_DATA_OFFSET_CACHE_LINE3);
			}

			/*
			 * Deliver the packet to linux
			 */
			napi_gro_receive(&rx_info->napi_rx, rx_skb);
		} else {
			dev_kfree_skb_any(rx_skb);
			atomic64_inc((atomic64_t *)&rx_info->rx_stats.rx_errors);

			if (status & (DESC_RX_CRC | DESC_RX_COLLISION |
					DESC_RX_OVERFLOW | DESC_RX_DRIBBLING |
					DESC_RX_LENGTH_ERROR)) {
				(status & DESC_RX_COLLISION) ? atomic64_inc((atomic64_t *)&rx_info->rx_stats.rx_late_collision_errors) : NULL;
				(status & DESC_RX_DRIBBLING) ? atomic64_inc((atomic64_t *)&rx_info->rx_stats.rx_dribble_bit_errors) : NULL;
				(status & DESC_RX_LENGTH_ERROR) ? atomic64_inc((atomic64_t *)&rx_info->rx_stats.rx_length_errors) : NULL;
				(status & DESC_RX_CRC) ? atomic64_inc((atomic64_t *)&rx_info->rx_stats.rx_crc_errors) : NULL;
				(status & DESC_RX_OVERFLOW) ? atomic64_inc((atomic64_t *)&rx_info->rx_stats.rx_overflow_errors) : NULL;
				(status & DESC_RX_IP_HEADER_ERROR) ? atomic64_inc((atomic64_t *)&rx_info->rx_stats.rx_ip_header_errors) : NULL;
				(status & DESC_RX_IP_PAYLOAD_ERROR) ? atomic64_inc((atomic64_t *)&rx_info->rx_stats.rx_ip_payload_errors) : NULL;
			}
		}

		/*
		 * Reset Rx descriptor and update rx_info information
		 */
		syn_dp_rx_reset_one_desc(rx_desc);
		rx_info->rx_buf_pool[rx_idx].skb = NULL;
		rx_info->rx_idx = syn_dp_rx_inc_index(rx_idx, 1);
		atomic_dec((atomic_t *)&rx_info->busy_rx_desc_cnt);
		busy--;
		rx_desc = rx_desc_next;
	} while (likely(busy > 0));

	/*
	 * Increment total rx packets and byte count.
	 */
	atomic64_add(rx_packets, (atomic64_t *)&rx_info->rx_stats.rx_packets);
	atomic64_add(rx_bytes, (atomic64_t *)&rx_info->rx_stats.rx_bytes);

	return budget - busy;
}
