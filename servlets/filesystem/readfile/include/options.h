/**
 * Copyright (C) 2018, Hao Hou
 **/
/**
 * @brief The servlet options for the readfile servlet
 * @file filesystem/readfile/include/options.h
 **/
#ifndef __OPTIONS_H__
#define __OPTIONS_H__

/**
 * @brief The output mode
 **/
typedef enum {
	OPTIONS_INPUT_MODE_RAW,             /*!< We get the file name from a RAW pipe */
	OPTIONS_INPUT_MODE_STRING,          /*!< We get the file name from a string pipe */
	OPTIONS_INPUT_MODE_STRING_FIELD     /*!< Indicates we get a string field */
} options_input_mode_t;

/**
 * @brief Indicates the output mode
 **/
typedef enum {
	OPTIONS_OUTPUT_MODE_RAW,          /*!< Means we should write the file content to a untyped pipe */
	OPTIONS_OUTPUT_MODE_FILE,         /*!< Means we should procedure a RLS file object */
	OPTIONS_OUTPUT_MODE_HTTP          /*!< Means we should produce a HTTP response */
} options_output_mode_t;

/**
 * @brief The error page when we use HTTP mode
 **/
typedef struct {
	char* filename;    /*!< The file name to the error page */
	char* mime_type;   /*!< The mime type */
} options_output_err_page_t;

/**
 * @brief The servlet options
 **/
typedef struct {
	/* Generic Options */
	options_input_mode_t      input_mode;   /*!< The input mode */
	options_output_mode_t     output_model; /*!< The output mode */
	char*                     root_dir;     /*!< The root directory */
	uint32_t                  inscure:1;    /*!< Inscure mode, allows the file outside of the root directory */

	/* The input options */
	char*                     path_field;   /*!< The string field for path when we use string field mode */

	/* HTTP error pages */
	options_output_err_page_t http_err_not_found; /*!< The not found error page */
	options_output_err_page_t http_err_moved;     /*!< The moved error page */
	options_output_err_page_t http_err_forbiden;  /*!< The forbiden error page */

	/* HTTP Mime Type Guesser parameter */
	char*                     default_mime_type;  /*!< The default MIME type */
	char*                     mime_map_file;      /*!< The file extension to MIME type mapping file */
	char**                    compressable_types; /*!< The MIME type that can be compressed, NULL terminated */

	/* The Index Page */
	uint32_t                  directory_list_page:1;  /*!< Indicates we are able to generate the directory list when the index.html is missing */
	char**                    index_file_names;       /*!< All the possible index file names, NULL terminated */
} options_t;

#endif