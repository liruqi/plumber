/**
 * Copyright (C) 2018, Hao Hou
 **/
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>

#include <sys/epoll.h>
#include <sys/eventfd.h>

#include <curl/curl.h>

#include <pservlet.h>
#include <pstd.h>

#include <barrier.h>
#include <arch/arch.h>

#include <client.h>

typedef struct _thread_ctx_t _thread_ctx_t;

/**
 * @brief The data structure used to tracking a request
 **/
typedef struct {
	uint32_t                    in_use:1;      /*!< indicates if this request buffer is being used */
	uint32_t                    epolled:1;     /*!< If this request is already managed by epoll */
	union {
		int                     priority;      /*!< The priority of this request */
		uint32_t                next_unused;   /*!< The next element unused request buffer list */
	};
	uint64_t                    serial_num;    /*!< The serial number for this request */
	_thread_ctx_t*              thread_ctx;    /*!< The ower thread ctx */
	const char*                 url;           /*!< The target URL */
	CURL*                       curl_handle;   /*!< The CURL hndle object, NULL if this object haven't been picked up */
	async_handle_t*             async_handle;  /*!< The servlet asynchronous handle, used for completion notification */
	client_request_setup_func_t setup_cb;      /*!< The setup callback */
	void*                       setup_data;    /*!< The data used by the setup callback */
	char**                      result_buf;    /*!< The result buffer */
	size_t*                     result_size_buf; /*!< The result size buffer */
	size_t                      buf_cap;       /*!< The buffer capacity */
	int                         fd;            /*!< The underlying conmmunication fd */
} _req_t;

/**
 * @brief The client loop context, it may have multiple threads
 * @todo Currently we use epoll direcly for simplicity. 
 *       But we need to support other flavor of event driven APIs
 **/
struct _thread_ctx_t {
	/***** The global resources ********/
	uint32_t  started:1;    /*!< Indicates if the client thread has been started */
	uint32_t  killed:1;     /*!< Indicates if this client has been killed */
	uint32_t  tid:30;       /*!< The thread ID */
	int       epoll_fd;     /*!< The epoll fd */
	CURLM*    curlm;        /*!< The CURL Multi interface */
	_req_t*   req_buf;      /*!< The buffer for the request data */
	pthread_t thread;       /*!< Current pthread object */
	pthread_mutex_t writer_mutex; /*!< The writter mutex */
	pthread_cond_t writer_cond;  /*!< The writer condition variable */
	
	/******** The unused request buffer list ******/
	uint32_t  unused;       /*!< The unused request list */

	/******** The pending-for-add quee ************/
	int                event_fd;        /*!< The event fd use for add notification */
	volatile uint32_t  add_queue_front; /*!< The front pointer of the add queue */
	volatile uint32_t  add_queue_rear;  /*!< The rear pointer of the add queue */
	uint32_t*          add_queue;       /*!< The actual pending queue */

	/******** The pending request heap ***********/
	uint32_t* req_heap;            /*!< The pending request heap */
	uint32_t  req_heap_size;       /*!< The pending request heap size */
};

/**
 * @brief The global context of the client library 
 **/
static struct {
	uint32_t        ref_cnt;            /*!< How many servlet is currently using this */
	pthread_mutex_t mutex;              /*!< The pthread mutex */
	uint32_t        queue_size;         /*!< The maximum number of the queue size for each thread */
	uint32_t        pr_limit;           /*!< The maximum number of parallel request for each thread */
	uint32_t        num_threads;        /*!< The maximum number of client threads */
	uint32_t        thread_cap;         /*!< The capacity of the thread context array */
	uint32_t        num_started_threads;/*!< The number of started client threads */
	_thread_ctx_t*  thread_ctx;         /*!< The thread context objects */
} _ctx;

static inline int _req_cmp(_thread_ctx_t* ctx, uint32_t a, uint32_t b)
{
	_req_t* ra = ctx->req_buf + ctx->req_heap[a];
	_req_t* rb = ctx->req_buf + ctx->req_heap[b];

	if(ra->priority < rb->priority) 
		return -1;
	
	if(ra->priority > rb->priority)
		return 1;
	
	if(ra->serial_num < rb->serial_num)
		return -1;

	if(ra->serial_num > rb->serial_num)
		return 1;

	return 0;
}

static inline void _req_heapify(_thread_ctx_t* ctx, uint32_t root)
{
	for(;;)
	{
		uint32_t idx = root;
		if(root * 2 + 1 < ctx->req_heap_size && _req_cmp(ctx, idx, root * 2 + 1) == -1)
			idx = root * 2 + 1;
		if(root * 2 + 2 < ctx->req_heap_size && _req_cmp(ctx, idx, root * 2 + 2) == -1)
			idx = root * 2 + 2;

		if(idx == root) return;

		uint32_t tmp = ctx->req_heap[root];
		ctx->req_heap[root] = ctx->req_heap[idx];
		ctx->req_heap[idx] = tmp;

		root = idx;
	}
}

static inline void _req_heap_add(_thread_ctx_t* ctx, uint32_t req_idx)
{
	/* Although race condition is possible with the next_serial_num variable,
	 * However, the only way we use the serial num is determine the order within
	 * the same thread. */
	static uint64_t next_serial_num = 0;

	/* Since we will use up all the request buffer at the time the queue is full,
	 * thus we don't need to check if the heap is full, because this is not possible */
	
	ctx->req_heap[ctx->req_heap_size ++] = req_idx;
	ctx->req_buf[req_idx].serial_num = next_serial_num;
	arch_atomic_sw_increment_u64(&next_serial_num);

	uint32_t root = ctx->req_heap_size - 1;

	for(;root > 0 && _req_cmp(ctx, (root - 1) / 2, root) == 1; root = (root - 1) / 2)
	{
		uint32_t tmp = ctx->req_heap[root];
		ctx->req_heap[root] = ctx->req_heap[(root - 1) / 2];
		ctx->req_heap[(root - 1) / 2] = tmp;
	}

	ctx->req_buf[req_idx].thread_ctx = ctx;
}

static inline uint32_t _req_heap_pop(_thread_ctx_t* ctx)
{
	if(ctx->req_heap_size == 0) return ERROR_CODE(uint32_t);

	uint32_t ret = ctx->req_heap[0];
	
	ctx->req_heap[0] = ctx->req_heap[--ctx->req_heap_size];

	_req_heapify(ctx, 0);

	return ret;
}

static int _curl_write_func(char* data, size_t size, size_t count, void* up)
{
	_req_t* req = (_req_t*)up;

	if(*(req->result_buf) == NULL)
	{
		req->buf_cap = 4096;
		*(req->result_size_buf) = 0;
		if(NULL == (*(req->result_buf) = (char*)malloc(req->buf_cap)))
			ERROR_RETURN_LOG_ERRNO(int, "Cannot allocate memory for the result buffer");
	}

	size_t required = size * count;
	if(req->buf_cap <= *(req->result_size_buf) + required)
	{
		size_t new_size = *(req->result_size_buf) + required;
		
		new_size = (new_size & ~(size_t)(4096 - 1)) + 4096;
		
		char* buf = realloc(*(req->result_buf), new_size);
		if(NULL == buf) ERROR_RETURN_LOG_ERRNO(int, "Cannot resize the result buffer");

		*req->result_buf = buf;
		req->buf_cap = new_size;
	}

	memcpy(*req->result_buf + *req->result_size_buf, data, required);
	(*req->result_buf)[(*req->result_size_buf) += required] = 0;

	return (int)required;
}

static int _curl_socket_func(CURL* handle, int fd, int action, void* up, void* sp)
{
	(void)sp;
	(void)up;
	_req_t* req;

	CURLcode rc = curl_easy_getinfo(handle, CURLINFO_PRIVATE, &req);
	if(CURLE_OK != rc)
		ERROR_RETURN_LOG(int, "Cannot get the request data structure: %s", curl_easy_strerror(rc));

	int opcode;

	struct epoll_event event = {
		.events = 0,
		.data = {
			.ptr = req
		}
	};

	switch(action)
	{
		case CURL_POLL_REMOVE:
			opcode = EPOLL_CTL_DEL;
			break;
		case CURL_POLL_IN:
			opcode = EPOLL_CTL_ADD;
			event.events = EPOLLIN;
			break;
		case CURL_POLL_OUT:
			opcode = EPOLL_CTL_ADD;
			event.events = EPOLLOUT;
			break;
		case CURL_POLL_INOUT:
			opcode = EPOLL_CTL_ADD;
			event.events = EPOLLIN | EPOLLOUT;
			break;
		default:
			return 0;
	}

	if(opcode == EPOLL_CTL_ADD && req->epolled)
		opcode = EPOLL_CTL_MOD;

	if(epoll_ctl(req->thread_ctx->epoll_fd, opcode, fd, &event) < 0)
		ERROR_RETURN_LOG_ERRNO(int, "Cannot invoke epoll_ctl: op(%x), fd(%x)", fd, opcode);

	req->fd = fd;

	return 0;
}

static inline int _dispose_req(_thread_ctx_t* ctx, uint32_t idx)
{
	if(ctx->req_buf[idx].curl_handle != NULL)
	{
		CURLMcode mrc =  curl_multi_remove_handle(ctx->curlm, ctx->req_buf[idx].curl_handle);
		if(mrc != CURLM_OK)
			ERROR_RETURN_LOG(int, "Cannot remove the handle from the libcurl multi stack object: %s", curl_multi_strerror(mrc));

		curl_easy_cleanup(ctx->req_buf[idx].curl_handle);
		ctx->req_buf[idx].curl_handle = NULL;
	}

	ctx->req_buf[idx].next_unused = ctx->unused;
	ctx->unused = idx;
	ctx->req_buf[idx].in_use = 0;
	return 0;
}

static void* _client_main(void* data)
{
	_thread_ctx_t* ctx = (_thread_ctx_t*)data;

	if(pthread_mutex_lock(&_ctx.mutex) != 0)
		ERROR_PTR_RETURN_LOG_ERRNO("Cannot acquire the global client lock");

	ctx->started = 1;
	_ctx.num_started_threads ++;

	if(pthread_mutex_unlock(&_ctx.mutex) != 0)
		ERROR_PTR_RETURN_LOG_ERRNO("Cannot release the global client lock");

	LOG_NOTICE("Client thread #%u is started", ctx->tid);

	while(!ctx->killed)
	{
		struct epoll_event events[128];

		int eprc = epoll_wait(ctx->epoll_fd, events, sizeof(events) / sizeof(events[0]), 1000);

		if(eprc < 0) 
		{
			LOG_WARNING_ERRNO("Cannot epoll the event");
			continue;
		}

		/* Then we need to handle all the epoll events */
		int i, num_running_handle = 0;
		for(i = 0; i < eprc; i ++)
			if(events[i].data.ptr != NULL)
			{
				/* If this is an communication FD */
				_req_t* req = (_req_t*)events[i].data.ptr;


				CURLMcode rc = curl_multi_socket_action(ctx->curlm, req->fd, 0, &num_running_handle);
				if(rc != CURLM_OK)
				{
					LOG_WARNING("Cannot send the socket ready event to libcurl: %s", curl_multi_strerror(rc));
					continue;
				}

				int n_msg = 0;
				for(;;)
				{
					CURLMsg* msg = curl_multi_info_read(ctx->curlm, &n_msg);
					if(msg == NULL) break;
					if(msg->msg == CURLMSG_DONE)
					{
						CURL* handle = msg->easy_handle;
						_req_t* cur_req;
						CURLcode get_info_rc = curl_easy_getinfo(handle, CURLINFO_PRIVATE, &cur_req);
						if(CURLE_OK != get_info_rc)
							LOG_WARNING("Cannot get the curl private data: %s", curl_easy_strerror(rc));
						else
						{
							if(ERROR_CODE(int) == async_cntl(cur_req->async_handle, ASYNC_CNTL_NOTIFY_WAIT, 0))
								LOG_WARNING("Cannot notify the completion state");

							if(ERROR_CODE(int) == _dispose_req(ctx, (uint32_t)(cur_req - ctx->req_buf)))
								LOG_WARNING("Cannot dispose the request buffer");
						}
					}
				}
			}

		/* Then we need to add the pending-to-add queue to the request heap */
		while(((ctx->add_queue_rear - ctx->add_queue_front) & (_ctx.queue_size - 1)) > 0)
		{
			_req_heap_add(ctx, ctx->add_queue[ctx->add_queue_front]);
			BARRIER();
			ctx->add_queue_front ++;

			if(pthread_mutex_lock(&ctx->writer_mutex) < 0)
				LOG_WARNING_ERRNO("Cannot acquire the writer mutex for client thread #%u", ctx->tid);

			if(pthread_cond_signal(&ctx->writer_cond) < 0)
				LOG_WARNING_ERRNO("Cannot notify the writer for queue avibilitiy(thread #%u)", ctx->tid);

			if(pthread_mutex_unlock(&ctx->writer_mutex) < 0)
				LOG_WARNING_ERRNO("Cannot release the writer mutex for client thread #%u", ctx->tid);
		}

		/* After that we should create new running request if the number of running handle is less than the 
		 * parallel limit */
		while((uint32_t)num_running_handle < _ctx.pr_limit && ctx->req_heap_size > 0)
		{
			uint32_t idx = _req_heap_pop(ctx);
			_req_t* buf = ctx->req_buf + idx;
			if(NULL == (buf->curl_handle = curl_easy_init()))
				ERROR_LOG_GOTO(CURL_INIT_ERR, "Cannot initialize the libcurl handle object");
			
			CURLcode rc = curl_easy_setopt(buf->curl_handle, CURLOPT_PRIVATE, buf);
			if(rc != CURLE_OK)
				ERROR_LOG_GOTO(CURL_INIT_ERR, "Cannot set the private pointer: %s", curl_easy_strerror(rc));

			rc = curl_easy_setopt(buf->curl_handle, CURLOPT_URL, buf->url);
			if(rc != CURLE_OK)
				ERROR_LOG_GOTO(CURL_INIT_ERR, "Cannot set the URL: %s", curl_easy_strerror(rc));

			rc = curl_easy_setopt(buf->curl_handle, CURLOPT_WRITEFUNCTION, _curl_write_func);
			if(rc != CURLE_OK)
				ERROR_LOG_GOTO(CURL_INIT_ERR, "Cannot set the write function callback: %s", curl_easy_strerror(rc));

			rc = curl_easy_setopt(buf->curl_handle, CURLOPT_WRITEDATA, buf);
			if(rc != CURLE_OK)
				ERROR_LOG_GOTO(CURL_INIT_ERR, "Cannot set the write function data: %s", curl_easy_strerror(rc));
			
			if(buf->setup_cb != NULL && ERROR_CODE(int) == buf->setup_cb(buf->curl_handle, buf->setup_data))
				ERROR_LOG_GOTO(CURL_INIT_ERR, "Cannot configure the CURL handle");

			rc = curl_multi_add_handle(ctx->curlm, buf->curl_handle);
			if(rc != CURLE_OK)
				ERROR_LOG_GOTO(CURL_INIT_ERR, "Cannot add the handle to CURL: %s", curl_multi_strerror(rc));

			num_running_handle ++;
			continue;

CURL_INIT_ERR:

			if(buf->curl_handle != NULL)
			{
				curl_easy_cleanup(buf->curl_handle);
				buf->curl_handle = NULL;
			}

			/* In this case we should cleanup the failed request */ 
			if(ERROR_CODE(int) == async_cntl(buf->async_handle, ASYNC_CNTL_NOTIFY_WAIT, ERROR_CODE(int)))
				LOG_WARNING("Cannot notify the async failure status");

			if(ERROR_CODE(int) == _dispose_req(ctx, idx))
				LOG_WARNING("Cannot dispose the request");
		}
	}

	uint32_t i;
	for(i = 0; i < _ctx.queue_size; i ++)
	{
		if(!ctx->req_buf[i].in_use) continue;
		if(ERROR_CODE(int) == _dispose_req(ctx, i))
			LOG_ERROR("Cannot dispose the currently running request");
	}


	LOG_NOTICE("Client thread #%u is terminated", ctx->tid);

	return NULL;
}

/**
 * @brief Actually start the thread and allocate all the buffer and queues for this thread
 * @return status code 
 **/
static inline int _ensure_threads_started(void)
{
	int rc = 0;
	if(_ctx.num_threads != _ctx.num_started_threads)
	{
		if(pthread_mutex_lock(&_ctx.mutex) != 0)
			ERROR_RETURN_LOG(int, "Cannot lock the global client mutex");
		uint32_t i;
		for(i = 0; i < _ctx.num_threads; i ++)
		{
			if(!_ctx.thread_ctx[i].started)
			{
				LOG_NOTICE("Starting client thread #%u", i);

				if(0 != pthread_mutex_init(&_ctx.thread_ctx[i].writer_mutex, NULL))
					ERROR_RETURN_LOG_ERRNO(int, "Cannot initialize the writer mutex for client thread #%u", i);

				if(NULL == (_ctx.thread_ctx[i].req_buf = (_req_t*)malloc(sizeof(_req_t) * _ctx.queue_size)))
					ERROR_RETURN_LOG_ERRNO(int, "Cannot allocate memory for the request buffer for client thread #%u", i);

				uint32_t j;
				for(j = 0; j < _ctx.queue_size; j ++)
				{
					_ctx.thread_ctx[i].req_buf[j].in_use = 0;
					_ctx.thread_ctx[i].req_buf[j].next_unused = (j == _ctx.queue_size - 1) ? ERROR_CODE(uint32_t) : (j + 1);
				}

				_ctx.thread_ctx[i].unused = 0;

				if(NULL == (_ctx.thread_ctx[i].add_queue = (uint32_t*)malloc(sizeof(uint32_t) * _ctx.queue_size)))
					ERROR_LOG_ERRNO_GOTO(THREAD_ERR, "Cannot allocate memory for the add queue for client thread #%u", i);

				_ctx.thread_ctx[i].add_queue_front = 0;
				_ctx.thread_ctx[i].add_queue_rear  = 0;

				if(NULL == (_ctx.thread_ctx[i].req_heap = (uint32_t*)malloc(sizeof(uint32_t) * _ctx.queue_size)))
					ERROR_LOG_ERRNO_GOTO(THREAD_ERR, "Cannot allocate memory for the request priority queue for client thread #%u", i);
				_ctx.thread_ctx[i].req_heap_size = 0;

				if(pthread_create(&_ctx.thread_ctx[i].thread, NULL, _client_main, _ctx.thread_ctx + i) != 0)
					ERROR_LOG_ERRNO_GOTO(THREAD_ERR, "Cannot start the new client thread #%u", i);

				continue;
THREAD_ERR:
				if(NULL != _ctx.thread_ctx[i].req_buf) free(_ctx.thread_ctx[i].req_buf);
				if(NULL != _ctx.thread_ctx[i].add_queue) free(_ctx.thread_ctx[i].add_queue);
				if(NULL != _ctx.thread_ctx[i].req_heap) free(_ctx.thread_ctx[i].req_heap);

				_ctx.thread_ctx[i].req_buf = NULL;
				_ctx.thread_ctx[i].add_queue = NULL;
				_ctx.thread_ctx[i].req_heap = NULL;
				rc = ERROR_CODE(int);
				break;
			}
		}
		if(pthread_mutex_unlock(&_ctx.mutex) != 0)
			LOG_WARNING_ERRNO("Cannot reloase the global mutex lock");
	}

	return rc;
}

/**
 * @brief Cleanup all the client thread
 * @note The existing request cleanup should be responsible for the client thread itself, 
 *       this function only send the terminating message to the thread and wait the thread
 *       for completion
 * @return status code
 **/
static inline int _thread_cleanup(void)
{
	int rc = 0;
	uint32_t i;

	for(i = 0; i < _ctx.num_threads; i ++)
	{
		if(_ctx.thread_ctx[i].started)
		{
			_ctx.thread_ctx[i].killed = 1;

			uint64_t val = 1;
			int term_rc = 0;
			if(write(_ctx.thread_ctx[i].event_fd, &val, sizeof(val)) != sizeof(uint64_t))
			{
				LOG_ERROR_ERRNO("Cannot write event to the event FD for client thread #%u", i);
				term_rc = ERROR_CODE(int);
			}

			if(term_rc == 0 && pthread_join(_ctx.thread_ctx[i].thread, NULL) != 0)
			{
				LOG_ERROR_ERRNO("Cannot join the client thread #%u", i);
				term_rc = ERROR_CODE(int);
			}

			if(term_rc == ERROR_CODE(int)) rc = ERROR_CODE(int);
		}

		if(NULL != _ctx.thread_ctx[i].req_buf) free(_ctx.thread_ctx[i].req_buf);
		if(NULL != _ctx.thread_ctx[i].add_queue) free(_ctx.thread_ctx[i].add_queue);
		if(NULL != _ctx.thread_ctx[i].req_heap) free(_ctx.thread_ctx[i].req_heap);

		if(NULL != _ctx.thread_ctx[i].curlm) curl_multi_cleanup(_ctx.thread_ctx[i].curlm);
		if(_ctx.thread_ctx[i].event_fd > 0 && close(_ctx.thread_ctx[i].event_fd) < 0)
		{
			LOG_ERROR_ERRNO("Cannot close the event FD for the client thread #%u", i);
			rc = ERROR_CODE(int);
		}
		if(_ctx.thread_ctx[i].epoll_fd > 0 && close(_ctx.thread_ctx[i].epoll_fd) < 0)
		{
			LOG_ERROR_ERRNO("Cannot close the epoll FD for the client thread #%u", i);
			rc = ERROR_CODE(int);
		}

		if(0 != pthread_mutex_destroy(&_ctx.thread_ctx[i].writer_mutex))
		{
			LOG_ERROR_ERRNO("Cannot dispose the writer mutex for client thread #%u", i);
			rc = ERROR_CODE(int);
		}
	}

	return rc;
}

/**
 * @brief Initailize a new client thread
 * @note This function will initialize the epoll, eventfd and curl. 
 *       However, the size of the queue can be changed later. Thus
 *       it should be initialized whenever the thread starts to run
 * @return status code
 **/
static inline int _thread_init(void)
{
	_thread_ctx_t* buf = _ctx.thread_ctx;

	if(_ctx.thread_ctx == NULL)
		buf = _ctx.thread_ctx = (_thread_ctx_t*)malloc(sizeof(*_ctx.thread_ctx));
	else if(_ctx.num_threads == _ctx.thread_cap)
		buf = (_thread_ctx_t*)realloc(_ctx.thread_ctx, sizeof(*_ctx.thread_ctx) * (1 + _ctx.thread_cap));

	if(buf == NULL) ERROR_RETURN_LOG_ERRNO(int, "Cannot allocate memory for the new thread context");

	_ctx.thread_ctx = buf;

	if(_ctx.num_threads == _ctx.thread_cap)
		_ctx.thread_cap ++;

	memset(_ctx.thread_ctx + _ctx.num_threads, 0, sizeof(*_ctx.thread_ctx));

	_thread_ctx_t* current = _ctx.thread_ctx + _ctx.num_threads;

	current->tid = (_ctx.num_threads & 0x3fffffffu);

	if(NULL == (current->curlm = curl_multi_init()))
		ERROR_LOG_GOTO(ERR, "Cannot initialize the CURL Multi interface");

	if(CURLM_OK != curl_multi_setopt(current->curlm, CURLMOPT_SOCKETFUNCTION, _curl_socket_func))
		ERROR_LOG_GOTO(ERR, "Cannot setup the socket callback function for the CURLM");

	if(-1 == (current->epoll_fd = epoll_create1(0)))
		ERROR_LOG_ERRNO_GOTO(ERR, "Cannot create epoll for the HTTP client thread");

	if(-1 == (current->event_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC)))
		ERROR_LOG_ERRNO_GOTO(ERR, "Cannnot create event FD for the HTTP client thread");

	struct epoll_event event = {
		.events = EPOLLIN | EPOLLET,
		.data   = {
			.ptr = NULL
		}
	};

	if(-1 == epoll_ctl(current->epoll_fd, EPOLL_CTL_ADD, current->event_fd, &event))
		ERROR_LOG_GOTO(ERR, "Cannot add event FD to the HTTP client thread");

	_ctx.num_threads ++;

	return 0;
ERR:
	if(NULL != current->curlm)  curl_multi_cleanup(current->curlm);
	if(current->event_fd >= 0) close(current->event_fd);
	if(current->epoll_fd >= 0) close(current->epoll_fd);
	return ERROR_CODE(int);
}

int client_init(uint32_t queue_size, uint32_t parallel_limit, uint32_t num_threads)
{
	if(_ctx.ref_cnt == 0)
	{
		LOG_DEBUG("Initializing libcurl");
		CURLcode rc;
		if(0 != (rc = curl_global_init(CURL_GLOBAL_ALL)))
			ERROR_RETURN_LOG(int, "Cannnot initialize libcurl: %s", curl_easy_strerror(rc));

		if(0 != pthread_mutex_init(&_ctx.mutex, NULL))
			ERROR_RETURN_LOG_ERRNO(int, "Cannot intialize the global client mutex");

		_ctx.queue_size = 128;
		_ctx.pr_limit = 32;
		_ctx.num_threads = 0;
	}

	while(_ctx.num_threads < num_threads)
		if(ERROR_CODE(int) == _thread_init())
		{
			if(_ctx.ref_cnt == 0) curl_global_cleanup();
			ERROR_RETURN_LOG(int, "Cannot initialize the new thread");
		}

	if(_ctx.pr_limit < parallel_limit) _ctx.pr_limit = parallel_limit;

	for(;_ctx.queue_size < queue_size; _ctx.queue_size <<= 1);
	
	_ctx.ref_cnt ++;
	
	return 0;
}

int client_finalize(void)
{
	int ret = 0;
	if(_ctx.ref_cnt > 0 && --_ctx.ref_cnt == 0)
	{
		curl_global_cleanup();
		
		if(NULL != _ctx.thread_ctx)
		{
			if(ERROR_CODE(int) == _thread_cleanup())
			{
				ret = ERROR_CODE(int);
				LOG_ERROR("Cannot cleanup the thread local data");
			}

			free(_ctx.thread_ctx);
		}

		if(0 != pthread_mutex_destroy(&_ctx.mutex))
			ERROR_RETURN_LOG_ERRNO(int, "Cannot dispose the global client mutex");

	}

	return ret;
}

int client_add_request(const char* url, async_handle_t* handle, int priority, int block, client_request_setup_func_t setup, void* setup_data, char** res_buf, size_t* size_buf)
{
	int ret = 0;
	if(NULL == url || NULL == handle || priority < 0 || res_buf == NULL || size_buf == NULL)
		ERROR_RETURN_LOG(int, "Invalid arguments");

	if(ERROR_CODE(int) == _ensure_threads_started())
		ERROR_RETURN_LOG(int, "Cannot ensure all the client threads initialized");

	static __thread uint32_t round_ronbin_next = 0;

	_thread_ctx_t* thread = _ctx.thread_ctx + (round_ronbin_next ++);

	if(round_ronbin_next >= _ctx.num_threads) round_ronbin_next = 0;

	if(!block)
	{
		if(((thread->add_queue_rear - thread->add_queue_front) & (_ctx.queue_size - 1)) == _ctx.queue_size)
			return 0;

		if(pthread_mutex_trylock(&thread->writer_mutex) < 0)
		{
			/* Detect the case that the mutex has been used, thus non-blocking posting failed */
			if(errno == EBUSY) 
				return 0;
			else
				ERROR_RETURN_LOG(int, "Cannot acquire the writer mutex for client thread #%u", thread->tid);
		}
	}
	else if(pthread_mutex_lock(&thread->writer_mutex) < 0)
		ERROR_RETURN_LOG_ERRNO(int, "Cannot acquire the writer mutex for client thread #%u", thread->tid);

	for(;((thread->add_queue_rear - thread->add_queue_front) & (_ctx.queue_size - 1)) == _ctx.queue_size;)
		if(pthread_cond_wait(&thread->writer_cond, &thread->writer_mutex) < 0)
			ERROR_LOG_ERRNO_GOTO(ERR, "Cannot wait for the condition variable");

	uint32_t idx = thread->unused;
	_req_t* req = thread->req_buf + idx;
	thread->unused = req->next_unused;

	/* TODO: fill all the fields for that request */
	req->setup_cb = setup;
	req->setup_data = setup_data;
	req->in_use = 1;
	req->epolled = 0;
	req->priority = priority;
	req->url = url;
	req->curl_handle = NULL;
	req->result_buf = res_buf;
	req->result_size_buf = size_buf;

	*size_buf = 0;
	*res_buf  = NULL;
	req->buf_cap = 0;

	/* Finally wake up the epoll */
	uint64_t val = 1;
	if(write(thread->event_fd, &val, sizeof(val)) < 0)
		ERROR_LOG_ERRNO_GOTO(ERR, "Cannot write event FD for thread #%u", thread->tid);

	goto EXIT;
ERR:
	ret = ERROR_CODE(int);
EXIT:
	if(pthread_mutex_unlock(&thread->writer_mutex) < 0)
		ERROR_RETURN_LOG_ERRNO(int, "Cannot acquire the writer mutex for client thread #%u", thread->tid);

	return ret;
}