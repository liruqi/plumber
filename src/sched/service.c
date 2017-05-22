/**
 * Copyright (C) 2017, Hao Hou
 **/

#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <stdint.h>

#include <error.h>
#include <itc/module_types.h>
#include <itc/module.h>
#include <runtime/api.h>
#include <runtime/pdt.h>
#include <runtime/servlet.h>
#include <runtime/task.h>
#include <runtime/stab.h>

#include <sched/service.h>
#include <sched/cnode.h>
#include <sched/prof.h>
#include <sched/type.h>

#include <utils/vector.h>
#include <utils/log.h>
#include <utils/string.h>

/**
 * @brief the actual defination of the service scheduer buffer
 **/
struct _sched_service_buffer_t {
	uint32_t  reuse_servlet:1;            /*!< Indicates if we allows the servlet be used twice in the graph, make sure you use this only for testing */
	vector_t* nodes;                      /*!< the list of the nodes in ADG */
	vector_t* pipes;                      /*!< all the pipes that has been added, used for the duplication check */
	sched_service_node_id_t input_node;   /*!< the entry point of this service */
	runtime_api_pipe_id_t input_pipe;     /*!< the entry pipe of this service */
	sched_service_node_id_t output_node;  /*!< the exit point of this service */
	runtime_api_pipe_id_t output_pipe;    /*!< the output pipe of this service */
};

/**
 * @brief the data structure that used to represent a node in the service
 **/
typedef struct {
	uint16_t incoming_count;                    /*!< how many incoming pipes */
	uint16_t outgoing_count;                    /*!< how many outgoing pipes */
	runtime_stab_entry_t servlet_id;            /*!< the servlet ID */
	const void* args;                           /*!< the argument for this node */
	char**   pipe_type;                         /*!< the concrete type of the pipe */
	size_t*  pipe_header_size;                  /*!< the size of pipe header */
	runtime_task_flags_t flags;                 /*!< the additional task flags */
	sched_service_pipe_descriptor_t* outgoing;  /*!< outgoing list */
	uintptr_t __padding__[0];
	sched_service_pipe_descriptor_t incoming[0];/*!< the incoming list */
} _node_t;
STATIC_ASSERTION_SIZE(_node_t, incoming, 0);
STATIC_ASSERTION_LAST(_node_t, incoming);
/* We should make sure that the pipe counter won't overflow */
STATIC_ASSERTION_SIZE(_node_t, incoming_count, sizeof(runtime_api_pipe_id_t));
STATIC_ASSERTION_SIZE(_node_t, outgoing_count, sizeof(runtime_api_pipe_id_t));

/**
 * @brief the defination for the service
 **/
struct _sched_service_t {
	sched_service_node_id_t input_node;   /*!< the entry point of this service */
	sched_service_node_id_t output_node;  /*!< the exit point of this service */
	runtime_api_pipe_id_t input_pipe;     /*!< the entry pipe of this service */
	runtime_api_pipe_id_t output_pipe;    /*!< the output pipe of this service */
	sched_cnode_info_t*   c_nodes;        /*!< the critical node */
	size_t node_count;                    /*!< how many nodes in this service */
	sched_prof_t*         profiler;       /*!< the profiler for this service */
	uintptr_t __padding__[0];
	_node_t*  nodes[0];                   /*!< the node list */
};

STATIC_ASSERTION_SIZE(sched_service_t, nodes, 0);
STATIC_ASSERTION_LAST(sched_service_t, nodes);
STATIC_ASSERTION_TYPE_COMPATIBLE(sched_service_t, input_node, sched_service_pipe_descriptor_t, source_node_id);
STATIC_ASSERTION_TYPE_COMPATIBLE(sched_service_t, output_node, sched_service_pipe_descriptor_t, destination_node_id);
STATIC_ASSERTION_TYPE_COMPATIBLE(sched_service_t, input_pipe, sched_service_pipe_descriptor_t, source_pipe_desc);
STATIC_ASSERTION_TYPE_COMPATIBLE(sched_service_t, output_pipe, sched_service_pipe_descriptor_t, destination_pipe_desc);

/**
 * @brief define a node of the service
 **/
typedef struct {
	runtime_stab_entry_t servlet_id;   /*!< the servlet reference ID */
	runtime_task_flags_t        flags;        /*!< the flags of the task */
} _sched_service_buffer_node_t;

/**
 * @brief allocate a new service
 * @param num_nodes number of nodes in the service
 * @return the newly created service, NULL when error
 **/
static inline sched_service_t* _create_service(size_t num_nodes)
{
	size_t size = num_nodes * sizeof(_node_t*) + sizeof(sched_service_t);

	sched_service_t* ret = (sched_service_t*)malloc(size);
	if(NULL == ret) ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate memory for service def");
	ret->node_count = num_nodes;
	memset(ret->nodes, 0, size - sizeof(sched_service_t));
	ret->c_nodes = NULL;
	return ret;
}

/**
 * @brief create a new node for service
 * @param servlet the servlet ID
 * @param flags the task flags
 * @param incoming_count how many incoming pipes
 * @param outgoing_count how many outgoing pipes
 * @param reuse_servlet if we allows resuse the servlet instance, only used for testing
 * @return the newly created node or NULL when error
 **/
static inline _node_t* _create_node(runtime_stab_entry_t servlet, runtime_task_flags_t flags, uint32_t incoming_count, uint32_t outgoing_count, int reuse_servlet)
{
	size_t size = (incoming_count + outgoing_count) * sizeof(sched_service_pipe_descriptor_t) + sizeof(_node_t);
	_node_t* ret = (_node_t*)malloc(size);
	if(NULL == ret) ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate memory for service node");

	ret->outgoing = ret->incoming + incoming_count;
	ret->incoming_count = ret->outgoing_count = 0;
	ret->servlet_id = servlet;
	ret->flags = flags;

	const runtime_pdt_t* pdt = runtime_stab_get_pdt(servlet);
	if(NULL == pdt)
	    ERROR_LOG_GOTO(ERR, "Cannot get the PDT for servlet %u", servlet);

	runtime_api_pipe_id_t npipes = runtime_pdt_get_size(pdt);
	if(ERROR_CODE(runtime_api_pipe_id_t) == npipes)
	    ERROR_LOG_GOTO(ERR, "Cannot get the size of the PDT");

	if(NULL == (ret->pipe_type = (char**)calloc(npipes, sizeof(char*))))
	    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot allocate memory for the type buffer");

	if(NULL == (ret->pipe_header_size = (size_t*)calloc(npipes, sizeof(size_t))))
	    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot allocate memory for the type size buffer");

	if(ERROR_CODE(int) == runtime_stab_set_owner(servlet, ret, reuse_servlet))
	    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot set the owner of the servlet");

	return ret;

ERR:
	if(NULL != ret)
	{
		if(NULL != ret->pipe_type) free(ret->pipe_type);
		if(NULL != ret->pipe_header_size) free(ret->pipe_header_size);
		free(ret);
	}
	return NULL;
}

/**
 * @brief dispose a used service node
 * @param node the node to dispose
 * @return status code
 **/
static inline int _dispose_node(_node_t* node)
{
	if(NULL != node->pipe_type)
	{

		size_t count = 0;
		runtime_stab_entry_t sid = node->servlet_id;
		const runtime_pdt_t* pdt = runtime_stab_get_pdt(sid);
		if(NULL == pdt)
		    LOG_WARNING("Cannot get the PTD for servlet %u", sid);

		if(ERROR_CODE(size_t) == (count = runtime_pdt_get_size(pdt)))
		{
			LOG_WARNING("Cannot get the size of the PDT table");
			count = 0;
		}

		size_t i;
		for(i = 0; i < count; i ++)
		    if(NULL != node->pipe_type[i])
		        free(node->pipe_type[i]);
		free(node->pipe_type);
	}

	if(NULL != node->pipe_header_size)
	    free(node->pipe_header_size);

	free(node);

	return 0;
}

/**
 * @brief validate the node ID
 * @param buffer the service buffer
 * @param node_id the node ID in the service buffer
 * @return status code
 **/
static inline int _check_node_id(const sched_service_buffer_t* buffer, sched_service_node_id_t node_id)
{
	if(node_id == ERROR_CODE(sched_service_node_id_t)) ERROR_RETURN_LOG(int, "Invalid node id %u", node_id);
	if(node_id >= vector_length(buffer->nodes)) ERROR_RETURN_LOG(int, "Node #%u doesn't exist", node_id);
	return 0;
}

/**
 * @brief validate the pipe
 * @param buffer the service buffer
 * @param node_id the node ID
 * @param pipe_id the pipe ID in that node
 * @param incoming if it's not 0, means should be a incoming pipe, otherwise it should be a outgoing pipe
 * @return status code
 **/
static inline int _check_pipe(const sched_service_buffer_t* buffer, sched_service_node_id_t node_id, runtime_api_pipe_id_t pipe_id, int incoming)
{
	if(_check_node_id(buffer, node_id) == ERROR_CODE(int)) return ERROR_CODE(int);
	const _sched_service_buffer_node_t* node = VECTOR_GET_CONST(_sched_service_buffer_node_t, buffer->nodes, node_id);
	if(NULL == node) ERROR_RETURN_LOG(int, "Cannot read the buffer nodes table");

	size_t npipes = runtime_stab_num_pipes(node->servlet_id);

	if(npipes == ERROR_CODE(size_t) || npipes <= pipe_id || pipe_id == ERROR_CODE(runtime_api_pipe_id_t))
	    ERROR_RETURN_LOG(int, "Invalid pipe ID %d", pipe_id);

	runtime_api_pipe_flags_t flags = runtime_stab_get_pipe_flags(node->servlet_id, pipe_id);

	if(incoming)
	{
		if(!RUNTIME_API_PIPE_IS_INPUT(flags)) ERROR_RETURN_LOG(int, "Input side of the pipe expected");
	}
	else if(!RUNTIME_API_PIPE_IS_OUTPUT(flags)) ERROR_RETURN_LOG(int, "Output side of the pipe expected");

	return 0;
}

/**
 * @brief check if the service is a well formed service, which means input and output pipe
 *        are defined and service do not contains a loop
 * @param service the service to check
 * @return the status code
 **/
static inline int _check_service_graph(const sched_service_t* service)
{
	if(service->nodes[service->input_node]->incoming_count != 0)
	    ERROR_RETURN_LOG(int, "Input node cannot depens on other node");

	if(runtime_stab_get_num_input_pipe(service->nodes[service->input_node]->servlet_id) != 1)
	    ERROR_RETURN_LOG(int, "Input node must have exactly 1 input");

	/* Connecting the output node to another node doesn't make sense, because it won't affect the output */
	if(service->nodes[service->output_node]->outgoing_count != 0)
	    ERROR_RETURN_LOG(int, "Cannot connect output node to another node");

	sched_service_node_id_t id;
	uint32_t i;
	size_t  nz = service->node_count, next;
	//int deg[service->node_count];
	int* deg = (int*)malloc(sizeof(int) *service->node_count);
	if(NULL == deg) ERROR_RETURN_LOG_ERRNO(int, "Cannot allocate memory for the degree array");

	for(id = 0; (size_t)id < service->node_count; id ++)
	{
		deg[id] = service->nodes[id]->incoming_count;
		if(deg[id] == 0 && id != service->input_node)
		{
			LOG_WARNING("node #%d has no incoming pipe", id);
			nz --;
		}
	}

	for(next = service->input_node; nz > 0 && next < service->node_count; nz--)
	{
		for(i = 0; i < service->nodes[next]->outgoing_count; i ++)
		    deg[service->nodes[next]->outgoing[i].destination_node_id] --;
		deg[next] = -1;
		for(next = 0; next < service->node_count && deg[next] != 0; next ++);
	}

	free(deg);

	if(nz != 0) ERROR_RETURN_LOG(int, "A circular dependency is detected in the service graph");

	return 0;
}

sched_service_buffer_t* sched_service_buffer_new()
{
	sched_service_buffer_t* ret = (sched_service_buffer_t*)malloc(sizeof(sched_service_buffer_t));
	if(NULL == ret) ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate memory for the service def buffer");

	if(NULL == (ret->nodes = vector_new(sizeof(_sched_service_buffer_node_t), SCHED_SERVICE_BUFFER_NODE_LIST_INIT_SIZE)))
	    ERROR_LOG_GOTO(ERR, "Cannot create vector for the service buffer node list");

	if(NULL == (ret->pipes = vector_new(sizeof(sched_service_pipe_descriptor_t), SCHED_SERVICE_BUFFER_OUT_GOING_LIST_INIT_SIZE)))
	    ERROR_LOG_GOTO(ERR, "Cannot create vector for the service buffer pipe descriptor list");

	/* fill the undetermined field with error code, so that we can enforce this later */
	ret->input_node = ERROR_CODE(sched_service_node_id_t);
	ret->output_node = ERROR_CODE(sched_service_node_id_t);
	ret->input_pipe = ERROR_CODE(runtime_api_pipe_id_t);
	ret->output_pipe = ERROR_CODE(runtime_api_pipe_id_t);

	return ret;
ERR:
	if(ret != NULL)
	{
		if(ret->pipes != NULL) vector_free(ret->pipes);
		free(ret);
	}
	return NULL;
}

int sched_service_buffer_free(sched_service_buffer_t* buffer)
{
	int rc = 0;
	if(NULL == buffer) return 0;

	if(buffer->nodes != NULL)
	{
		if(vector_free(buffer->nodes) < 0)
		{
			LOG_WARNING("cannot dispose the nodes vector for the service buffer");
			rc = ERROR_CODE(int);
		}
	}

	if(buffer->pipes != NULL && vector_free(buffer->pipes) < 0)
	{
		LOG_WARNING("cannot dispose the pipes vector for the service buffer");
		rc = ERROR_CODE(int);
	}

	free(buffer);

	return rc;
}

int sched_service_buffer_allow_reuse_servlet(sched_service_buffer_t* buffer)
{
	if(NULL == buffer)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	buffer->reuse_servlet = 1;

	return 0;
}

sched_service_node_id_t sched_service_buffer_add_node(sched_service_buffer_t* buffer, runtime_stab_entry_t sid)
{
	if(NULL == buffer || sid == ERROR_CODE(runtime_stab_entry_t))
	    ERROR_RETURN_LOG(sched_service_node_id_t, "Invalid arguments");

	sched_service_node_id_t ret = (sched_service_node_id_t)vector_length(buffer->nodes);

	_sched_service_buffer_node_t node = {
		.servlet_id = sid,
	};

	vector_t* result;
	if((result = vector_append(buffer->nodes, &node)) == NULL)
	    ERROR_RETURN_LOG(sched_service_node_id_t, "Cannot insert new node to the node list");
	buffer->nodes = result;

#ifdef LOG_TRACE_ENABLED
	char name_buffer[128];
	uint32_t argc, i;
	char const * const * argv;
	string_buffer_t sbuf;

	string_buffer_open(name_buffer, sizeof(name_buffer), &sbuf);
	if(NULL == (argv = runtime_stab_get_init_arg(sid, &argc)))
	    string_buffer_appendf(&sbuf, "<INVALID_SID_%d>", sid);
	else
	{
		for(i = 0; i < argc; i ++)
		    if(i == 0)
		        string_buffer_append(argv[i], &sbuf);
		    else
		        string_buffer_appendf(&sbuf, " %s", argv[i]);
	}
	string_buffer_close(&sbuf);

	LOG_TRACE("Servlet (SID = %d) `%s' has been added as node #%u", sid, name_buffer, ret);
#endif
	return ret;
}

int sched_service_buffer_add_pipe(sched_service_buffer_t* buffer, sched_service_pipe_descriptor_t desc)
{
	if(NULL == buffer) ERROR_RETURN_LOG(int, "Invalid arguments");

	sched_service_node_id_t src_node = desc.source_node_id;
	runtime_api_pipe_id_t src_pipe = desc.source_pipe_desc;
	sched_service_node_id_t dst_node = desc.destination_node_id;
	runtime_api_pipe_id_t dst_pipe = desc.destination_pipe_desc;

	if(_check_pipe(buffer, src_node, src_pipe, 0) == ERROR_CODE(int))
	    ERROR_RETURN_LOG(int, "Invalid output pipe <NID=%u, PID=%u>", src_node, src_pipe);

	if(_check_pipe(buffer, dst_node, dst_pipe, 1) == ERROR_CODE(int))
	    ERROR_RETURN_LOG(int, "Invalid input pipe <NID=%u, PID=%u>", dst_node, dst_pipe);

	/* Check if the pipe slot has been used
	 * Because this function is only used when the service defination is updated, so there's almost no
	 * performance impact. The duplication check is just a linear search */

	uint32_t i;
	for(i = 0; i < vector_length(buffer->pipes); i ++)
	{
		const sched_service_pipe_descriptor_t* desc = VECTOR_GET_CONST(sched_service_pipe_descriptor_t, buffer->pipes, i);

		if((src_node == desc->source_node_id && src_pipe == desc->source_pipe_desc) ||
		   (src_node == desc->destination_node_id && src_pipe == desc->destination_pipe_desc))
		    ERROR_RETURN_LOG(int, "Pipe slot <NID=%u,PID=%u> has already in use", src_node, src_pipe);

		if((dst_node == desc->source_node_id && dst_pipe == desc->source_pipe_desc) ||
		   (dst_node == desc->destination_node_id && dst_pipe == desc->destination_pipe_desc))
		    ERROR_RETURN_LOG(int, "Pipe slot <NID=%u,PID=%u> has already in use", dst_node, dst_pipe);
	}

	_sched_service_buffer_node_t* node = VECTOR_GET(_sched_service_buffer_node_t, buffer->nodes, src_node);

	if(NULL == node) ERROR_RETURN_LOG(int, "Cannot read the node list in a service buffer");

	vector_t* result = vector_append(buffer->pipes, &desc);
	if(NULL == result) ERROR_RETURN_LOG(int, "Cannot insert the new descriptor to the pipe list");
	buffer->pipes = result;

	if(NULL == node) ERROR_RETURN_LOG(int, "Cannot read the node list");

	LOG_TRACE("Defined new pipe <NID=%d, SID=%d> -> <NID=%d, SID=%d>", src_node, src_pipe, dst_node, dst_pipe);
	return 0;
}

int sched_service_buffer_set_input(sched_service_buffer_t* buffer, sched_service_node_id_t node, runtime_api_pipe_id_t pipe)
{
	if(NULL == buffer) ERROR_RETURN_LOG(int, "Invalid arguments");
	if(_check_pipe(buffer, node, pipe, 1) < 0) return ERROR_CODE(int);

	buffer->input_node = node;
	buffer->input_pipe = pipe;
	return 0;
}

int sched_service_buffer_set_output(sched_service_buffer_t* buffer, sched_service_node_id_t node, runtime_api_pipe_id_t pipe)
{
	if(NULL == buffer) ERROR_RETURN_LOG(int, "Invalid arguments");
	if(_check_pipe(buffer, node, pipe, 0) < 0) return ERROR_CODE(int);

	buffer->output_node = node;
	buffer->output_pipe = pipe;
	return 0;
}

/**
 * @brief the compare function used to sort the pipe descriptor list by it's source pipe id
 * @param l the left operator
 * @param r the right operator
 * @return compare result
 **/
static inline int _compare_src_pipe(const void* l, const void* r)
{
	const sched_service_pipe_descriptor_t* left = (const sched_service_pipe_descriptor_t*)l;
	const sched_service_pipe_descriptor_t* right = (const sched_service_pipe_descriptor_t*)r;

	if(left->source_pipe_desc < right->source_pipe_desc) return -1;
	if(left->source_pipe_desc > right->source_pipe_desc) return 1;
	return 0;
}

sched_service_t* sched_service_from_buffer(const sched_service_buffer_t* buffer)
{
	uint32_t i;
	uint32_t* incoming_count = NULL;
	uint32_t* outgoing_count = NULL;
	if(NULL == buffer) ERROR_PTR_RETURN_LOG("Invalid arguments");

	size_t num_nodes = vector_length(buffer->nodes);

	if(_check_pipe(buffer, buffer->input_node, buffer->input_pipe, 1) < 0 || _check_pipe(buffer, buffer->output_node, buffer->output_pipe, 0) < 0)
	    ERROR_PTR_RETURN_LOG("Invalid input or output of the service");

	sched_service_t* ret = _create_service(num_nodes);
	if(NULL == ret) ERROR_PTR_RETURN_LOG("Cannot create a new service def");

	size_t counting_array_size = sizeof(uint32_t) * num_nodes;

	if(NULL == (incoming_count = (uint32_t*)calloc(1, counting_array_size))) ERROR_LOG_ERRNO_GOTO(ERR, "Cannot allocate incoming counting array");

	if(NULL == (outgoing_count = (uint32_t*)calloc(1, counting_array_size))) ERROR_LOG_ERRNO_GOTO(ERR, "Cannot allocate outgoing counting array");

	for(i = 0; i < vector_length(buffer->pipes); i ++)
	{
		const sched_service_pipe_descriptor_t* pipe = VECTOR_GET_CONST(sched_service_pipe_descriptor_t, buffer->pipes, i);
		if(NULL == pipe)
		{
			LOG_WARNING("Could not read the pipe list in the service buffer");
			continue;
		}

		sched_service_node_id_t src_node = pipe->source_node_id;
		sched_service_node_id_t dst_node = pipe->destination_node_id;

		incoming_count[dst_node] ++;
		outgoing_count[src_node] ++;
	}

	for(i = 0; i < num_nodes; i ++)
	{
		const _sched_service_buffer_node_t* node = VECTOR_GET_CONST(_sched_service_buffer_node_t, buffer->nodes, i);
		if(NULL == node) ERROR_LOG_ERRNO_GOTO(ERR, "Cannot read the node table in the service buffer");
		if(NULL == (ret->nodes[i] = _create_node(node->servlet_id, node->flags, incoming_count[i], outgoing_count[i], (buffer->reuse_servlet != 0))))
		    ERROR_LOG_GOTO(ERR, "Cannot create node in the service def");
	}

	for(i = 0; i < vector_length(buffer->pipes); i ++)
	{
		const sched_service_pipe_descriptor_t* pipe = VECTOR_GET_CONST(sched_service_pipe_descriptor_t, buffer->pipes, i);
		if(NULL == pipe)
		{
			LOG_WARNING("could not read the pipe list in the service buffer");
			continue;
		}

		sched_service_node_id_t src_node = pipe->source_node_id;
		sched_service_node_id_t dst_node = pipe->destination_node_id;

		memcpy(ret->nodes[dst_node]->incoming + (ret->nodes[dst_node]->incoming_count ++), pipe, sizeof(sched_service_pipe_descriptor_t));
		memcpy(ret->nodes[src_node]->outgoing + (ret->nodes[src_node]->outgoing_count ++), pipe, sizeof(sched_service_pipe_descriptor_t));
	}

	/**
	 * The reason why we need to sort the outgoing pipes is simple.
	 * Because we have shadow pipes, and to declare the shadow pipes we need to know the target pipe id first.
	 * In here, we assume the pipe_define function assign the pipe id incrementally, so that the target pipe *must* have
	 * a smaller pipe id.
	 * In addition, we assume the scheduler initialize the pipe in the order how outgoing pipes are stored in this array.
	 * If we sort the outgoing pipe by its source id, this can guarantee all the target pipe has been initialized before
	 * the attemption of initializing its shadow.
	 * The input pipe is not actually improtant in this case, because when the scheduler initialize the outputs, that means
	 * the task is ready, and all valid input pipes are already initialized
	 **/
	for(i = 0; i < num_nodes; i ++)
	    qsort(ret->nodes[i]->outgoing, ret->nodes[i]->outgoing_count, sizeof(sched_service_pipe_descriptor_t), _compare_src_pipe);

	ret->input_node = buffer->input_node;
	ret->input_pipe = buffer->input_pipe;
	ret->output_node = buffer->output_node;
	ret->output_pipe = buffer->output_pipe;

	if(_check_service_graph(ret) == ERROR_CODE(int)) ERROR_LOG_GOTO(ERR, "Invalid service graph");

	free(incoming_count);
	free(outgoing_count);

	incoming_count = outgoing_count = NULL;

	if(NULL == (ret->c_nodes = sched_cnode_analyze(ret)))
	    ERROR_LOG_GOTO(ERR, "Cannot analyze the critical node");
#ifdef ENABLE_PROFILER
	if(ERROR_CODE(int) == sched_prof_new(ret, &ret->profiler))
	    LOG_WARNING("Cannot initialize the profiler");
#else
	ret->profiler = NULL;
#endif

	if(ERROR_CODE(int) == sched_type_check(ret))
	    ERROR_LOG_GOTO(ERR, "Service type checker failed");

	return ret;
ERR:
	if(ret != NULL)
	{
		for(i = 0; i < num_nodes; i ++)
		    if(ret->nodes[i] != NULL)
		        _dispose_node(ret->nodes[i]);
		if(ret->c_nodes != NULL) sched_cnode_info_free(ret->c_nodes);
		free(ret);
	}
	if(incoming_count != NULL) free(incoming_count);
	if(outgoing_count != NULL) free(outgoing_count);
	return NULL;
}

int sched_service_free(sched_service_t* service)
{
	uint32_t i;
	int rc = 0;

	if(NULL == service) return ERROR_CODE(int);

	for(i = 0; i < service->node_count; i ++)
	    if(service->nodes[i] != NULL && ERROR_CODE(int) == _dispose_node(service->nodes[i]))
	        rc = ERROR_CODE(int);

	if(NULL != service->c_nodes && ERROR_CODE(int) == sched_cnode_info_free(service->c_nodes))
	    rc = ERROR_CODE(int);

#ifdef ENABLE_PROFILER
	if(NULL != service->profiler && ERROR_CODE(int) == sched_prof_free(service->profiler))
	    rc = ERROR_CODE(int);
#endif

	free(service);
	return rc;
}


runtime_task_t* sched_service_create_task(const sched_service_t* service, sched_service_node_id_t nid)
{
	if(NULL == service || nid == ERROR_CODE(sched_service_node_id_t) || (uint32_t)nid >= service->node_count)
	    ERROR_PTR_RETURN_LOG("Invalid arguments");

	const _node_t* node = service->nodes[nid];
	if(NULL == node) ERROR_PTR_RETURN_LOG("Invalid service def, node %u is NULL", nid);

	runtime_task_t* task = runtime_stab_create_exec_task(node->servlet_id, node->flags);
	if(NULL == task) ERROR_PTR_RETURN_LOG("Cannot create new task for service node #%u", nid);
	return task;
}

const sched_service_pipe_descriptor_t* sched_service_get_incoming_pipes(const sched_service_t* service, sched_service_node_id_t nid, uint32_t* nresult)
{
	if(NULL == service || nid == ERROR_CODE(sched_service_node_id_t) || nid >= service->node_count || NULL == nresult)
	    ERROR_PTR_RETURN_LOG("Invalid arguments");

	const _node_t* node = service->nodes[nid];
	if(NULL == node) ERROR_PTR_RETURN_LOG("Invalid service def, node #%u is NULL", nid);
	*nresult = node->incoming_count;
	return node->incoming;
}

const sched_service_pipe_descriptor_t* sched_service_get_outgoing_pipes(const sched_service_t* service, sched_service_node_id_t nid, uint32_t* nresult)
{
	if(NULL == service || nid == ERROR_CODE(sched_service_node_id_t) || nid >= service->node_count || NULL == nresult)
	    ERROR_PTR_RETURN_LOG("Invalid arguments");

	const _node_t* node = service->nodes[nid];

	if(NULL == node) ERROR_PTR_RETURN_LOG("Invalid service def, node #%d is NULL", nid);
	*nresult = node->outgoing_count;
	return node->outgoing;
}

char const* const* sched_service_get_node_args(const sched_service_t* service, sched_service_node_id_t nid, uint32_t* argc)
{
	if(NULL == service || nid == ERROR_CODE(sched_service_node_id_t) || nid >= service->node_count || NULL == argc)
	    ERROR_PTR_RETURN_LOG("Invalid arguments");
	const _node_t* node = service->nodes[nid];
	if(NULL == node) ERROR_PTR_RETURN_LOG("Invalid service def, node #%d is NULL", nid);
	return runtime_stab_get_init_arg(node->servlet_id, argc);
}

runtime_api_pipe_flags_t sched_service_get_pipe_flags(const sched_service_t* service, sched_service_node_id_t nid, runtime_api_pipe_id_t pid)
{
	if(NULL == service || nid == ERROR_CODE(sched_service_node_id_t) || nid >= service->node_count)
	    ERROR_RETURN_LOG(runtime_api_pipe_flags_t, "Invalid arguments");
	const _node_t* node = service->nodes[nid];

	if(NULL == node) ERROR_RETURN_LOG(runtime_api_pipe_flags_t, "Invalid service def, node #%d is NULL", nid);
	return runtime_stab_get_pipe_flags(node->servlet_id, pid);
}

size_t sched_service_get_num_node(const sched_service_t* service)
{
	if(NULL == service) ERROR_RETURN_LOG(size_t, "Invalid arguments");

	return service->node_count;
}

sched_service_node_id_t sched_service_get_input_node(const sched_service_t* service)
{
	if(NULL == service) ERROR_RETURN_LOG(sched_service_node_id_t, "Invalid arguments");

	return service->input_node;
}

sched_service_node_id_t sched_service_get_output_node(const sched_service_t* service)
{
	if(NULL == service) ERROR_RETURN_LOG(sched_service_node_id_t, "Invalid arguments");

	return service->output_node;
}

const sched_cnode_info_t* sched_service_get_cnode_info(const sched_service_t* service)
{
	if(NULL == service) ERROR_PTR_RETURN_LOG("Invalid arguments");

	return service->c_nodes;
}

int sched_service_profiler_timer_start(const sched_service_t* service, sched_service_node_id_t node)
{
	if(NULL == service || node == ERROR_CODE(sched_service_node_id_t)) ERROR_RETURN_LOG(int, "Invlaid arguments");

	if(service->profiler == NULL) return 0;

	return sched_prof_start_timer(service->profiler, node);
}

int sched_service_profiler_timer_stop(const sched_service_t* service)
{
	if(NULL == service) ERROR_RETURN_LOG(int, "Invalid arguments");

	if(service->profiler == NULL) return 0;

	return sched_prof_stop_timer(service->profiler);
}

int sched_service_profiler_flush(const sched_service_t* service)
{
	if(NULL == service) ERROR_RETURN_LOG(int, "Invalid arguments");

	if(service->profiler == NULL) return 0;

	return sched_prof_flush(service->profiler);
}

int sched_service_get_pipe_type(const sched_service_t* service, sched_service_node_id_t node, runtime_api_pipe_id_t pid, char const* * result)
{
	if(NULL == service || ERROR_CODE(sched_service_node_id_t) == node ||
	   node >= service->node_count || ERROR_CODE(runtime_api_pipe_id_t) == pid ||
	   NULL == result)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	runtime_stab_entry_t servlet = service->nodes[node]->servlet_id;
	const runtime_pdt_t* pdt = runtime_stab_get_pdt(servlet);
	if(NULL == pdt)
	    ERROR_RETURN_LOG(int, "Cannot get the PDT for servlet %u", servlet);

	runtime_api_pipe_id_t pcount = runtime_pdt_get_size(pdt);
	if(ERROR_CODE(runtime_api_pipe_id_t) == pcount)
	    ERROR_RETURN_LOG(int, "Cannot get the size of the PDT");

	if(pcount <= pid)
	    ERROR_RETURN_LOG(int, "Invalid pipe id");

	*result = service->nodes[node]->pipe_type[pid];

	return 0;
}

size_t sched_service_get_pipe_type_size(const sched_service_t* service, sched_service_node_id_t node, runtime_api_pipe_id_t pid)
{

	if(NULL == service || ERROR_CODE(sched_service_node_id_t) == node ||
	   node >= service->node_count || ERROR_CODE(runtime_api_pipe_id_t) == pid)
	    ERROR_RETURN_LOG(size_t, "Invalid arguments");

	runtime_stab_entry_t servlet = service->nodes[node]->servlet_id;
	const runtime_pdt_t* pdt = runtime_stab_get_pdt(servlet);
	if(NULL == pdt)
	    ERROR_RETURN_LOG(size_t, "Cannot get the PDT for servlet %u", servlet);

	runtime_api_pipe_id_t pcount = runtime_pdt_get_size(pdt);
	if(ERROR_CODE(runtime_api_pipe_id_t) == pcount)
	    ERROR_RETURN_LOG(size_t, "Cannot get the size of the PDT");

	if(pcount <= pid)
	    ERROR_RETURN_LOG(size_t, "Invalid pipe id");

	return service->nodes[node]->pipe_header_size[pid];
}

int sched_service_set_pipe_type(const sched_service_t* service, sched_service_node_id_t node, runtime_api_pipe_id_t pid, const char* type_name, size_t header_size)
{
	if(NULL == service || ERROR_CODE(sched_service_node_id_t) == node ||
	   node >= service->node_count || ERROR_CODE(runtime_api_pipe_id_t) == pid ||
	   NULL == type_name || header_size == ERROR_CODE(size_t))
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	runtime_stab_entry_t servlet = service->nodes[node]->servlet_id;
	const runtime_pdt_t* pdt = runtime_stab_get_pdt(servlet);
	if(NULL == pdt)
	    ERROR_RETURN_LOG(int, "Cannot get the PDT for servlet %u", servlet);

	runtime_api_pipe_id_t pcount = runtime_pdt_get_size(pdt);
	if(ERROR_CODE(runtime_api_pipe_id_t) == pcount)
	    ERROR_RETURN_LOG(int, "Cannot get the size of the PDT");

	if(pcount <= pid)
	    ERROR_RETURN_LOG(int, "Invalid pipe id");

	if(NULL != (service->nodes[node]->pipe_type[pid]))
	{
		free(service->nodes[node]->pipe_type[pid]);
		service->nodes[node]->pipe_type[pid] = NULL;
	}

	size_t len = strlen(type_name);

	if(NULL == (service->nodes[node]->pipe_type[pid] = (char*)malloc(len + 1)))
	    ERROR_RETURN_LOG_ERRNO(int, "Cannot allocate memory for the new type name");

	memcpy(service->nodes[node]->pipe_type[pid], type_name, len + 1);
	service->nodes[node]->pipe_header_size[pid] = header_size;

	/* Finally we run the callback function attached to this PD */
	runtime_api_pipe_type_callback_t callback;
	void* data;
	if(ERROR_CODE(int) == runtime_pdt_get_type_hook(pdt, pid, &callback, &data))
	    ERROR_RETURN_LOG(int, "Cannot get the callback function from PDT");

	if(NULL != callback && ERROR_CODE(int) == callback(RUNTIME_API_PIPE_FROM_ID(pid), type_name, data))
	    ERROR_RETURN_LOG(int, "The callbak function returns an error, PID = %u, type_name = %s, data = %p", pid, type_name, data);

	return 0;
}

const char* sched_service_get_pipe_type_expr(const sched_service_t* service, sched_service_node_id_t node, runtime_api_pipe_id_t pid)
{
	if(NULL == service || ERROR_CODE(sched_service_node_id_t) == node ||
	   node >= service->node_count || ERROR_CODE(runtime_api_pipe_id_t) == pid)
	    ERROR_PTR_RETURN_LOG("Invlaid arguments");

	const runtime_pdt_t* pipe_table = runtime_stab_get_pdt(service->nodes[node]->servlet_id);

	if(NULL == pipe_table)
	    ERROR_PTR_RETURN_LOG("Cannot get the pipe table for node %u", node);

	return runtime_pdt_type_expr(pipe_table, pid);
}
