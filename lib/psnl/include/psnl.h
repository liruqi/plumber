/**
 * Copyright (C) 2018, Hao Hou
 **/

/**
 * @brief The Plumber Standard Numeric Library
 * @details This is the library that is used for high performance numeric computing
 *          on the Plumber platform
 * @file psnl/include/psnl.h.
 **/
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

#include <pservlet.h>
#include <pstd.h>

#ifndef __PSNL_H__
#define __PSNL_H__
#ifdef __cplusplus
extern "C" {
#endif
#include <psnl/dim.h>
#include <psnl/memobj.h>

#include <psnl/cpu/field.h>
#include <psnl/cpu/field_cont.h>
#ifdef __cplusplus
}
#endif
#endif /*__PSNL_H__ */