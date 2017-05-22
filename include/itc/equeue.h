/**
 * Copyright (C) 2017, Hao Hou
 **/

/**
 * @brief The event queue
 * @details The event queue is actually a group of queues, and each module instance starts a thread
 *          and listen to the incoming request(if the module is able to do so, which means
 *          accept module call is implemented, plus the module has a flag ITC_MODULE_FLAGS_EVENT_LOOP is set).
 *          (See the module function call "get_flags" for the detail about the module flags).
 *          And once the module gets a incoming request
 *          It will put the pair of handles in the equeue, then accept the the next request.
 *          From the scheduler/dispatcher side, it will be able to either check all of queues are empty
 *          or wait until one of the queues is not empty. <br/>
 * @file equeue.h
 **/
#ifndef __PLUMBER_ITC_EQUEUE_H__
#define __PLUMBER_ITC_EQUEUE_H__

/**
 * @brief The event in equeue
 * @details In Plumber, an event is defined as "a pair of input and output pipes, which represents
 *         an IO request". An event comes out means we have unconsumed data that needs to be read. <br/>
 *         For example, a client connect the server (at this point, it's not a event), then it send
 *         the request data to the server, when the server get the data, an event comes out. <br/>
 *         The event will have an input pipe, where data comes from; and an output pipe, where the
 *         response data goes. <br/>
 *         As we can see, the event has to get from the module.
 *         The term used for getting an event from the module is accept. The module call "accept"
 *         is used to get next event. If the module doesn't have the event to get, it should block
 *         the thread.<br/>
 **/
typedef struct {
	itc_module_pipe_t* in;   /*!< the input pipe handle */
	itc_module_pipe_t* out;  /*!< the output pipe handle */
} itc_equeue_event_t;

/**
 * @brief The token used for identify different thread.
 * @details Because the event queue are shared by multiple threads, so we should use something to
 *         identify which thread is accessing the event queue. <br/>
 *         This is the data type used as the identifier of each token. <br/>
 *         For each event loop, it should get a new event queue token when it started. <br/>
 *         For the scheduler/dispatcher, the entire system only allow one dispatcher thread,
 *         so when it start, it should call the function to get a new scheduler token. <br/>
 **/
typedef uint32_t itc_equeue_token_t;

/**
 * @brief initialize the event queue subsystem
 * @return status code
 **/
int itc_equeue_init();

/**
 * @brief finalize the event queue subsystem
 * @return status code
 **/
int itc_equeue_finalize();

/**
 * @brief create new module thread token, this is used for event loop to get a new
 *        event queue token
 * @param size the size of the queue for this type, how many entries is allowed at most in the queue
 * @return the queue token, or error code
 **/
itc_equeue_token_t itc_equeue_module_token(uint32_t size);

/**
 * @brief create new scheduler/dispatcher thread token
 * @return the token, or error code
 **/
itc_equeue_token_t itc_equeue_scheduler_token();

/**
 * @brief put a event in the queue
 * @param token the thread token
 * @param event the event to put
 * @note if the queue is full, this call will block the thread unitl the queue is able to put <br/>
 *       This function must be called with the module token
 * @return status code
 **/
int itc_equeue_put(itc_equeue_token_t token, itc_equeue_event_t event);

/**
 * @brief get a token from the equeue
 * @note this function must have scheduler token to call <br/>
 *       If the all the event loop's local queue are empty, block the thread execution until
 *       next event avaiable
 * @param token the thread token
 * @param buffer the buffer used to return the result
 * @return status code
 **/
int itc_equeue_take(itc_equeue_token_t token, itc_equeue_event_t* buffer);

/**
 * @brief check if the equeue is empty
 * @note this won't block the thread execution, this function must called with the scheduler token
 * @param token the thread token
 * @return &gt;0: the queue is empty <br/>
 *         =0: the queue is not empty <br/>
 *         ERROR_CODE: error cases
 */
int itc_equeue_empty(itc_equeue_token_t token);

/**
 * @brief block execution until the equeue is not empty
 * @param token the treahd token
 * @param killed the flag indicates if the loop gets killed
 * @return status code
 **/
int itc_equeue_wait(itc_equeue_token_t token, const int* killed);

#endif /*__PLUMBER_QUEUE_H__ */
