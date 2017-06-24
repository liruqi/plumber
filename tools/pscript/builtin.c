/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/stat.h>
#include <errno.h>

#include <constants.h>
#include <error.h>

#include <plumber.h>
#include <pss.h>
#include <module.h>

extern char const* const* module_paths;

static pss_value_t _pscript_builtin_print(pss_vm_t* vm, uint32_t argc, pss_value_t* argv)
{
	(void)vm;
	pss_value_t ret = {};
	uint32_t i;
	for(i = 0; i < argc; i ++)
	{
		char buf[4096];
		if(ERROR_CODE(size_t) == pss_value_strify_to_buf(argv[i], buf, sizeof(buf)))
		{
			LOG_ERROR("Type error: Got invalid value");
			ret.kind = PSS_VALUE_KIND_ERROR;
			break;
		}

		printf("%s", buf);
	}

	putchar('\n');

	return ret;
}

static pss_value_t _pscript_builtin_dict(pss_vm_t* vm, uint32_t argc, pss_value_t* argv)
{
	(void)vm;
	(void)argc;
	(void)argv;
	pss_value_t ret = {.kind = PSS_VALUE_KIND_ERROR};

	ret = pss_value_ref_new(PSS_VALUE_REF_TYPE_DICT, NULL);

	return ret;
}

static pss_value_t _pscript_builtin_len(pss_vm_t* vm, uint32_t argc, pss_value_t* argv)
{
	(void)vm;
	pss_value_t ret = {
		.kind = PSS_VALUE_KIND_ERROR,
		.num  = PSS_VM_ERROR_TYPE
	};
	if(argc < 1) 
	{
		ret.num = PSS_VM_ERROR_ARGUMENT;
		LOG_ERROR("Argument error: len function requires at least one argument");
		return ret;
	}

	if(argv[0].kind != PSS_VALUE_KIND_REF) return ret;

	switch(pss_value_ref_type(argv[0]))
	{
		case PSS_VALUE_REF_TYPE_DICT:
		{
			pss_dict_t* dict = (pss_dict_t*)pss_value_get_data(argv[0]);
			if(NULL == dict) break;
			uint32_t result = pss_dict_size(dict);
			if(ERROR_CODE(uint32_t) == result) break;
			ret.kind = PSS_VALUE_KIND_NUM;
			ret.num = result;
			break;
		}
		case PSS_VALUE_REF_TYPE_STRING:
		{
			const char* str = (const char*)pss_value_get_data(argv[0]);
			if(NULL == str) break;
			ret.kind = PSS_VALUE_KIND_NUM;
			ret.num = (int64_t)strlen(str);
			break;
		}
		default:
			LOG_ERROR("Type error: len fucntion doesn't support the input type");
			break;
	}
	return ret;
}

static pss_value_t _pscript_builtin_import(pss_vm_t* vm, uint32_t argc, pss_value_t* argv)
{
	pss_value_t ret = {
		.kind = PSS_VALUE_KIND_ERROR,
		.num = PSS_VM_ERROR_TYPE
	};

	if(argc < 1) 
	{
		ret.num = PSS_VM_ERROR_ARGUMENT;
		LOG_ERROR("Argument error: len function requires at least one argument");
		return ret;
	}
	
	uint32_t i;
	for(i = 0; i < argc; i ++)
	{
		if(argv[i].kind != PSS_VALUE_KIND_REF || pss_value_ref_type(argv[i]) != PSS_VALUE_REF_TYPE_STRING) return ret;

		const char* name = (const char*)pss_value_get_data(argv[0]);

		if(module_is_loaded(name)) continue;

		pss_bytecode_module_t* module = module_from_file(name, 1, 1, NULL);
		if(NULL == module) 
		{
			LOG_ERROR("Module error: Cannot load the required module named %s", name);
			return ret;
		}

		if(ERROR_CODE(int) == pss_vm_run_module(vm, module, NULL))
		{
			LOG_ERROR("Module error: The module returns with an error code");
			return ret;
		}
	}

	ret.kind = PSS_VALUE_KIND_UNDEF;

	return ret;
}

static pss_value_t _pscript_builtin_insmod(pss_vm_t* vm, uint32_t argc, pss_value_t* argv)
{
	(void)vm;
	pss_value_t ret = {
		.kind = PSS_VALUE_KIND_ERROR,
		.num = PSS_VM_ERROR_TYPE
	};

	if(argc < 1)
	{
		ret.num =  PSS_VM_ERROR_ARGUMENT;
		LOG_ERROR("Argument error: len function requires at least one argument");
		return ret;
	}

	if(argv[0].kind != PSS_VALUE_KIND_REF || pss_value_ref_type(argv[0]) != PSS_VALUE_REF_TYPE_STRING)
	{
		ret.kind = PSS_VALUE_KIND_ERROR;
		ret.num  = PSS_VM_ERROR_TYPE;
		LOG_ERROR("Type error: String argument expected in the insmod builtin");
		return ret;
	}
	
	uint32_t module_arg_cap = 32, module_argc = 0, i;
	char** module_argv = (char**)malloc(module_arg_cap * sizeof(char*));
	const itc_module_t* binary = NULL;
	if(NULL == module_argv)
	{
		LOG_ERROR_ERRNO("Cannot create the argument buffer");
		ret.kind = PSS_VALUE_KIND_ERROR;
		ret.num  = PSS_VM_ERROR_INTERNAL;
		return ret;
	}

	for(i = 0; i < argc; i ++)
	{
		const char* mod_init_str = pss_value_get_data(argv[i]);

		if(NULL == mod_init_str)
		{
			LOG_ERROR_ERRNO("Cannot get the initialization string");
			ret.kind = PSS_VALUE_KIND_ERROR;
			ret.num  = PSS_VM_ERROR_INTERNAL;
			return ret;
		}

		const char* ptr, *begin = mod_init_str;
		for(begin = ptr = mod_init_str;; ptr ++)
		{
			if(*ptr == ' ' || *ptr == 0)
			{
				if(ptr - begin > 0)
				{
					if(module_argc >= module_arg_cap)
					{
						char** new_argv = (char**)realloc(argv, sizeof(char*) * module_arg_cap * 2);
						if(new_argv == NULL)
							ERROR_LOG_ERRNO_GOTO(ERR, "Cannot resize the argument buffer");
						module_argv = new_argv;
						module_arg_cap = module_arg_cap * 2;
					}

					module_argv[module_argc] = (char*)malloc((size_t)(ptr - begin + 1));
					if(NULL == module_argv[module_argc])
						ERROR_LOG_ERRNO_GOTO(ERR, "Cannot allcoate memory for the argument string");

					memcpy(module_argv[module_argc], begin, (size_t)(ptr - begin));
					module_argv[module_argc][ptr-begin] = 0;
					module_argc ++;
				}
				begin = ptr + 1;
			}

			if(*ptr == 0) break;
		}
	}


	binary = itc_binary_search_module(module_argv[0]);
	if(NULL == binary) ERROR_LOG_ERRNO_GOTO(ERR, "Cannot find the module binary named %s", module_argv[0]);

	LOG_DEBUG("Found module binary @%p", binary);

	if(ERROR_CODE(int) == itc_modtab_insmod(binary, module_argc - 1, (char const* const*) module_argv + 1))
	    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot instantiate the mdoule binary using param");

	ret.kind = PSS_VALUE_KIND_UNDEF;

	goto CLEANUP;
ERR:
	ret.kind = PSS_VALUE_KIND_ERROR;
	ret.num  = PSS_VM_ERROR_INTERNAL;
CLEANUP:

	if(NULL != module_argv)
	{
		uint32_t i;
		for(i = 0; i < module_argc; i ++)
		    free(module_argv[i]);

		free(module_argv);
	}

	return ret;
}

static pss_value_t _external_get(const char* name)
{
	lang_prop_value_t value = lang_prop_get(name);
	
	pss_value_t ret;
	switch(value.type)
	{
		case LANG_PROP_TYPE_ERROR:
			ret.kind = PSS_VALUE_KIND_ERROR;
			ret.num  = PSS_VM_ERROR_INTERNAL;
			return ret;
		case LANG_PROP_TYPE_INTEGER:
			ret.kind = PSS_VALUE_KIND_NUM;
			ret.num  = value.num;
			return ret;
		case LANG_PROP_TYPE_STRING:
			ret = pss_value_ref_new(PSS_VALUE_REF_TYPE_STRING, value.str);
			return ret;
		case LANG_PROP_TYPE_NONE:
			ret.kind = PSS_VALUE_KIND_UNDEF;
			return ret;
		default:
			ret.kind = PSS_VALUE_KIND_ERROR;
			ret.num  = PSS_VM_ERROR_INTERNAL;
	}

	return ret;
}

static int _external_set(const char* name, pss_value_t data)
{
	lang_prop_value_t val = {};

	switch(data.kind)
	{
		case PSS_VALUE_KIND_NUM:
			val.type = LANG_PROP_TYPE_INTEGER;
			val.num  = data.num;
			return lang_prop_set(name, val);
		case PSS_VALUE_KIND_REF:
			if(pss_value_ref_type(data) == PSS_VALUE_REF_TYPE_STRING)
			{
				val.type = LANG_PROP_TYPE_STRING;
				if(NULL == (val.str  = pss_value_get_data(data)))
					ERROR_RETURN_LOG(int, "Cannot get the string value from the string object");
				return lang_prop_set(name, val);
			}
		default:
			return 0;
	}
}

int builtin_init(pss_vm_t* vm)
{

	pss_vm_external_global_ops_t ops = {
		.get = _external_get,
		.set = _external_set
	};

	if(ERROR_CODE(int) == pss_vm_set_external_global_callback(vm, ops))
		ERROR_RETURN_LOG(int, "Cannot register the external global accessor");

	if(ERROR_CODE(int) == pss_vm_add_builtin_func(vm, "print", _pscript_builtin_print))
		ERROR_RETURN_LOG(int, "Cannot register the builtin function print");

	if(ERROR_CODE(int) == pss_vm_add_builtin_func(vm, "dict", _pscript_builtin_dict))
		ERROR_RETURN_LOG(int, "Cannot register the builtin function print");

	if(ERROR_CODE(int) == pss_vm_add_builtin_func(vm, "len", _pscript_builtin_len))
		ERROR_RETURN_LOG(int, "Cannot register the builtin function len");

	if(ERROR_CODE(int) == pss_vm_add_builtin_func(vm, "import", _pscript_builtin_import))
		ERROR_RETURN_LOG(int, "Cannot register the builtin function import");

	if(ERROR_CODE(int) == pss_vm_add_builtin_func(vm, "insmod", _pscript_builtin_insmod))
		ERROR_RETURN_LOG(int, "Cannot register the builtin function insmod");

	return 0;
}
