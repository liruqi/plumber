/**
 * Copyright (C) 2017, Hao Hou
 **/

#include <stdint.h>
#include <string.h>

#include <constants.h>
#include <utils/vector.h>
#include <utils/log.h>

#include <runtime/api.h>

#include <itc/itc.h>

#include <runtime/pdt.h>
#include <runtime/servlet.h>
#include <runtime/task.h>
#include <runtime/stab.h>

#include <error.h>

/**
 * @brief the servlet binary table
 **/
static vector_t* _btable = NULL;

/**
 * @brief the servlet instance table
 **/
static vector_t* _stable = NULL;

static inline runtime_servlet_t* _get_servlet(runtime_stab_entry_t sid)
{
	if(sid == ERROR_CODE(runtime_stab_entry_t) || (size_t)sid >= vector_length(_stable))
	    ERROR_PTR_RETURN_LOG("Invalid arguments");

	runtime_servlet_t* servlet = *VECTOR_GET_CONST(runtime_servlet_t*, _stable, sid);

	if(NULL == servlet)
	    ERROR_PTR_RETURN_LOG("Cannot read the servlet table");

	return servlet;
}

int runtime_stab_init()
{
	_stable = vector_new(sizeof(runtime_servlet_t*), RUNTIME_SERVLET_TAB_INIT_SIZE);
	if(NULL == _stable)
	    ERROR_RETURN_LOG(int, "Cannot create the servlet instance list");

	_btable = vector_new(sizeof(runtime_servlet_binary_t*), RUNTIME_SERVLET_TAB_INIT_SIZE);
	if(NULL == _btable)
	    ERROR_LOG_GOTO(ERR, "Cannot create the servlet binary list");
	return 0;
ERR:
	if(_stable != NULL) vector_free(_stable);
	if(_btable != NULL) vector_free(_btable);
	return ERROR_CODE(int);
}
int runtime_stab_dispose_instances()
{
	int rc = 0;
	unsigned i;
	if(NULL != _stable)
	{
		for(i = 0; i < vector_length(_stable); i ++)
		    if(ERROR_CODE(int) == runtime_servlet_free(*VECTOR_GET_CONST(runtime_servlet_t*, _stable, i)))
		        rc = ERROR_CODE(int);
		if(vector_free(_stable) < 0) rc = ERROR_CODE(int);
	}

	return rc;
}
int runtime_stab_finalize()
{
	int rc = 0;
	unsigned i;

	if(NULL != _btable)
	{
		for(i = 0; i < vector_length(_btable); i ++)
		    runtime_servlet_binary_unload(*VECTOR_GET_CONST(runtime_servlet_binary_t*, _btable, i));

		if(vector_free(_btable) < 0) rc = ERROR_CODE(int);
	}

	return rc;
}

runtime_stab_entry_t runtime_stab_load(uint32_t argc, char const * const * argv)
{
	if(argc < 1 || argv == NULL || argv[0] == NULL) ERROR_RETURN_LOG(runtime_stab_entry_t, "Invalid arguments");

	const char* name = argv[0];

	unsigned i;
	const runtime_servlet_binary_t* binary = NULL;
	size_t nentry = vector_length(_btable);

	for(i = 0; i < nentry; i ++)
	{
		binary = *VECTOR_GET_CONST(const runtime_servlet_binary_t*, _btable, i);
		if(strcmp(binary->name, name) == 0)
		{
			LOG_DEBUG("Servlet binary %s has been previous loaded", name);
			break;
		}
	}


	if(i == nentry)
	{
		LOG_DEBUG("Could not find the servlet binary %s from the servlet binary table, try to load from disk", name);
		const char* path = runtime_servlet_find_binary(name);
		if(NULL == path) ERROR_RETURN_LOG(runtime_stab_entry_t, "Could not find any binary for servlet %s", name);

		LOG_DEBUG("Found servlet binary %s matches name %s", path, name);

		binary = runtime_servlet_binary_load(path, name);

		if(NULL == binary) ERROR_RETURN_LOG(runtime_stab_entry_t, "Could not load binary %s", path);

		vector_t* newtab = vector_append(_btable, &binary);
		if(NULL == newtab) ERROR_RETURN_LOG(runtime_stab_entry_t, "Could not append the newly loaded binary to the binary table");

		_btable = newtab;
	}

	runtime_servlet_t* servlet = runtime_servlet_new(binary, argc, argv);

	if(NULL == servlet) ERROR_RETURN_LOG(runtime_stab_entry_t, "Could not create new servlet instance for %s", argv[0]);

	vector_t* tmp = vector_append(_stable, &servlet);
	if(NULL == tmp)
	{
		runtime_servlet_free(servlet);
		ERROR_RETURN_LOG(runtime_stab_entry_t, "Failed to insert the servlet to servlet table");
	}
	_stable = tmp;

	return (runtime_stab_entry_t)vector_length(_stable) - 1;
}

runtime_task_t* runtime_stab_create_exec_task(runtime_stab_entry_t sid, runtime_task_flags_t flags)
{
	runtime_servlet_t* servlet = _get_servlet(sid);

	if(NULL == servlet) return NULL;

	return runtime_task_new(servlet, (flags & ~RUNTIME_TASK_FLAG_ACTION_MASK) | RUNTIME_TASK_FLAG_ACTION_EXEC);
}

size_t runtime_stab_num_pipes(runtime_stab_entry_t sid)
{
	const runtime_servlet_t* servlet = _get_servlet(sid);

	if(NULL == servlet) return ERROR_CODE(size_t);

	return runtime_pdt_get_size(servlet->pdt);

}

const runtime_pdt_t* runtime_stab_get_pdt(runtime_stab_entry_t sid)
{
	const runtime_servlet_t* servlet = _get_servlet(sid);

	if(NULL == servlet) return NULL;

	return servlet->pdt;
}

runtime_api_pipe_id_t runtime_stab_get_pipe(runtime_stab_entry_t sid, const char* pipe)
{
	if(NULL == pipe) ERROR_RETURN_LOG(runtime_api_pipe_id_t, "Invalid arguments");

	const runtime_pdt_t* pdt = runtime_stab_get_pdt(sid);

	if(NULL == pdt) return ERROR_CODE(runtime_api_pipe_id_t);

	return runtime_pdt_get_pd_by_name(pdt, pipe);
}

runtime_api_pipe_flags_t runtime_stab_get_pipe_flags(runtime_stab_entry_t sid, runtime_api_pipe_id_t pipe)
{
	const runtime_servlet_t* servlet = _get_servlet(sid);

	if(NULL == servlet) return ERROR_CODE(runtime_api_pipe_flags_t);

	return runtime_pdt_get_flags_by_pd(servlet->pdt, pipe);
}

int runtime_stab_get_num_input_pipe(runtime_stab_entry_t sid)
{
	const runtime_servlet_t* servlet = _get_servlet(sid);

	if(NULL == servlet) return ERROR_CODE(int);

	return runtime_pdt_input_count(servlet->pdt);

}

int runtime_stab_get_num_output_pipe(runtime_stab_entry_t sid)
{
	const runtime_servlet_t* servlet = _get_servlet(sid);

	if(NULL == servlet) return ERROR_CODE(int);

	return runtime_pdt_output_count(servlet->pdt);

}


const char* runtime_stab_get_description(runtime_stab_entry_t sid)
{
	const runtime_servlet_t* servlet = _get_servlet(sid);

	if(NULL == servlet) return NULL;

	return servlet->bin->define->desc;
}


uint32_t runtime_stab_get_version(runtime_stab_entry_t sid)
{
	const runtime_servlet_t* servlet = _get_servlet(sid);

	if(NULL == servlet) return ERROR_CODE(uint32_t);

	return servlet->bin->define->version;
}

char const* const* runtime_stab_get_init_arg(runtime_stab_entry_t sid, uint32_t* argc)
{
	if(NULL == argc) return NULL;

	const runtime_servlet_t* servlet = _get_servlet(sid);

	if(NULL == servlet) return NULL;

	*argc = servlet->argc;
	return (char const* const*)servlet->argv;
}

int runtime_stab_set_owner(runtime_stab_entry_t sid, const void* owner, int reuse_servlet)
{
	if(ERROR_CODE(runtime_stab_entry_t) == sid || NULL == owner)
	    ERROR_RETURN_LOG(int, "Invalid arguments");

	runtime_servlet_t* servlet = _get_servlet(sid);

	if(NULL == servlet)
	    ERROR_RETURN_LOG(int, "No such servlet numbered %u", sid);

	if(NULL != servlet->owner && !reuse_servlet)
	    ERROR_RETURN_LOG(int, "Try to set the owner reference, reuse servlet is not allowed at this time");

	servlet->owner = owner;

	return 0;
}

const void* runtime_stab_get_owner(runtime_stab_entry_t sid)
{
	if(ERROR_CODE(runtime_stab_entry_t) == sid)
	    return NULL;

	const runtime_servlet_t* servlet = _get_servlet(sid);

	if(NULL == servlet)
	    return NULL;

	return servlet->owner;
}
