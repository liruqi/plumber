/**
 * Copyright (C) 2017, Hao Hou
 **/

#include <stdio.h>

#include <plumber.h>
#include <testenv.h>
#include <utils/log.h>
#include <utils/mempool/objpool.h>
#include <module/builtins.h>

#include <proto.h>

static int __memory_check = 1;

extern int __check_memory_allocation();
extern int __print_memory_leakage();

extern test_case_t test_list[];
int setup();
int teardown();

#ifdef ALLOW_IGNORE_MEMORY_LEAK
void __disable_memory_check()
{
	__memory_check = 0;
}
#endif

static inline int _load_default_module(uint16_t port)
{
	int rc = 0;
	char const * args[3] = {};
	char buf[16];
	snprintf(buf, sizeof(buf), "%u", port);

	args[0] = "test";
	if(itc_modtab_insmod(&module_test_module_def, 1, args) == ERROR_CODE(int))
	    rc = ERROR_CODE(int);

	args[0] = buf;
	if(itc_modtab_insmod(&module_tcp_module_def, 1, args) == ERROR_CODE(int))
	    rc = ERROR_CODE(int);

	if(itc_modtab_insmod(&module_mem_module_def, 0, NULL) == ERROR_CODE(int))
	    rc = ERROR_CODE(int);

	if(itc_modtab_insmod(&module_file_module_def, 0, NULL) == ERROR_CODE(int))
	    rc = ERROR_CODE(int);

	return rc;
}

static const char * const stage_message[] = {
	"initialize plumber",
	"setup test cases",
	"teardown test cases",
	"finalize plumber"
};
int main(int argc, const char** argv)
{
	(void) argc;
	(void) argv;
	int stage = -1;
	if(plumber_init() < 0)
	{
		stage = 0;
		goto ERR;
	}

	if(ERROR_CODE(int) == proto_cache_set_root(TEST_PROTODB_ROOT))
	    goto ERR;

	if(_load_default_module(8888) < 0)
	{
		goto ERR;
	}
	/* by default we should disable the mempool otherwise the memory leak detection won't work */
	if(mempool_objpool_disabled(1) < 0 || setup() < 0)
	{
		stage = 1;
		goto ERR;
	}
	int i, result = 0;
	for(i = 0; NULL != test_list[i].func; i ++)
	{
		test_case_t testcase = test_list[i];
		int case_result = testcase.func();

		if(case_result < 0)
		{
			LOG_ERROR("Test case %s failed", testcase.name);
			result = -1;
		}
		else
		{
			LOG_INFO("Test case %s passed", testcase.name);
		}
	}
	if(teardown() < 0)
	{
		stage = 2;
		goto ERR;
	}
	if(plumber_finalize() < 0)
	{
		stage = 3;
		goto ERR;
	}
	if(__memory_check)
	{
		__print_memory_leakage();
	}
	if(__memory_check && __check_memory_allocation() < 0)
	{
		fprintf(stderr, "detect memory issues, run the program with valgrind\n");
		return -1;
	}
	if(!__memory_check)
	{
		fprintf(stderr, "Warning: memory leak check is disabled\n");
	}
	return result;
ERR:
	fprintf(stderr, "failed to %s", stage_message[stage]);
	if(stage > 1 && stage < 2) teardown();
	if(stage > 0 && stage < 3) plumber_finalize();
	return -1;
}
