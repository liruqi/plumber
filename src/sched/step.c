/**
 * Copyright (C) 2017, Hao Hou
 **/

#include <plumber.h>
#include <utils/log.h>
#include <error.h>

static __thread sched_rscope_t* _current_request_scope = NULL;

sched_rscope_t* sched_step_current_scope()
{
	return _current_request_scope;
}

int sched_step_next(itc_module_type_t type)
{
	sched_task_t* task = NULL;
	uint32_t size, i;
	const sched_service_pipe_descriptor_t* result;
	itc_module_pipe_t *pipes[2];

	task = sched_task_next_ready_task();

	if(NULL == task)
	{
		LOG_DEBUG("No task is ready");
		return 0;
	}

	if(NULL == (result = sched_service_get_outgoing_pipes(task->service, task->node, &size)))
	    ERROR_LOG_GOTO(LERR, "Cannot get outgoing pipes");

	for(i = 0; i < size; i ++)
	{
		runtime_api_pipe_flags_t out_flags = 0, in_flags = 0;

		out_flags = sched_service_get_pipe_flags(task->service, result[i].source_node_id, result[i].source_pipe_desc);
		if(ERROR_CODE(runtime_api_pipe_flags_t) == out_flags)
		    ERROR_LOG_GOTO(LERR, "Cannot get output pipe flags");

		in_flags = sched_service_get_pipe_flags(task->service, result[i].destination_node_id, result[i].destination_pipe_desc);
		if(ERROR_CODE(runtime_api_pipe_flags_t) == in_flags)
		    ERROR_LOG_GOTO(LERR, "Cannot get input pipe flags");

		size_t input_header_size, output_header_size;

		if(ERROR_CODE(size_t) == (output_header_size = sched_service_get_pipe_type_size(task->service,
		                                                                                result[i].source_node_id,
		                                                                                result[i].source_pipe_desc)))
		    ERROR_LOG_GOTO(LERR, "Cannot get the size of output header size");

		if(ERROR_CODE(size_t) == (input_header_size = sched_service_get_pipe_type_size(task->service,
		                                                                               result[i].destination_node_id,
		                                                                               result[i].destination_pipe_desc)))
		    ERROR_LOG_GOTO(LERR, "Cannot get the size of output header size");

		itc_module_pipe_param_t param = {
			.output_flags  = out_flags,
			.output_header = output_header_size,
			.input_flags   = in_flags,
			.input_header  = input_header_size,
			.args = NULL
		};

		if(out_flags & RUNTIME_API_PIPE_SHADOW)
		{
			runtime_api_pipe_id_t target_pid = RUNTIME_API_PIPE_GET_TARGET(out_flags);
			runtime_api_pipe_flags_t disabled = (out_flags & RUNTIME_API_PIPE_DISABLED);
			pipes[0] = NULL;
			pipes[1] = itc_module_pipe_fork(task->exec_task->pipes[target_pid], in_flags | RUNTIME_API_PIPE_SHADOW | target_pid | disabled, input_header_size, NULL);

			if(ERROR_CODE(int) == sched_task_output_shadow(task, result[i].source_pipe_desc, pipes[1]))
			    ERROR_LOG_GOTO(LERR, "Cannot add the forked pipe as shadow");
		}
		else if(itc_module_pipe_allocate(type, 0, param, pipes + 0, pipes + 1) < 0)
		    ERROR_LOG_GOTO(LERR, "Cannot allocate pipe from <NID = %d, PID = %d> -> <NID = %d, PID = %d>",
		                         result[i].source_node_id, result[i].source_pipe_desc,
		                         result[i].destination_node_id, result[i].destination_pipe_desc);

		if(pipes[0] != NULL && sched_task_output_pipe(task, result[i].source_pipe_desc, pipes[0]) == ERROR_CODE(int))
		    ERROR_LOG_GOTO(LERR, "Cannot assign output pipe to the task");

		if(sched_task_input_pipe(task->service, task->request, result[i].destination_node_id, result[i].destination_pipe_desc, pipes[1]) == ERROR_CODE(int))
		    ERROR_LOG_GOTO(LERR, "Cannot assign the input pipe to the downstream task");
	}

#ifdef ENABLE_PROFILER
	static __thread int counter = 0;

	if(sched_service_profiler_timer_start(task->service, task->node) == ERROR_CODE(int))
	    LOG_WARNING("Cannot start the profiler");
	counter ++;
#endif
	_current_request_scope = task->scope;
	if(runtime_task_start(task->exec_task) == ERROR_CODE(int))
	{
#ifdef ENABLE_PROFILER
		if(sched_service_profiler_timer_stop(task->service) == ERROR_CODE(int))
		    LOG_WARNING("Cannot stop the profiler");
#endif
		ERROR_LOG_GOTO(LERR, "Task failed");
	}
#ifdef ENABLE_PROFILER
	if(sched_service_profiler_timer_stop(task->service) == ERROR_CODE(int))
	    LOG_WARNING("Cannot stop the profiler");
	_current_request_scope = NULL;
	if(counter > 10000)
	{
		sched_service_profiler_flush(task->service);
		counter = 0;
	}
#endif

	if(sched_task_free(task) < 0) LOG_WARNING("Cannot dispose task");

	return 1;

LERR:
	if(task) sched_task_free(task);
	return ERROR_CODE(int);
}
