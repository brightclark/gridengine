#ifndef __SGE_PROGNAMES_H
#define __SGE_PROGNAMES_H
/*___INFO__MARK_BEGIN__*/
/*************************************************************************
 * 
 *  The Contents of this file are made available subject to the terms of
 *  the Sun Industry Standards Source License Version 1.2
 * 
 *  Sun Microsystems Inc., March, 2001
 * 
 * 
 *  Sun Industry Standards Source License Version 1.2
 *  =================================================
 *  The contents of this file are subject to the Sun Industry Standards
 *  Source License Version 1.2 (the "License"); You may not use this file
 *  except in compliance with the License. You may obtain a copy of the
 *  License at http://gridengine.sunsource.net/Gridengine_SISSL_license.html
 * 
 *  Software provided under this License is provided on an "AS IS" basis,
 *  WITHOUT WARRANTY OF ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING,
 *  WITHOUT LIMITATION, WARRANTIES THAT THE SOFTWARE IS FREE OF DEFECTS,
 *  MERCHANTABLE, FIT FOR A PARTICULAR PURPOSE, OR NON-INFRINGING.
 *  See the License for the specific provisions governing your rights and
 *  obligations concerning the Software.
 * 
 *   The Initial Developer of the Original Code is: Sun Microsystems, Inc.
 * 
 *   Copyright: 2001 by Sun Microsystems, Inc.
 * 
 *   All Rights Reserved.
 * 
 ************************************************************************/
/*___INFO__MARK_END__*/

#include "basis_types.h"
#include "sge_arch.h"
#include "sge_env.h"

#define SGE_PREFIX      "sge_"
#define SGE_COMMD       "sge_commd"
#define SGE_SHEPHERD    "sge_shepherd"
#define SGE_COSHEPHERD  "sge_coshepherd"
#define SGE_QMASTER     "sge_qmaster"
#define SGE_SHADOWD     "sge_shadowd"
#define PE_HOSTFILE     "pe_hostfile"

/* who - must match prognames[] in prognames.c */

enum {
 QALTER =  1 , 
 QCONF           ,       /* 2  */        
 QDEL            ,       /* 3 */
 QHOLD           ,       /* 4 */
 QMASTER         ,       /* 6 */
 QMOD            ,       /* 7 */
 QRESUB          ,       /* 10 */
 QRLS            ,       /* 11 */
 QSELECT         ,       /* 12 */
 QSH             ,       /* 13 */
 QRSH            ,       /* 14 */
 QLOGIN          ,       /* 15 */
 QSTAT           ,       /* 17 */
 QSUB            ,       /* 18 */
 EXECD           ,       /* 19 */
 QEVENT,                 /* 20 */
 QUSERDEFINED    ,       /* 21 */
 ALL_OPT         ,       /* 22 */

/* programs with numbers > ALL_OPT do not use the old parsing */

 QMON            ,       /* 26 */
 SCHEDD          ,       /* 27 */
 QACCT           ,       /* 29 */
 SHADOWD         ,       /* 32 */
 QHOST           ,       /* 37 */
 SPOOLDEFAULTS   ,       /* 39 */
 JAPI            ,       /* 40 */
 JAPI_EC         ,       /* 41 */
 DRMAA           ,       /* 42 */
 QPING           ,       /* 43 */
 SGE_PASSWD      ,       /* 44 */
 QQUOTA          ,       /* 45 */
 JGDI            ,       /* 46 */
 QTCSH           ,       /* 47 */
 SGE_SHARE_MON           /* 48 */
};

typedef void (*sge_exit_func_t)(void **ctx_ref, int);

typedef struct sge_prog_state_class_str sge_prog_state_class_t; 

struct sge_prog_state_class_str {
   void *sge_prog_state_handle;
   void (*dprintf)(sge_prog_state_class_t *thiz);
   const char* (*get_sge_formal_prog_name)(sge_prog_state_class_t *thiz);
   const char* (*get_qualified_hostname)(sge_prog_state_class_t *thiz);
   const char* (*get_unqualified_hostname)(sge_prog_state_class_t *thiz);
   u_long32 (*get_who)(sge_prog_state_class_t *thiz);
   u_long32 (*get_uid)(sge_prog_state_class_t *thiz);
   u_long32 (*get_gid)(sge_prog_state_class_t *thiz);
   bool (*get_daemonized)(sge_prog_state_class_t *thiz);
   const char* (*get_user_name)(sge_prog_state_class_t *thiz);
   const char* (*get_default_cell)(sge_prog_state_class_t *thiz);
   bool (*get_exit_on_error)(sge_prog_state_class_t *thiz);
   sge_exit_func_t (*get_exit_func)(sge_prog_state_class_t *thiz);

   void (*set_sge_formal_prog_name)(sge_prog_state_class_t *thiz, const char *prog_name);
   void (*set_qualified_hostname)(sge_prog_state_class_t *thiz, const char *qualified_hostname);
   void (*set_unqualified_hostname)(sge_prog_state_class_t *thiz, const char *unqualified_hostname);
   void (*set_who)(sge_prog_state_class_t *thiz, u_long32 who);
   void (*set_uid)(sge_prog_state_class_t *thiz, u_long32 uid);
   void (*set_gid)(sge_prog_state_class_t *thiz, u_long32 gid);
   void (*set_daemonized)(sge_prog_state_class_t *thiz, bool daemonized);
   void (*set_user_name)(sge_prog_state_class_t *thiz, const char* user_name);
   void (*set_default_cell)(sge_prog_state_class_t *thiz, const char* default_cell);
   void (*set_exit_on_error)(sge_prog_state_class_t *thiz, bool exit_on_error);
   void (*set_exit_func)(sge_prog_state_class_t *thiz, sge_exit_func_t exit_func);
};

sge_prog_state_class_t *sge_prog_state_class_create(sge_env_state_class_t *sge_env, u_long32 program_number, sge_error_class_t *eh);
void sge_prog_state_class_destroy(sge_prog_state_class_t **pst);


extern const char *prognames[];

#ifndef GDI_OFF

void sge_getme(u_long32 sge_formal_prog_name);

const char *    uti_state_get_sge_formal_prog_name(void);
const char *    uti_state_get_qualified_hostname(void);
const char *    uti_state_get_unqualified_hostname(void);
u_long32        uti_state_get_mewho(void);
u_long32        uti_state_get_uid(void);
u_long32        uti_state_get_gid(void);
int             uti_state_get_daemonized(void);
const char *    uti_state_get_user_name(void);
const char *    uti_state_get_default_cell(void);
bool            uti_state_get_exit_on_error(void);
sge_exit_func_t uti_state_get_exit_func(void);

void uti_state_set_qualified_hostname(const char *s);
void uti_state_set_daemonized(int daemonized);
void uti_state_set_mewho(u_long32 who);
void uti_state_set_exit_on_error(bool i);
void uti_state_set_exit_func(sge_exit_func_t f);

#endif

#endif /* __SGE_PROGNAMES_H */


