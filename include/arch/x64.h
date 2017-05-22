/**
 * Copyright (C) 2017, Hao Hou
 **/

/**
 * @brief the CPU specified code on X64 CPU
 * @note All the functions with the name *_sw_* means single-write-safe function. <br/>
 *       A signle-write-safe function is a function that, can will be able to use the function
 *       in the senario that there's only one writer but multiple readers. <br/>
 *       The property of a single-write-safe function should be, the function should write back all
 *       the result to the memory at the same time. <br/>
 *       This is used to avoid the case that the writer writes memroy in different times, so the reader
 *       may get some half updated values, which is unexpected <br/>
 *       The function that meets the single-writer-safe standard should avoid this and make sure the
 *       data integrity all the time.
 * @file x64.h
 **/
#ifndef __PLUMBER_ARCH_X64_H__
#define __PLUMBER_ARCH_X64_H__

#	ifdef __amd64__

/**
 * @breif the "atomic" increment operation on a 64 bit unsigned interger
 * @note the meaning of SW (signle writer) means there's only one thread is
 *       possible to perform this operation. This means during the time we
 *       do the operation, there's no need to lock the bus if we make sure
 *       the data is updated by only a single instruction.  <br/>
 *       Since we do not have another writer, so there's no race condition here <br/>
 *       Note it's not means *real atomic* operations, but it means writing to memory
 *       is amotic, which means no one will see partially written data.
 * @param var the address to the variable to increment
 * @return nothing
 **/
static inline void arch_atomic_sw_increment_u64(uint64_t* var)
{
	(*var) ++;
}

/**
 * @brief the siglee-writer-safe version of "atomic" increment on a 32 bit unsigned integer
 * @note the detailed description of it's behavior is similar to the 64 bit version besides it only
 *       take a 32 bit unsigned integer
 * @param var the address to the variable to incrment
 * @return nothing
 **/
static inline void arch_atomic_sw_increment_u32(uint32_t* var)
{
	(*var) ++;
}

/**
 * @brief performe the assignment for an size_t integer
 * @note this means the data should be write to memory at the same time
 * @param var the address to the variable to increment
 * @param val the new value to assign
 * @return nothing
 **/
static inline void arch_atomic_sw_assignment_sz(size_t* var, size_t val)
{
	(*var) = val;
}

/**
 * @biref perfom the asgginment operation for an usngined 32 bit integer in a single-writer-safe way
 * @note similar to it's 64 bit counter part, only difference is it takes uint32_t
 * @param var the address to the variable to increment
 * @param val the nwe value to assign
 * @return nothing
 **/
static inline void arch_atomic_sw_assignment_u32(uint32_t* var, uint32_t val)
{
	(*var) = val;
}
#	endif /* __amd64__ */

#endif /* __PLUMBER_ARCH_X64_H__ */
