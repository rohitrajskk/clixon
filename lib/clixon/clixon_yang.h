/*
 *
  ***** BEGIN LICENSE BLOCK *****
 
  Copyright (C) 2009-2019 Olof Hagsand and Benny Holmgren

  This file is part of CLIXON.

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

  Alternatively, the contents of this file may be used under the terms of
  the GNU General Public License Version 3 or later (the "GPL"),
  in which case the provisions of the GPL are applicable instead
  of those above. If you wish to allow use of your version of this file only
  under the terms of the GPL, and not to allow others to
  use your version of this file under the terms of Apache License version 2, 
  indicate your decision by deleting the provisions above and replace them with
  the  notice and other provisions required by the GPL. If you do not delete
  the provisions above, a recipient may use your version of this file under
  the terms of any one of the Apache License version 2 or the GPL.

  ***** END LICENSE BLOCK *****

 * Yang functions
 * @see https://tools.ietf.org/html/rfc6020 YANG 1.0
 * @see https://tools.ietf.org/html/rfc7950 YANG 1.1
 */

#ifndef _CLIXON_YANG_H_
#define _CLIXON_YANG_H_


/*
 * Actually cligen variable stuff XXX
 */
#define V_UNIQUE	0x01	/* Variable flag */
#define V_UNSET		0x08	/* Variable is unset, ie no default */

/*
 * Types
 */
struct xml;
/*! YANG keywords from RFC6020.
 * See also keywords generated by yacc/bison in clicon_yang_parse.tab.h, but they start with K_
 * instead of Y_
 * Wanted to unify these (K_ and Y_) but gave up for several reasons:
 * - Dont want to expose a generated yacc file to the API
 * - Cant use the symbols in this file because yacc needs token definitions
 * - Use 0 as no keyword --> therefore start enumeration with 1.
 */
enum rfc_6020{
    Y_ACTION = 1,
    Y_ANYDATA,
    Y_ANYXML,
    Y_ARGUMENT,
    Y_AUGMENT,
    Y_BASE,
    Y_BELONGS_TO,
    Y_BIT,
    Y_CASE,
    Y_CHOICE,
    Y_CONFIG,
    Y_CONTACT,
    Y_CONTAINER,
    Y_DEFAULT,
    Y_DESCRIPTION,
    Y_DEVIATE,
    Y_DEVIATION,
    Y_ENUM,
    Y_ERROR_APP_TAG,
    Y_ERROR_MESSAGE,
    Y_EXTENSION,
    Y_FEATURE,
    Y_FRACTION_DIGITS,
    Y_GROUPING,
    Y_IDENTITY,
    Y_IF_FEATURE,
    Y_IMPORT,
    Y_INCLUDE,
    Y_INPUT,
    Y_KEY,
    Y_LEAF,
    Y_LEAF_LIST,
    Y_LENGTH,
    Y_LIST,
    Y_MANDATORY,
    Y_MAX_ELEMENTS,
    Y_MIN_ELEMENTS,
    Y_MODIFIER,
    Y_MODULE,
    Y_MUST,
    Y_NAMESPACE,
    Y_NOTIFICATION,
    Y_ORDERED_BY,
    Y_ORGANIZATION,
    Y_OUTPUT,
    Y_PATH,
    Y_PATTERN,
    Y_POSITION,
    Y_PREFIX,
    Y_PRESENCE,
    Y_RANGE,
    Y_REFERENCE,
    Y_REFINE,
    Y_REQUIRE_INSTANCE,
    Y_REVISION,
    Y_REVISION_DATE,
    Y_RPC,
    Y_STATUS,
    Y_SUBMODULE,
    Y_TYPE,
    Y_TYPEDEF,
    Y_UNIQUE,
    Y_UNITS,
    Y_UNKNOWN,
    Y_USES,
    Y_VALUE,
    Y_WHEN,
    Y_YANG_VERSION,
    Y_YIN_ELEMENT,
    Y_SPEC  /* XXX: NOTE NOT YANG STATEMENT, reserved for top level spec */
};

/* Type used to group yang nodes used in some functions
 * See RFC7950 Sec 3
 */
enum yang_class{
    YC_NONE,            /* Someting else,... */
    YC_DATANODE,        /* See yang_datanode() */
    YC_DATADEFINITION,  /* See yang_datadefinition() */
    YC_SCHEMANODE       /* See yang_schemanode() */
};
typedef enum yang_class yang_class;

#define YANG_FLAG_MARK 0x01  /* Marker for dynamic algorithms, eg expand */

/* Yang data node 
 * See RFC7950 Sec 3:
 *   o  data node: A node in the schema tree that can be instantiated in a
 *      data tree.  One of container, leaf, leaf-list, list, anydata, and
 *      anyxml.
 */
#define yang_datanode(y) ((y)->ys_keyword == Y_CONTAINER || (y)->ys_keyword == Y_LEAF || (y)->ys_keyword == Y_LIST || (y)->ys_keyword == Y_LEAF_LIST || (y)->ys_keyword == Y_ANYXML)

/* Yang data definition statement
 * See RFC 7950 Sec 3:
 *   o  data definition statement: A statement that defines new data
 *      nodes.  One of "container", "leaf", "leaf-list", "list", "choice",
 *      "case", "augment", "uses", "anydata", and "anyxml".
 */
#define yang_datadefinition(y) (yang_datanode(y) || (y)->ys_keyword == Y_CHOICE || (y)->ys_keyword == Y_CASE || (y)->ys_keyword == Y_AUGMENT || (y)->ys_keyword == Y_USES)

/* Yang schema node .
 * See RFC 7950 Sec 3:
 *    o  schema node: A node in the schema tree.  One of action, container,
 *       leaf, leaf-list, list, choice, case, rpc, input, output,
 *       notification, anydata, and anyxml.
 */
#define yang_schemanode(y) (yang_datanode(y) || (y)->ys_keyword == Y_RPC || (y)->ys_keyword == Y_CHOICE || (y)->ys_keyword == Y_CASE || (y)->ys_keyword == Y_INPUT || (y)->ys_keyword == Y_OUTPUT || (y)->ys_keyword == Y_NOTIFICATION)


typedef struct yang_stmt yang_stmt; /* forward */

/*! Yang type cache. Yang type statements can cache all typedef info here
 * @note unions not cached
*/
struct yang_type_cache{
    int        yc_options;
    cg_var    *yc_mincv;
    cg_var    *yc_maxcv;
    char      *yc_pattern;
    uint8_t    yc_fraction;
    yang_stmt *yc_resolved; /* Resolved type object, can be NULL - note direct ptr */
};
typedef struct yang_type_cache yang_type_cache;

/*! yang statement 
 */
struct yang_stmt{
    int                ys_len;       /* Number of children */
    struct yang_stmt **ys_stmt;      /* Vector of children statement pointers */
    struct yang_node  *ys_parent;    /* Backpointer to parent: yang-stmt or yang-spec */
    enum rfc_6020      ys_keyword;   /* See clicon_yang_parse.tab.h */

    char              *ys_argument;  /* String / argument depending on keyword */   
    int                ys_flags;     /* Flags according to YANG_FLAG_* above */
    /*--------------here common for all -------*/
    char              *ys_extra;     /* For unknown */
    cg_var            *ys_cv;        /* cligen variable. See ys_populate()
					Following stmts have cv:s:
				        leaf: for default value
					leaf-list, 
					config: boolean true or false
					mandatory: boolean true or false
					fraction-digits for fraction-digits
					unknown-stmt (argument)
				     */
    cvec              *ys_cvec;      /* List of stmt-specific variables 
					Y_RANGE: range_min, range_max 
					Y_LIST: vector of keys
					Y_TYPE & identity: store all derived types
				     */
    yang_type_cache   *ys_typecache; /* If ys_keyword==Y_TYPE, cache all typedef data except unions */
};


/*! top-level yang parse-tree */
struct yang_spec{
    int                yp_len;       /* Number of children */
    struct yang_stmt **yp_stmt;      /* Vector of children statement pointers */
    struct yang_node  *yp_parent;    /* Backpointer to parent: always NULL. See yang_stmt */
    enum rfc_6020      yp_keyword;   /* SHOULD BE Y_SPEC */
    char              *yp_argument;  /* XXX String / argument depending on keyword */   
    int                yp_flags;     /* Flags according to YANG_FLAG_* above */
};
typedef struct yang_spec yang_spec;

/*! super-class of yang_stmt and yang_spec: it must start exactly as those two classes */
struct yang_node{
    int                yn_len;       /* Number of children */
    struct yang_stmt **yn_stmt;      /* Vector of children statement pointers */
    struct yang_node  *yn_parent;    /* Backpointer to parent: yang-stmt or yang-spec */
    enum rfc_6020      yn_keyword;   /* See clicon_yang_parse.tab.h */
    char              *yn_argument;  /* XXX String / argument depending on keyword */   
    int                yn_flags;     /* Flags according to YANG_FLAG_* above */
};
typedef struct yang_node yang_node;

typedef int (yang_applyfn_t)(yang_stmt *ys, void *arg);

/*
 * Prototypes
 */
yang_spec *yspec_new(void);
yang_stmt *ys_new(enum rfc_6020 keyw);
int        ys_free(yang_stmt *ys);
int        yspec_free(yang_spec *yspec);
int        ys_cp(yang_stmt *new, yang_stmt *old);
yang_stmt *ys_dup(yang_stmt *old);
int        yn_insert(yang_node *yn_parent, yang_stmt *ys_child);
yang_stmt *yn_each(yang_node *yn, yang_stmt *ys);
char      *yang_key2str(int keyword);
char      *yarg_prefix(yang_stmt *ys);
char      *yarg_id(yang_stmt *ys);
int        ys_module_by_xml(yang_spec *ysp, struct xml *xt, yang_stmt **ymodp);
yang_stmt *ys_module(yang_stmt *ys);
yang_spec *ys_spec(yang_stmt *ys);
yang_stmt *yang_find_module_by_prefix(yang_stmt *ys, char *prefix);
yang_stmt *yang_find_module_by_namespace(yang_spec *yspec, char *namespace);
yang_stmt *yang_find_module_by_name(yang_spec *yspec, char *name);
yang_stmt *yang_find(yang_node *yn, int keyword, const char *argument);
int        yang_match(yang_node *yn, int keyword, char *argument);
yang_stmt *yang_find_datanode(yang_node *yn, char *argument);
yang_stmt *yang_find_schemanode(yang_node *yn, char *argument);
char      *yang_find_myprefix(yang_stmt *ys);
char      *yang_find_mynamespace(yang_stmt *ys);
int        yang_order(yang_stmt *y);
int        yang_print(FILE *f, yang_node *yn);
int        yang_print_cbuf(cbuf *cb, yang_node *yn, int marginal);
int        ys_populate(yang_stmt *ys, void *arg);
yang_stmt *yang_parse_file(int fd, const char *name, yang_spec *ysp);
int        yang_parse(clicon_handle h, const char *filename,
		      const char *module, 
		      const char *revision, yang_spec *ysp);
int        yang_apply(yang_node *yn, enum rfc_6020 key, yang_applyfn_t fn, 
		      void *arg);
int        yang_abs_schema_nodeid(yang_spec *yspec, yang_stmt *ys,
				  char *schema_nodeid, 
				  enum rfc_6020 keyword, yang_stmt **yres);
int        yang_desc_schema_nodeid(yang_node *yn, char *schema_nodeid, 
				   enum rfc_6020 keyword, yang_stmt **yres);
cg_var    *ys_parse(yang_stmt *ys, enum cv_type cvtype);
int        ys_parse_sub(yang_stmt *ys, char *extra);
int        yang_mandatory(yang_stmt *ys);
int        yang_config(yang_stmt *ys);
int        yang_spec_parse_module(clicon_handle h, char *module, char *revision, yang_spec *yspec);
int        yang_spec_parse_file(clicon_handle h, char *filename, yang_spec *yspec);
int        yang_spec_load_dir(clicon_handle h, char *dir, yang_spec *yspec);
cvec      *yang_arg2cvec(yang_stmt *ys, char *delimi);
int        yang_key_match(yang_node *yn, char *name);

#endif  /* _CLIXON_YANG_H_ */
