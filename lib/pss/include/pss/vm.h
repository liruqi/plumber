/**
 * Copyright (C) 2017, Hao Hou
 **/
/**
 * @brief The Bytecode virtual machine for PSS
 * @file include/pss/vm.h
 **/
#ifndef __PSS_VM_H__
#define __PSS_VM_H__

/**
 * @brief The data type for the PSS Virtual Machine
 **/
typedef struct _pss_vm_t pss_vm_t;

/**
 * @brief The error code of the VM
 **/
typedef enum {
	PSS_VM_ERROR_NONE,       /*!< Everything is good */
	PSS_VM_ERROR_INTERNAL,   /*!< The internal error */
	PSS_VM_ERROR_BYTECODE,   /*!< An invalid bytecode */
	PSS_VM_ERROR_TYPE,       /*!< The instruction gets an unsupported type */
	PSS_VM_ERROR_ARITHMETIC, /*!< The arithmetic error */
	PSS_VM_ERROR_STACK,      /*!< The stack overflow exception */
	PSS_VM_ERROR_ARGUMENT,   /*!< The argument error */
	PSS_VM_ERROR_MODULE,     /*!< We can not find the required module */
	PSS_VM_ERROR_IMPORT,     /*!< We failed import a script */
	PSS_VM_ERROR_FAILED,     /*!< The requested operation failed */
	PSS_VM_ERROR_ADD_NODE,   /*!< Cannot add node to the service graph */
	PSS_VM_ERROR_PIPE,       /*!< We cannot add pipe connecting two servlet */
	PSS_VM_ERROR_SERVICE,    /*!< Cannot start the service */
	PSS_VM_ERROR_UNDEF,      /*!< Operating an undefined value */
	PSS_VM_ERROR_NONFUNC,    /*!< Calling a non-function */
	PSS_VM_ERROR_SECONDARY   /*!< The secondary error, which means this is caused by other failure */
} pss_vm_error_t;

/**
 * @brief The stack backtrace
 **/
typedef struct _pss_vm_bracktrace_t {
	uint32_t     line;   /*!< The line number */
	const char*  func;   /*!< The function name (include the file name) */
	struct _pss_vm_bracktrace_t* next;  /*!< The next element in the stack */
} pss_vm_backtrace_t;

/**
 * @brief Represent a VM runtime error information
 **/
typedef struct {
	pss_vm_backtrace_t* backtrace;  /*!< The stack backtrack */
	pss_vm_error_t      code;       /*!!< The error code */
	const char*         message;    /*!< The message */
} pss_vm_exception_t;

/**
 * @brief The callback functions for the external globals
 **/
typedef struct {
	/**
	 * @brief The getter func
	 * @param name The name of the variable
	 * @return The get result
	 **/
	pss_value_t (*get)(const char* name);
	/**
	 * @brief The setter func
	 * @param name The name of the variable
	 * @param data The data we want to put
	 * @return The number of field that has been wrrite, or error code
	 **/
	int (*set)(const char* name, pss_value_t data);
} pss_vm_external_global_ops_t;


/**
 * @brief Create a new PSS virtual machine
 * @return The newly created PSS virtual machine
 **/
pss_vm_t* pss_vm_new(void);

/**
 * @brief Set the callback function that handles the external globals
 * @param vm The virtual machine
 * @param ops The operation callback
 * @return status code
 **/
int pss_vm_set_external_global_callback(pss_vm_t* vm, pss_vm_external_global_ops_t ops);

/**
 * @brief Add a builtin function
 * @param vm The virtual machine we want to add the builtin function to
 * @param func The function pointer
 * @param name The name of the builtin function
 * @return status code
 **/
int pss_vm_add_builtin_func(pss_vm_t* vm, const char* name, pss_value_builtin_t func);

/**
 * @brief Set the global variable of this virtual machine
 * @param vm The virtual machine we want to add
 * @param var The global variable name
 * @param val The value
 * @return status code
 **/
int pss_vm_set_global(pss_vm_t* vm, const char* var, pss_value_t val);

/**
 * @brief Get the global variable from the given virtual machine
 * @param vm The vm we want to peek
 * @param var The variable name
 * @return The result value
 **/
pss_value_t pss_vm_get_global(pss_vm_t* vm, const char* var);

/**
 * @brief Dispose an used PSS virtual machine
 * @param vm The virtual machine to dispose
 * @return status code
 **/
int pss_vm_free(pss_vm_t* vm);

/**
 * @brief Run a bytecode module in the PSS virtual machine
 * @param vm The virtual machine used to run the code
 * @param module The module to run
 * @param retbuf The return buffer
 * @return stauts code
 **/
int pss_vm_run_module(pss_vm_t* vm, const pss_bytecode_module_t* module, pss_value_t* retbuf);

/**
 * @brief Run a closure with the PSS VM
 * @param vm The VM used to run the code
 * @param module The module to run
 * @param retbuf The return buffer
 * @return status code
 **/
int pss_vm_run_closure(pss_vm_t* vm, pss_value_t closure_value, pss_value_t* retbuf);

/**
 * @brief Get the last exception occured with the virtual machine
 * @param vm The virtual machine, after we did this, the VM well become a good state again
 * @return The exception description
 **/
pss_vm_exception_t* pss_vm_last_exception(pss_vm_t* vm);

/**
 * @brief Dispose a exception description
 * @param exception The exception description
 * @return status code
 **/
int pss_vm_exception_free(pss_vm_exception_t* exception);

/**
 * @brief Kill current execution
 * @param vm the target vm
 * @return status code
 **/
int pss_vm_kill(pss_vm_t* vm);


#endif
