/*
 * Copyright (c) 2014-2018 The Linux Foundation. All rights reserved.
 *
 * Permission to use, copy, modify, and/or distribute this software for
 * any purpose with or without fee is hereby granted, provided that the
 * above copyright notice and this permission notice appear in all
 * copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
 * WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
 * AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
 * PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#if !defined(__DP_RX_THREAD_H)
#define __DP_RX_THREAD_H

#include <qdf_lock.h>
#include <qdf_event.h>
#include <qdf_threads.h>
#include <wlan_objmgr_vdev_obj.h>
/* Maximum number of REO rings supported (for stats tracking) */
#define DP_RX_TM_MAX_REO_RINGS 4

/* Number of DP RX threads supported */
#define DP_MAX_RX_THREADS DP_RX_TM_MAX_REO_RINGS

/*
 * Macro to get to wait_queue structure. Needed since wait_q is an object.
 * API qdf_wait_queue_interruptible needs the object be passed to it and not a
 * pointer
 */
#define DP_RX_THREAD_GET_WAIT_QUEUE_OBJ(rx_tm_handle_cmn) \
		(((struct dp_rx_tm_handle *)rx_tm_handle_cmn)->wait_q)
/*
 * struct dp_rx_tm_handle_cmn - Opaque handle for rx_threads to store
 * rx_tm_handle. This handle will be common for all the threads.
 * Individual threads should not be accessing
 * elements from dp_rx_tm_handle. It should be via an API.
 */
struct dp_rx_tm_handle_cmn;

/**
 * struct dp_rx_thread_stats - structure holding stats for DP RX thread
 * @nbuf_queued: packets queued into the thread per reo ring
 * @nbuf_dequeued: packets de-queued from the thread
 * @nbuf_sent_to_stack: packets sent to the stack. some dequeued packets may be
 *			dropped due to no peer or vdev, hence this stat.
 * @nbufq_max_len: maximum number of nbuf_lists queued for the thread
 * @dropped_invalid_vdev: packets(nbuf_list) dropped due to no vdev
 * @dropped_invalid_peer: packets(nbuf_list) dropped due to no peer
 * @dropped_others: packets dropped due to other reasons

 */
struct dp_rx_thread_stats {
	unsigned int nbuf_queued[DP_RX_TM_MAX_REO_RINGS];
	unsigned int nbuf_dequeued;
	unsigned int nbuf_sent_to_stack;
	unsigned int nbufq_max_len;
	unsigned int dropped_invalid_vdev;
	unsigned int dropped_invalid_peer;
	unsigned int dropped_others;
};

/**
 * struct dp_rx_thread - structure holding variables for a single DP RX thread
 * @id: id of the dp_rx_thread (0 or 1 or 2..DP_MAX_RX_THREADS - 1)
 * @task: task structure corresponding to the thread
 * @start_event: handle of Event for DP Rx thread to signal startup
 * @suspend_event: handle of Event for DP Rx thread to signal suspend
 * @resume_event: handle of Event for DP Rx thread to signal resume
 * @shutdown_event: handle of Event for DP Rx thread to signal shutdown
 * @event_flag: event flag to post events to DP Rx thread
 * @nbuf_queue:nbuf queue used to store RX packets
 * @nbufq_len: length of the nbuf queue
 * @aff_mask: cuurent affinity mask of the DP Rx thread
 * @stats: per thread stats
 * @rtm_handle_cmn: abstract RX TM handle. This allows access to the dp_rx_tm
 *		    structures via APIs.
 * @napi: napi to deliver packet to stack via GRO
 * @netdev: dummy netdev to initialize the napi structure with
 */
struct dp_rx_thread {
	uint8_t id;
	qdf_thread_t *task;
	qdf_event_t start_event;
	qdf_event_t suspend_event;
	qdf_event_t resume_event;
	qdf_event_t shutdown_event;
	unsigned long event_flag;
	qdf_nbuf_queue_head_t nbuf_queue;
	unsigned long aff_mask;
	struct dp_rx_thread_stats stats;
	struct dp_rx_tm_handle_cmn *rtm_handle_cmn;
	struct napi_struct napi;
	struct net_device netdev;
};

/**
 * enum dp_rx_thread_state - enum to keep track of the state of the rx thread
 * @DP_RX_THREAD_INVALID: initial invalid state
 * @DP_RX_THREAD_INIT: state after being initialized
 * @DP_RX_THREAD_RUNNING: rx thread is functional(NOT suspended, processing
 *			  packets or waiting on a wait_queue)
 * @DP_RX_THREAD_SUSPENDED: rx_thread operation is suspeded from cfg8011 suspend
 */
enum dp_rx_thread_state {
	DP_RX_THREAD_INVALID,
	DP_RX_THREAD_INIT,
	DP_RX_THREAD_RUNNING,
	DP_RX_THREAD_SUSPENDED
};

/**
 * struct dp_rx_tm_handle - DP RX thread infrastructure handle
 * @num_dp_rx_threads: number of DP RX threads initialized
 * @txrx_handle_cmn: opaque txrx handle to get to pdev and soc
 * wait_q: wait_queue for the rx_threads to wait on and expect an event
 * @state: state of the rx_threads. All of them should be in the same state.
 * @rx_thread: array of pointers of type struct dp_rx_thread
 */
struct dp_rx_tm_handle {
	uint8_t num_dp_rx_threads;
	struct dp_txrx_handle_cmn *txrx_handle_cmn;
	qdf_wait_queue_head_t wait_q;
	enum dp_rx_thread_state state;
	struct dp_rx_thread **rx_thread;
};

/**
 * dp_rx_tm_init() - initialize DP Rx thread infrastructure
 * @rx_tm_hdl: dp_rx_tm_handle containing the overall thread infrastructure
 * @num_dp_rx_threads: number of DP Rx threads to be initialized
 *
 * Return: QDF_STATUS_SUCCESS
 */
QDF_STATUS dp_rx_tm_init(struct dp_rx_tm_handle *rx_tm_hdl,
			 uint8_t num_dp_rx_threads);

/**
 * dp_rx_tm_deinit() - de-initialize DP Rx thread infrastructure
 * @rx_tm_hdl: dp_rx_tm_handle containing the overall thread infrastructure
 *
 * Return: QDF_STATUS_SUCCESS on success, error qdf status on failure
 */
QDF_STATUS dp_rx_tm_deinit(struct dp_rx_tm_handle *rx_tm_hdl);

/**
 * dp_rx_tm_enqueue_pkt() - enqueue RX packet into RXTI
 * @rx_tm_hdl: dp_rx_tm_handle containing the overall thread infrastructure
 * @nbuf_list: single or a list of nbufs to be enqueued into RXTI
 *
 * Return: QDF_STATUS_SUCCESS
 */
QDF_STATUS dp_rx_tm_enqueue_pkt(struct dp_rx_tm_handle *rx_tm_hdl,
				qdf_nbuf_t nbuf_list);

/**
 * dp_rx_tm_suspend() - suspend all threads in RXTI
 * @rx_tm_handle: pointer to dp_rx_tm_handle object
 *
 * Return: QDF_STATUS_SUCCESS on success, error qdf status on failure
 */
QDF_STATUS dp_rx_tm_suspend(struct dp_rx_tm_handle *rx_tm_handle);

/**
 * dp_rx_tm_resume() - resume all threads in RXTI
 * @rx_tm_handle: pointer to dp_rx_tm_handle object
 *
 * Return: QDF_STATUS_SUCCESS on success, error qdf status on failure
 */
QDF_STATUS dp_rx_tm_resume(struct dp_rx_tm_handle *rx_tm_handle);

/**
 * dp_rx_tm_dump_stats() - dump stats for all threads in RXTI
 * @rx_tm_handle: pointer to dp_rx_tm_handle object
 *
 * Return: QDF_STATUS_SUCCESS on success, error qdf status on failure
 */
QDF_STATUS dp_rx_tm_dump_stats(struct dp_rx_tm_handle *rx_tm_handle);

/**
 * dp_rx_thread_get_txrx_handle() - get txrx handle from rx_tm_handle_cmn
 * @rx_tm_handle_cmn: opaque pointer to dp_rx_tm_handle_cmn struct
 *
 * Return: pointer to dp_txrx_handle_cmn handle
 */
static inline struct dp_txrx_handle_cmn*
dp_rx_thread_get_txrx_handle(struct dp_rx_tm_handle_cmn *rx_tm_handle_cmn)
{
	return (((struct dp_rx_tm_handle *)rx_tm_handle_cmn)->txrx_handle_cmn);
}

/**
 * dp_rx_thread_get_wait_queue() - get wait_q from dp_rx_tm_handle
 * @rx_tm_handle_cmn: opaque pointer to dp_rx_tm_handle_cmn struct
 *
 * The function is needed since dp_rx_thread does not have access to the real
 * dp_rx_tm_handle structure, but only an opaque dp_rx_tm_handle_cmn handle
 *
 * Return: pointer to dp_txrx_handle_cmn handle
 */
static inline qdf_wait_queue_head_t*
dp_rx_thread_get_wait_queue(struct dp_rx_tm_handle_cmn *rx_tm_handle_cmn)
{
	struct dp_rx_tm_handle *rx_tm_handle;

	rx_tm_handle = (struct dp_rx_tm_handle *)rx_tm_handle_cmn;
	return &rx_tm_handle->wait_q;
}

/**
 * dp_rx_tm_get_napi_context() - get NAPI context for a RX CTX ID
 * @soc: ol_txrx_soc_handle object
 * @rx_ctx_id: RX context ID (RX thread ID) corresponding to which NAPI is
 *             needed
 *
 * Return: NULL on failure, else pointer to NAPI corresponding to rx_ctx_id
 */
struct napi_struct *dp_rx_tm_get_napi_context(struct dp_rx_tm_handle *rx_tm_hdl,
					      uint8_t rx_ctx_id);
#endif /* __DP_RX_THREAD_H */
