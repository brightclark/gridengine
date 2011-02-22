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
 *  The Initial Developer of the Original Code is: Sun Microsystems, Inc.
 *
 *  Copyright: 2001 by Sun Microsystems, Inc.
 *
 *  All Rights Reserved.
 *
 ************************************************************************/
/*___INFO__MARK_END__*/

#ifdef SECURE
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <netdb.h>
#include <string.h>
#include <netinet/tcp.h> 
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>


#define ENABLE_CRL

#ifdef LOAD_OPENSSL

#ifdef LINUX
#ifndef __USE_GNU
#define __USE_GNU
#endif /* __USE_GNU */
#endif /* LINUX */


#include <dlfcn.h>
#ifdef LINUX
#ifndef __USE_GNU
#undef __USE_GNU
#endif /* __USE_GNU */
#endif /* LINUX */

#ifdef SOLARIS
#include <link.h>
#endif /* SOLARIS */
#else /* LOAD_OPENSSL */
/*
 * Disable the warning "The variable ... is set but never used."
 * We set a lot of function pointers we don't use currently, but we want
 * to have them ready when we need them.
 */
#if defined (IRIX65)
#pragma set woff 1552 
#endif
#endif /* LOAD_OPENSSL */

#include <openssl/err.h> 
#include <openssl/bio.h>
#include <openssl/ssl.h>
#include <openssl/rand.h>

#ifdef USE_POLL
 #include <sys/poll.h>
#endif

#include "cl_errors.h"
#include "cl_connection_list.h"
#include "cl_fd_list.h"
#include "cl_ssl_framework.h"
#include "cl_communication.h"
#include "cl_commlib.h"
#include "msg_commlib.h"
#include "sge_unistd.h"
#include "uti/sge_string.h"
#include "sge_os.h"

#if (OPENSSL_VERSION_NUMBER < 0x0090700fL) 
#define OPENSSL_CONST
#define NID_userId NID_uniqueIdentifier
#else
#define OPENSSL_CONST const
#endif


#define cl_com_ssl_func__SSL_CTX_set_mode(ctx,op) \
	cl_com_ssl_func__SSL_CTX_ctrl((ctx),SSL_CTRL_MODE,(op),NULL)
#define cl_com_ssl_func__SSL_CTX_get_mode(ctx) \
	cl_com_ssl_func__SSL_CTX_ctrl((ctx),SSL_CTRL_MODE,0,NULL)
#define cl_com_ssl_func__SSL_set_mode(ssl,op) \
	cl_com_ssl_func__SSL_ctrl((ssl),SSL_CTRL_MODE,(op),NULL)
#define cl_com_ssl_func__SSL_get_mode(ssl) \
        cl_com_ssl_func__SSL_ctrl((ssl),SSL_CTRL_MODE,0,NULL)


#define cl_com_ssl_func__SSL_CTX_set_options(ctx,op) \
	cl_com_ssl_func__SSL_CTX_ctrl((ctx),SSL_CTRL_OPTIONS,(op),NULL)
#define cl_com_ssl_func__SSL_CTX_get_options(ctx) \
	cl_com_ssl_func__SSL_CTX_ctrl((ctx),SSL_CTRL_OPTIONS,0,NULL)
#define cl_com_ssl_func__SSL_set_options(ssl,op) \
	cl_com_ssl_func__SSL_ctrl((ssl),SSL_CTRL_OPTIONS,(op),NULL)
#define cl_com_ssl_func__SSL_get_options(ssl) \
        cl_com_ssl_func__SSL_ctrl((ssl),SSL_CTRL_OPTIONS,0,NULL)

/*
 * bugfix for HP and AIX:
 * =====================
 *
 * On some operating systems the open ssl error may return an
 * error when calling ssl functions from a thread. A second call
 * to the same function will not result in SSL_ERROR_SYSCALL ssl error.
 * 
 * Therefore the SSL_ERROR_SYSCALL error is ignored if 
 *    CL_COM_ENABLE_SSL_THREAD_RETRY_BUGFIX
 * is defined. 
 *
 * If there would be a real error the commlib will remove the connection
 * after commlib case specific timeouts.
 *
 */
#if defined(HPUX) || defined(AIX)
#define CL_COM_ENABLE_SSL_THREAD_RETRY_BUGFIX
#endif


/* ssl function wrappers set by dlopen() */
static void                 (*cl_com_ssl_func__CRYPTO_set_id_callback)              (unsigned long (*id_function)(void));
static void                 (*cl_com_ssl_func__CRYPTO_set_locking_callback)         (void (*locking_function)(int mode, int n, const char *file, int line));
static int                  (*cl_com_ssl_func__CRYPTO_num_locks)                    (void);
static unsigned long        (*cl_com_ssl_func__ERR_get_error)                       (void);
static void                 (*cl_com_ssl_func__ERR_error_string_n)                  (unsigned long e, char *buf, size_t len);
static void                 (*cl_com_ssl_func__ERR_free_strings)                    (void);
static void                 (*cl_com_ssl_func__ERR_clear_error)                     (void);
static int                  (*cl_com_ssl_func__BIO_free)                            (BIO *a);
static BIO*                 (*cl_com_ssl_func__BIO_new_fp)                          (FILE *stream, int flags);
static BIO*                 (*cl_com_ssl_func__BIO_new_socket)                      (int sock, int close_flag);
static BIO*                 (*cl_com_ssl_func__BIO_new_mem_buf)                     (void *buf, int len);
static int                  (*cl_com_ssl_func__BIO_printf)                          (BIO *bio, const char *format, ...);
static void                 (*cl_com_ssl_func__SSL_set_bio)                         (SSL *s, BIO *rbio,BIO *wbio);
static int                  (*cl_com_ssl_func__SSL_accept)                          (SSL *ssl);
static void                 (*cl_com_ssl_func__SSL_CTX_free)                        (SSL_CTX *);
static SSL_CTX*             (*cl_com_ssl_func__SSL_CTX_new)                         (SSL_METHOD *meth);
static SSL_METHOD*          (*cl_com_ssl_func__SSLv23_method)                       (void);
static int                  (*cl_com_ssl_func__SSL_CTX_use_certificate_chain_file)  (SSL_CTX *ctx, const char *file);
static int                  (*cl_com_ssl_func__SSL_CTX_use_certificate)             (SSL_CTX *ctx, X509 *cert);
static int                  (*cl_com_ssl_func__SSL_CTX_use_PrivateKey_file)         (SSL_CTX *ctx, const char *file, int type);
static int                  (*cl_com_ssl_func__SSL_CTX_use_PrivateKey)              (SSL_CTX *ctx, EVP_PKEY *pkey);
static int                  (*cl_com_ssl_func__SSL_CTX_load_verify_locations)       (SSL_CTX *ctx, const char *CAfile, const char *CApath);
static int                  (*cl_com_ssl_func__SSL_library_init)                    (void);
static void                 (*cl_com_ssl_func__SSL_load_error_strings)              (void);
static SSL*                 (*cl_com_ssl_func__SSL_new)                             (SSL_CTX *ctx);
static int                  (*cl_com_ssl_func__SSL_connect)                         (SSL *ssl);
static int                  (*cl_com_ssl_func__SSL_shutdown)                        (SSL *s);
static int                  (*cl_com_ssl_func__SSL_clear)                           (SSL *s);
static void                 (*cl_com_ssl_func__SSL_free)                            (SSL *ssl);
static int                  (*cl_com_ssl_func__SSL_get_fd)                          (const SSL *ssl);
static int                  (*cl_com_ssl_func__SSL_get_error)                       (SSL *s,int ret_code);
static long                 (*cl_com_ssl_func__SSL_get_verify_result)               (SSL *ssl);
static X509*                (*cl_com_ssl_func__SSL_get_peer_certificate)            (SSL *s);
static int                  (*cl_com_ssl_func__SSL_write)                           (SSL *ssl,const void *buf,int num);
static int                  (*cl_com_ssl_func__SSL_read)                            (SSL *ssl,void *buf,int num);
static X509_NAME*           (*cl_com_ssl_func__X509_get_subject_name)               (X509 *a);
static int 	                (*cl_com_ssl_func__X509_NAME_get_text_by_NID)           (X509_NAME *name, int nid, char *buf,int len);
static void                 (*cl_com_ssl_func__SSL_CTX_set_verify)                  (SSL_CTX *ctx, int mode, int (*verify_callback)(int, X509_STORE_CTX *));
static X509*                (*cl_com_ssl_func__X509_STORE_CTX_get_current_cert)     (X509_STORE_CTX *ctx);
static int                  (*cl_com_ssl_func__X509_NAME_get_text_by_OBJ)           (X509_NAME *name, ASN1_OBJECT *obj, char *buf,int len);
static ASN1_OBJECT*         (*cl_com_ssl_func__OBJ_nid2obj)                         (int n);
static void                 (*cl_com_ssl_func__X509_free)                           (X509 *a);
static void                 (*cl_com_ssl_func__EVP_PKEY_free)                       (EVP_PKEY *a);
static long                 (*cl_com_ssl_func__SSL_CTX_ctrl)                        (SSL_CTX *ctx, int cmd, long larg, void *parg);
static long                 (*cl_com_ssl_func__SSL_ctrl)                            (SSL *ssl, int cmd, long larg, void *parg);
static int                  (*cl_com_ssl_func__RAND_status)                         (void);
static int                  (*cl_com_ssl_func__RAND_load_file)                      (const char *filename, long max_bytes);
static const char*          (*cl_com_ssl_func__SSL_get_cipher_list)                 (SSL *ssl, int priority);
static int                  (*cl_com_ssl_func__SSL_CTX_set_cipher_list)             (SSL_CTX *,const char *str);
static int                  (*cl_com_ssl_func__SSL_set_cipher_list)                 (SSL *ssl, const char *str);
static void                 (*cl_com_ssl_func__SSL_set_quiet_shutdown)              (SSL *ssl, int mode);
static void *               (*cl_com_ssl_func__PEM_ASN1_read_bio)                   (void *(*d2i)(),const char *name,BIO *bp, void **x, pem_password_cb *cb, void *u);
static X509*                (*cl_com_ssl_func__d2i_X509)                            (X509 **a, const unsigned char **pp, long length);
static PKCS8_PRIV_KEY_INFO *(*cl_com_ssl_func__d2i_PKCS8_PRIV_KEY_INFO)             (PKCS8_PRIV_KEY_INFO **x, const unsigned char **in, long len);
static EVP_PKEY*            (*cl_com_ssl_func__d2i_PrivateKey)                      (int type, EVP_PKEY **a, const unsigned char **pp, long length);
static EVP_PKEY*            (*cl_com_ssl_func__d2i_AutoPrivateKey)                  (EVP_PKEY **a, const unsigned char **pp, long length);
static EVP_PKEY*            (*cl_com_ssl_func__d2i_AutoPrivateKey)                  (EVP_PKEY **a, const unsigned char **pp, long length);
static EVP_PKEY*            (*cl_com_ssl_func__d2i_PKCS8PrivateKey_bio)             (BIO *bp, EVP_PKEY **x, pem_password_cb *cb, void *u);
static EVP_PKEY*            (*cl_com_ssl_func__EVP_PKCS82PKEY)                      (PKCS8_PRIV_KEY_INFO *p8);
static ASN1_VALUE*          (*cl_com_ssl_func__ASN1_item_d2i)                       (ASN1_VALUE **pval, const unsigned char **in, long len, const ASN1_ITEM *it);

#ifdef ENABLE_CRL
static void *               (*cl_com_ssl_func__PEM_ASN1_read)                       (void *(*d2i)(),const char *name,FILE *fp,void **x, pem_password_cb *cb, void *u);
static X509_STORE *         (*cl_com_ssl_func__SSL_CTX_get_cert_store)              (SSL_CTX *ctx);
static int                  (*cl_com_ssl_func__X509_STORE_add_crl)                  (X509_STORE *ctx, X509_CRL *x);
static X509_CRL*            (*cl_com_ssl_func__d2i_X509_CRL)                        (X509_CRL **a, const unsigned char **pp, long length);
static int                  (*cl_com_ssl_func__X509_STORE_set_flags)                (X509_STORE *ctx, unsigned long flags);
static X509*                (*cl_com_ssl_func__X509_STORE_CTX_get_current_cert)     (X509_STORE_CTX *ctx);
static X509_STORE*          (*cl_com_ssl_func__X509_STORE_new)                      (void);
static X509_NAME*           (*cl_com_ssl_func__X509_get_subject_name)               (X509 *a);
static X509_NAME*           (*cl_com_ssl_func__X509_get_issuer_name)                (X509 *a);
static X509_LOOKUP*         (*cl_com_ssl_func__X509_STORE_add_lookup)               (X509_STORE *v, X509_LOOKUP_METHOD *m);
static int                  (*cl_com_ssl_func__X509_load_crl_file)                  (X509_LOOKUP *ctx, const char *file, int type);
static X509_STORE_CTX*      (*cl_com_ssl_func__X509_STORE_CTX_new)                  (void);
static int                  (*cl_com_ssl_func__X509_STORE_CTX_init)                 (X509_STORE_CTX *ctx, X509_STORE *store, X509 *x509, STACK_OF(X509) *chain);
static void                 (*cl_com_ssl_func__X509_STORE_CTX_cleanup)              (X509_STORE_CTX *ctx);
static int                  (*cl_com_ssl_func__X509_verify_cert)                    (X509_STORE_CTX *ctx);
static int                  (*cl_com_ssl_func__X509_STORE_CTX_get_error)            (X509_STORE_CTX *ctx);
static void                 (*cl_com_ssl_func__ERR_print_errors_fp)                 (FILE *fp);

static X509_LOOKUP_METHOD*  (*cl_com_ssl_func__X509_LOOKUP_file)                    (void);
static void*                (*cl_com_ssl_func__X509_STORE_CTX_get_ex_data)          (X509_STORE_CTX *ctx,int idx);
static SSL_CTX*             (*cl_com_ssl_func__SSL_get_SSL_CTX)                     (SSL *ssl);
static int                  (*cl_com_ssl_func__X509_STORE_CTX_get_error_depth)      (X509_STORE_CTX *ctx);
static char*                (*cl_com_ssl_func__X509_NAME_oneline)                   (X509_NAME *a,char *buf,int size);
static void                 (*cl_com_ssl_func__CRYPTO_free)                         (void *);
static const char*          (*cl_com_ssl_func__X509_verify_cert_error_string)       (long n);
static int                  (*cl_com_ssl_func__SSL_get_ex_data_X509_STORE_CTX_idx)  (void);
static void*                (*cl_com_ssl_func__SSL_CTX_get_ex_data)                 (SSL_CTX *ssl,int idx);
static int                  (*cl_com_ssl_func__SSL_CTX_set_ex_data)                 (SSL_CTX *ssl,int idx,void *data);
static int                  (*cl_com_ssl_func__sk_num)                              (const STACK *);
static char*                (*cl_com_ssl_func__sk_value)                            (const STACK *, int);
static int                  (*cl_com_ssl_func__X509_STORE_get_by_subject)           (X509_STORE_CTX *vs,int type,X509_NAME *name, X509_OBJECT *ret);
static void                 (*cl_com_ssl_func__EVP_PKEY_free)                       (EVP_PKEY *pkey);
static void                 (*cl_com_ssl_func__X509_STORE_CTX_set_error)            (X509_STORE_CTX *ctx,int s);
static void                 (*cl_com_ssl_func__X509_OBJECT_free_contents)           (X509_OBJECT *a);
static ASN1_INTEGER*        (*cl_com_ssl_func__X509_get_serialNumber)               (X509 *x);
static int                  (*cl_com_ssl_func__X509_cmp_current_time)               (ASN1_TIME *s);
static int                  (*cl_com_ssl_func__ASN1_INTEGER_cmp)                    (ASN1_INTEGER *x, ASN1_INTEGER *y);
static long                 (*cl_com_ssl_func__ASN1_INTEGER_get)                    (ASN1_INTEGER *a);
static int                  (*cl_com_ssl_func__X509_CRL_verify)                     (X509_CRL *a, EVP_PKEY *r);
static EVP_PKEY*            (*cl_com_ssl_func__X509_get_pubkey)                     (X509 *x);
static int                  (*cl_com_ssl_func__X509_STORE_set_default_paths)        (X509_STORE *ctx);
static int                  (*cl_com_ssl_func__X509_STORE_load_locations)           (X509_STORE *ctx, const char *file, const char *dir);
static void                 (*cl_com_ssl_func__X509_STORE_free)                     (X509_STORE *v);

#define cl_com_ssl_func__SSL_CTX_set_app_data(ctx,arg)      (cl_com_ssl_func__SSL_CTX_set_ex_data(ctx,0,(char *)arg))
#define cl_com_ssl_func__SSL_CTX_get_app_data(ctx)  (cl_com_ssl_func__SSL_CTX_get_ex_data(ctx,0))
#define cl_com_ssl_func__OPENSSL_free(addr)   cl_com_ssl_func__CRYPTO_free(addr)
#define cl_com_ssl_func__SSL_CTX_get_app_data(ctx)  (cl_com_ssl_func__SSL_CTX_get_ex_data(ctx,0))
#define cl_com_ssl_func__PEM_read_X509_CRL(fp,x,cb,u) (X509_CRL *)cl_com_ssl_func__PEM_ASN1_read( \
   (char *(*)())cl_com_ssl_func__d2i_X509_CRL,PEM_STRING_X509_CRL,fp,(char **)x,cb,u)
#define cl_com_ssl_func__X509_CRL_get_nextUpdate(x) ((x)->crl->nextUpdate)
#define cl_com_ssl_func__X509_CRL_get_REVOKED(x) ((x)->crl->revoked)
#define cl_com_ssl_func__X509_STORE_set_verify_cb_func(ctx,func) ((ctx)->verify_cb=(func))

#endif

#define  cl_com_ssl_func__PEM_read_bio_X509(bp,x,cb,u) (X509 *)cl_com_ssl_func__PEM_ASN1_read_bio( \
   (void *(*)())cl_com_ssl_func__d2i_X509,PEM_STRING_X509,bp,(void **)x,cb,u)
#define  cl_com_ssl_func__PEM_read_bio_PrivateKey(bp,x,cb,u) (EVP_PKEY *)cl_com_ssl_func__PEM_ASN1_read_bio( \
   (void *(*)())cl_com_ssl_func__d2i_AutoPrivateKey,PEM_STRING_EVP_PKEY,bp,(void **)x,cb,u)

static PKCS8_PRIV_KEY_INFO* cl_com_ssl_func__PEM_read_bio_PKCS8_PRIV_KEY_INFO(BIO *bp, PKCS8_PRIV_KEY_INFO **x, pem_password_cb *cb, void *u)
{ 
   return((PKCS8_PRIV_KEY_INFO *)cl_com_ssl_func__PEM_ASN1_read_bio((void *(*)())cl_com_ssl_func__d2i_PKCS8_PRIV_KEY_INFO, PEM_STRING_PKCS8INF, 
            bp, (void **)x,cb,u));
}



/* 
 *   connection specific struct (not used from outside) 
 *   ==================================================
 *
 *   This structure is setup in cl_com_ssl_setup_connection() and
 *   freed with cl_com_ssl_free_com_private(). A pointer to the 
 *   malloced structure can be obtained with cl_com_ssl_get_private()
 */
typedef struct cl_ssl_verify_crl_data_type {
   time_t last_modified;
   X509_STORE *store;
} cl_ssl_verify_crl_data_t;

typedef struct cl_com_ssl_private_type {
   /* TCP/IP specific */
   int                server_port;         /* used port for server setup */
   int                connect_port;        /* port to connect to */
   int                connect_in_port;     /* port from where client is connected (used for reserved port check) */
   int                sockfd;              /* socket file descriptor */
   int                pre_sockfd;          /* socket which was prepared for later listen call (only_prepare_service == TRUE */
   struct sockaddr_in client_addr;         /* used in connect for storing client addr of connection partner */ 

   /* SSL specific */
   int                ssl_last_error;      /* last error value from SSL_get_error() */
   SSL_CTX*           ssl_ctx;             /* create with SSL_CTX_new() , free with SSL_CTX_free() */
   SSL*               ssl_obj;             /* ssl object for the connection */
   BIO*               ssl_bio_socket;      /* bio socket for the connection */ 
   cl_ssl_setup_t*    ssl_setup;           /* ssl setup structure */

   char*              ssl_unique_id;       /* uniqueIdentifier for this connection */
   cl_ssl_verify_crl_data_t* ssl_crl_data; /* contains crl specific data configuration */
} cl_com_ssl_private_t;

/* 
 *   global ssl struct (not used from outside) 
 *   =========================================
 */
typedef struct cl_com_ssl_global_type {

/* 
 * global init bool  
 */
   cl_bool_t          ssl_initialized;


/* 
 * global mutex array for ssl thread lock initialization 
 *
 * only modify when cl_com_ssl_global_config_mutex is locked 
 */
   pthread_mutex_t*   ssl_lib_lock_mutex_array; /* ssl lib lock array */
   int                ssl_lib_lock_num;   /* nr of ssl lib lock mutexes */

} cl_com_ssl_global_t;


/* global ssl configuration setup mutex */
static pthread_mutex_t cl_com_ssl_global_config_mutex = PTHREAD_MUTEX_INITIALIZER;
static cl_com_ssl_global_t* cl_com_ssl_global_config_object = NULL;

/* here we load the SSL functions via dlopen */
static pthread_mutex_t cl_com_ssl_crypto_handle_mutex = PTHREAD_MUTEX_INITIALIZER;
#ifdef LOAD_OPENSSL
static void* cl_com_ssl_crypto_handle = NULL;
#endif


/* static function declarations */
static cl_com_ssl_private_t* cl_com_ssl_get_private(cl_com_connection_t* connection);
static int                   cl_com_ssl_free_com_private(cl_com_connection_t* connection);
static int                   cl_com_ssl_setup_context(cl_com_connection_t* connection, cl_bool_t is_server);
static int                   cl_com_ssl_transform_ssl_error(unsigned long ssl_error, char* buffer, unsigned long buflen, char** transformed_error);
static int                   cl_com_ssl_log_ssl_errors(const char* function_name);
static const char*           cl_com_ssl_get_error_text(int ssl_error);

static int                   cl_com_ssl_build_symbol_table(void);
static int                   cl_com_ssl_destroy_symbol_table(void);

static unsigned long         cl_com_ssl_get_thread_id(void);
static void                  cl_com_ssl_locking_callback(int mode, int type, const char *file, int line);

static int                   cl_com_ssl_verify_callback(int preverify_ok, X509_STORE_CTX *ctx); /* callback for verify clients certificate */
static int                   cl_com_ssl_set_default_mode(SSL_CTX *ctx, SSL *ssl);
static void                  cl_com_ssl_log_mode_settings(long mode);
static int                   cl_com_ssl_fill_private_from_peer_cert(cl_com_ssl_private_t *private, cl_bool_t is_server);
static int cl_com_ssl_connection_request_handler_setup_finalize(cl_com_connection_t* connection);


#ifdef __CL_FUNCTION__
#undef __CL_FUNCTION__
#endif
#define __CL_FUNCTION__ "cl_com_ssl_log_mode_settings()"
static void cl_com_ssl_log_mode_settings(long mode) {
   if (mode & SSL_MODE_ENABLE_PARTIAL_WRITE) {
      CL_LOG(CL_LOG_INFO,"SSL_MODE_ENABLE_PARTIAL_WRITE:       on");
   } else {
      CL_LOG(CL_LOG_INFO,"SSL_MODE_ENABLE_PARTIAL_WRITE:       off");
   }

   if (mode & SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER) {
      CL_LOG(CL_LOG_INFO,"SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER: on");
   } else {
      CL_LOG(CL_LOG_INFO,"SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER: off");
   }

   if (mode & SSL_MODE_AUTO_RETRY) {
      CL_LOG(CL_LOG_INFO,"SSL_MODE_AUTO_RETRY:                 on");
   } else {
      CL_LOG(CL_LOG_INFO,"SSL_MODE_AUTO_RETRY:                 off");
   }
}

#ifdef __CL_FUNCTION__
#undef __CL_FUNCTION__
#endif
#define __CL_FUNCTION__ "cl_com_ssl_log_option_settings()"
static void cl_com_ssl_log_option_settings(long mode) {
   if (mode & SSL_OP_MICROSOFT_SESS_ID_BUG) {
      CL_LOG(CL_LOG_INFO,"SSL_OP_MICROSOFT_SESS_ID_BUG:                  on");
   } else {
      CL_LOG(CL_LOG_INFO,"SSL_OP_MICROSOFT_SESS_ID_BUG:                  off");
   }
   if (mode & SSL_OP_NETSCAPE_CHALLENGE_BUG) {
      CL_LOG(CL_LOG_INFO,"SSL_OP_NETSCAPE_CHALLENGE_BUG:                 on");
   } else {
      CL_LOG(CL_LOG_INFO,"SSL_OP_NETSCAPE_CHALLENGE_BUG:                 off");
   }
   if (mode & SSL_OP_NETSCAPE_REUSE_CIPHER_CHANGE_BUG) {
      CL_LOG(CL_LOG_INFO,"SSL_OP_NETSCAPE_REUSE_CIPHER_CHANGE_BUG:       on");
   } else {
      CL_LOG(CL_LOG_INFO,"SSL_OP_NETSCAPE_REUSE_CIPHER_CHANGE_BUG:       off");
   }
   if (mode & SSL_OP_SSLREF2_REUSE_CERT_TYPE_BUG) {
      CL_LOG(CL_LOG_INFO,"SSL_OP_SSLREF2_REUSE_CERT_TYPE_BUG:            on");
   } else {
      CL_LOG(CL_LOG_INFO,"SSL_OP_SSLREF2_REUSE_CERT_TYPE_BUG:            off");
   }
   if (mode & SSL_OP_MICROSOFT_BIG_SSLV3_BUFFER) {
      CL_LOG(CL_LOG_INFO,"SSL_OP_MICROSOFT_BIG_SSLV3_BUFFER:             on");
   } else {
      CL_LOG(CL_LOG_INFO,"SSL_OP_MICROSOFT_BIG_SSLV3_BUFFER:             off");
   }
   if (mode & SSL_OP_MSIE_SSLV2_RSA_PADDING) {
      CL_LOG(CL_LOG_INFO,"SSL_OP_MSIE_SSLV2_RSA_PADDING:                 on");
   } else {
      CL_LOG(CL_LOG_INFO,"SSL_OP_MSIE_SSLV2_RSA_PADDING:                 off");
   }
   if (mode & SSL_OP_SSLEAY_080_CLIENT_DH_BUG) {
      CL_LOG(CL_LOG_INFO,"SSL_OP_SSLEAY_080_CLIENT_DH_BUG:               on");
   } else {
      CL_LOG(CL_LOG_INFO,"SSL_OP_SSLEAY_080_CLIENT_DH_BUG:               off");
   }
   if (mode & SSL_OP_TLS_D5_BUG) {
      CL_LOG(CL_LOG_INFO,"SSL_OP_TLS_D5_BUG:                             on");
   } else {
      CL_LOG(CL_LOG_INFO,"SSL_OP_TLS_D5_BUG:                             off");
   }
   if (mode & SSL_OP_TLS_BLOCK_PADDING_BUG) {
      CL_LOG(CL_LOG_INFO,"SSL_OP_TLS_BLOCK_PADDING_BUG:                  on");
   } else {
      CL_LOG(CL_LOG_INFO,"SSL_OP_TLS_BLOCK_PADDING_BUG:                  off");
   }
   if (mode & SSL_OP_DONT_INSERT_EMPTY_FRAGMENTS) {
      CL_LOG(CL_LOG_INFO,"SSL_OP_DONT_INSERT_EMPTY_FRAGMENTS:            on");
   } else {
      CL_LOG(CL_LOG_INFO,"SSL_OP_DONT_INSERT_EMPTY_FRAGMENTS:            off");
   }
   if (mode & SSL_OP_ALL) {
      CL_LOG(CL_LOG_INFO,"SSL_OP_ALL:                                    on");
   } else {
      CL_LOG(CL_LOG_INFO,"SSL_OP_ALL:                                    off");
   }
   if (mode & SSL_OP_TLS_ROLLBACK_BUG) {
      CL_LOG(CL_LOG_INFO,"SSL_OP_TLS_ROLLBACK_BUG:                       on");
   } else {
      CL_LOG(CL_LOG_INFO,"SSL_OP_TLS_ROLLBACK_BUG:                       off");
   }
   if (mode & SSL_OP_SINGLE_DH_USE) {
      CL_LOG(CL_LOG_INFO,"SSL_OP_SINGLE_DH_USE:                          on");
   } else {
      CL_LOG(CL_LOG_INFO,"SSL_OP_SINGLE_DH_USE:                          off");
   }
   if (mode & SSL_OP_EPHEMERAL_RSA) {
      CL_LOG(CL_LOG_INFO,"SSL_OP_EPHEMERAL_RSA:                          on");
   } else {
      CL_LOG(CL_LOG_INFO,"SSL_OP_EPHEMERAL_RSA:                          off");
   }
   if (mode & SSL_OP_CIPHER_SERVER_PREFERENCE) {
      CL_LOG(CL_LOG_INFO,"SSL_OP_CIPHER_SERVER_PREFERENCE:               on");
   } else {
      CL_LOG(CL_LOG_INFO,"SSL_OP_CIPHER_SERVER_PREFERENCE:               off");
   }
   if (mode & SSL_OP_PKCS1_CHECK_1) {
      CL_LOG(CL_LOG_INFO,"SSL_OP_PKCS1_CHECK_1:                          on");
   } else {
      CL_LOG(CL_LOG_INFO,"SSL_OP_PKCS1_CHECK_1:                          off");
   }
   if (mode & SSL_OP_PKCS1_CHECK_2) {
      CL_LOG(CL_LOG_INFO,"SSL_OP_PKCS1_CHECK_2:                          on");
   } else {
      CL_LOG(CL_LOG_INFO,"SSL_OP_PKCS1_CHECK_2:                          off");
   }
   if (mode & SSL_OP_NETSCAPE_CA_DN_BUG) { 
      CL_LOG(CL_LOG_INFO,"SSL_OP_NETSCAPE_CA_DN_BUG:                     on");
   } else {
      CL_LOG(CL_LOG_INFO,"SSL_OP_NETSCAPE_CA_DN_BUG:                     off");
   }
   if (mode & SSL_OP_NETSCAPE_DEMO_CIPHER_CHANGE_BUG) {
      CL_LOG(CL_LOG_INFO,"SSL_OP_NETSCAPE_DEMO_CIPHER_CHANGE_BUG:        on");
   } else {
      CL_LOG(CL_LOG_INFO,"SSL_OP_NETSCAPE_DEMO_CIPHER_CHANGE_BUG:        off");
   }
   if (mode & SSL_OP_NO_SSLv2) {
      CL_LOG(CL_LOG_INFO,"SSL_OP_NO_SSLv2:                               on");
   } else {
      CL_LOG(CL_LOG_INFO,"SSL_OP_NO_SSLv2:                               off");
   }
   if (mode & SSL_OP_NO_SSLv3) {
      CL_LOG(CL_LOG_INFO,"SSL_OP_NO_SSLv3:                               on");
   } else {
      CL_LOG(CL_LOG_INFO,"SSL_OP_NO_SSLv3:                               off");
   }
   if (mode & SSL_OP_NO_TLSv1) {
      CL_LOG(CL_LOG_INFO,"SSL_OP_NO_TLSv1:                               on");
   } else {
      CL_LOG(CL_LOG_INFO,"SSL_OP_NO_TLSv1:                               off");
   }
   if (mode & SSL_OP_NO_SESSION_RESUMPTION_ON_RENEGOTIATION) {
      CL_LOG(CL_LOG_INFO,"SSL_OP_NO_SESSION_RESUMPTION_ON_RENEGOTIATION: on");
   } else {
      CL_LOG(CL_LOG_INFO,"SSL_OP_NO_SESSION_RESUMPTION_ON_RENEGOTIATION: off");
   }
}


#ifdef __CL_FUNCTION__
#undef __CL_FUNCTION__
#endif
#define __CL_FUNCTION__ "cl_com_ssl_set_default_mode()"
static int cl_com_ssl_set_default_mode(SSL_CTX *ctx, SSL *ssl) {

   /* 
    * see man page for SSL_CTX_set_mode() for mode settings 
    */
   long ctx_actual_mode;
   long ssl_actual_mode;
   long commlib_mode = SSL_MODE_ENABLE_PARTIAL_WRITE;

   /* 
    * see man SSL_CTX_set_options for option settings 
    */
   long ctx_actual_options;
   long ssl_actual_options;
   long commlib_options = 0; /* SSL_OP_NO_TLSv1; */

   /* 
    * see: http://www.openssl.org/docs/apps/ciphers.html#
    * test this cipher string with openssl ciphers -v "RC4-MD5:NULL-MD5" command 
    */
   const char* commlib_ciphers_string = "RC4-MD5:NULL-MD5"; /* "RC4-MD5:NULL-MD5"; */ /* or "DEFAULT" */

   if (ctx != NULL) {
      CL_LOG(CL_LOG_INFO,"setting CTX object defaults");      

      /* 
       * STEP 1: set cipher list 
       */
      CL_LOG_STR(CL_LOG_INFO,"setting cipher list:", commlib_ciphers_string);
      if ( cl_com_ssl_func__SSL_CTX_set_cipher_list(ctx, commlib_ciphers_string) != 1) {
         CL_LOG_STR(CL_LOG_ERROR,"could not set ctx cipher list:", commlib_ciphers_string);
         cl_commlib_push_application_error(CL_LOG_ERROR, CL_RETVAL_ERROR_SETTING_CIPHER_LIST, commlib_ciphers_string);
         return CL_RETVAL_ERROR_SETTING_CIPHER_LIST;
      }


      /* 
       * STEP 2: set mode 
       */
      CL_LOG(CL_LOG_INFO,"getting default modes");
      ctx_actual_mode = cl_com_ssl_func__SSL_CTX_get_mode(ctx);
      cl_com_ssl_log_mode_settings(ctx_actual_mode);

      if (ctx_actual_mode != commlib_mode) {
         /* set commlib modes if not equal to actual mode */
         ctx_actual_mode = commlib_mode;
         cl_com_ssl_func__SSL_CTX_set_mode(ctx,ctx_actual_mode);

         CL_LOG(CL_LOG_INFO,"setting commlib modes");
         ctx_actual_mode = cl_com_ssl_func__SSL_CTX_get_mode(ctx);
         cl_com_ssl_log_mode_settings(ctx_actual_mode);
      }


      /*
       * STEP 3: set options 
       */
      CL_LOG(CL_LOG_INFO,"getting default options");
      ctx_actual_options = cl_com_ssl_func__SSL_CTX_get_options(ctx);
      cl_com_ssl_log_option_settings(ctx_actual_options);

      if (ctx_actual_options != commlib_options) {
         /* setting commlib options */
         ctx_actual_options = commlib_options;
         cl_com_ssl_func__SSL_CTX_set_options(ctx,ctx_actual_options);

         /* print the options again */
         CL_LOG(CL_LOG_INFO,"setting commlib options");
         ctx_actual_options = cl_com_ssl_func__SSL_CTX_get_options(ctx);
         cl_com_ssl_log_option_settings(ctx_actual_options);
      }
   }

   if (ssl != NULL) {
      const char* helper_str = NULL;
      int prio = 0;

      CL_LOG(CL_LOG_INFO,"setting SSL object defaults");      

      /* 
       * STEP 1: set cipher list 
       */
      if ( cl_com_ssl_func__SSL_set_cipher_list(ssl, commlib_ciphers_string) != 1) {
         CL_LOG_STR(CL_LOG_ERROR,"could not set ssl cipher list:", commlib_ciphers_string);
         cl_commlib_push_application_error(CL_LOG_ERROR, CL_RETVAL_ERROR_SETTING_CIPHER_LIST, commlib_ciphers_string);
         return CL_RETVAL_ERROR_SETTING_CIPHER_LIST;
      }

      /* 
       * STEP 2: set mode 
       */
      CL_LOG(CL_LOG_INFO,"getting default modes");
      ssl_actual_mode = cl_com_ssl_func__SSL_get_mode(ssl);
      cl_com_ssl_log_mode_settings(ssl_actual_mode);

      if (ssl_actual_mode != commlib_mode) {
         ssl_actual_mode = commlib_mode;
         cl_com_ssl_func__SSL_set_mode(ssl,ssl_actual_mode);

         CL_LOG(CL_LOG_INFO,"setting commlib modes");
         ssl_actual_mode = cl_com_ssl_func__SSL_get_mode(ssl);
         cl_com_ssl_log_mode_settings(ssl_actual_mode);
      }

      /*
       * STEP 3: set options 
       */
      CL_LOG(CL_LOG_INFO,"getting default options");
      ssl_actual_options = cl_com_ssl_func__SSL_get_options(ssl);
      cl_com_ssl_log_option_settings(ssl_actual_options);
      
      if (ssl_actual_options != commlib_options) {
         /* setting commlib options */
         ssl_actual_options = commlib_options;
         cl_com_ssl_func__SSL_set_options(ssl,ssl_actual_options);

         /* print the options again */
         CL_LOG(CL_LOG_INFO,"setting commlib options");
         ssl_actual_options = cl_com_ssl_func__SSL_get_options(ssl);
         cl_com_ssl_log_option_settings(ssl_actual_options);
      }
 
      /*
       * Show cipher list
       */
      CL_LOG(CL_LOG_INFO,"supported cipher priority list:");
      while ( (helper_str = cl_com_ssl_func__SSL_get_cipher_list(ssl, prio)) != NULL) {
         CL_LOG(CL_LOG_INFO, helper_str );
         prio++;
      }
   }

   return CL_RETVAL_OK;
}

#ifdef ENABLE_CRL
static int ssl_callback_SSLVerify_CRL(int ok, X509_STORE_CTX *ctx, cl_com_ssl_private_t* private) {
   X509 *cert = NULL;
   X509_LOOKUP *lookup = NULL;
   X509_STORE_CTX verify_ctx;
   int err;
   int is_ok = true; 
   SGE_STRUCT_STAT stat_buffer;
   
   if (private == NULL || private->ssl_setup == NULL || private->ssl_crl_data == NULL) {
      CL_LOG(CL_LOG_INFO,"no crl checking");
      return true;
   }

   if (private->ssl_setup->ssl_crl_file == NULL || SGE_STAT(private->ssl_setup->ssl_crl_file, &stat_buffer)) {
      CL_LOG(CL_LOG_INFO,"no crl checking");
      return true;
   }   

   /* create the cert store and set the verify callback */
   if (private->ssl_crl_data->store == NULL || stat_buffer.st_mtime != private->ssl_crl_data->last_modified) {
       CL_LOG(CL_LOG_WARNING, "creating new crl store context");
       private->ssl_crl_data->last_modified=stat_buffer.st_mtime;
       if (private->ssl_crl_data->store != NULL) {
           cl_com_ssl_func__X509_STORE_free(private->ssl_crl_data->store);
           private->ssl_crl_data->store=NULL;
       }

       if (!(private->ssl_crl_data->store=cl_com_ssl_func__X509_STORE_new())) {
          CL_LOG(CL_LOG_ERROR,"Error creating X509_STORE_CTX object");
          is_ok = false;
       }   

       if (is_ok == true) {
          cl_com_ssl_func__X509_STORE_set_flags(private->ssl_crl_data->store, X509_V_FLAG_CRL_CHECK | X509_V_FLAG_CRL_CHECK_ALL);
       }
       if (is_ok == true && (cl_com_ssl_func__X509_STORE_load_locations(private->ssl_crl_data->store, private->ssl_setup->ssl_CA_cert_pem_file, NULL) != 1)) {
          CL_LOG(CL_LOG_ERROR, "Error loading the CA file or directory");
          is_ok = false;
       }   
       if (is_ok == true && (cl_com_ssl_func__X509_STORE_set_default_paths(private->ssl_crl_data->store) != 1)) {
          CL_LOG(CL_LOG_ERROR, "Error loading the system-wide CA certificates");
          is_ok = false;
       }   
       if (is_ok == true && (!(lookup = cl_com_ssl_func__X509_STORE_add_lookup(private->ssl_crl_data->store, cl_com_ssl_func__X509_LOOKUP_file())))) {
          CL_LOG(CL_LOG_ERROR, "Error creating X509_LOOKUP object");
          is_ok = false;
       }    
       if (is_ok == true && (cl_com_ssl_func__X509_load_crl_file(lookup, private->ssl_setup->ssl_crl_file, X509_FILETYPE_PEM) != 1)) {
          CL_LOG(CL_LOG_ERROR, "Error reading the CRL file");
          is_ok = false;
       }    

       /* free store on in error case */
       if (is_ok == false && private->ssl_crl_data->store != NULL) {
          cl_com_ssl_func__X509_STORE_free(private->ssl_crl_data->store);
          private->ssl_crl_data->store=NULL;
       }
   } else {
      CL_LOG(CL_LOG_WARNING, "using old crl store context");
   }

   cert = cl_com_ssl_func__X509_STORE_CTX_get_current_cert(ctx);
   if (is_ok == true && cert != NULL) {
       /* X509_STORE_CTX_init did not return an error condition in prior versions */
       if (cl_com_ssl_func__X509_STORE_CTX_init(&verify_ctx, private->ssl_crl_data->store, cert, NULL) != 1) {
          CL_LOG(CL_LOG_ERROR, "Error initializing verification context");
          is_ok = false;
       } else {
          /* verify the certificate */
          if (cl_com_ssl_func__X509_verify_cert(&verify_ctx) != 1) {
             is_ok = false;
          }
       }
       if (is_ok == false) {
           err = cl_com_ssl_func__X509_STORE_CTX_get_error(&verify_ctx);
           cl_com_ssl_func__X509_STORE_CTX_set_error(ctx, err);
       }
       cl_com_ssl_func__X509_STORE_CTX_cleanup(&verify_ctx);
   } else {
      if (is_ok == false) {
         CL_LOG(CL_LOG_ERROR,"X509 store is not valid");
      }
      if (cert == NULL) {
         CL_LOG(CL_LOG_ERROR,"cert is NULL");
      }
      is_ok = false;
   }

   return is_ok;
}

#endif /* end #ifdef ENABLE_CRL */



#ifdef __CL_FUNCTION__
#undef __CL_FUNCTION__
#endif
#define __CL_FUNCTION__ "cl_com_ssl_verify_callback()"
static int cl_com_ssl_verify_callback(int preverify_ok, X509_STORE_CTX *ctx) {
   int    is_ok = 0;
#if 0   
   X509*  xs = NULL;
   int errdepth = 0;
   char *cp = NULL;
   char *cp2 = NULL;
   char *crl_file = NULL;
#endif   
   int errnum = 0;
   SSL *ssl = NULL;
   SSL_CTX *ssl_ctx = NULL;
   cl_com_ssl_private_t* ssl_private_setup = NULL;

   if (preverify_ok != 1) {
      return preverify_ok;
   }

   /* get pointer to commlib private data struct from ssl ctx */
   ssl = cl_com_ssl_func__X509_STORE_CTX_get_ex_data(ctx, cl_com_ssl_func__SSL_get_ex_data_X509_STORE_CTX_idx());
   ssl_ctx = cl_com_ssl_func__SSL_get_SSL_CTX(ssl);
   ssl_private_setup = (cl_com_ssl_private_t*) cl_com_ssl_func__SSL_CTX_get_app_data(ssl_ctx);

   if (ssl_private_setup == NULL) {
      return is_ok;
   }   

#if 0   
   printf("crl_file is: %s\n", ssl_setup->ssl_crl_file);   
   printf("ca_cert_file is: %s\n", ssl_setup->ssl_CA_cert_pem_file);   
   xs = cl_com_ssl_func__X509_STORE_CTX_get_current_cert(ctx);
   errnum = cl_com_ssl_func__X509_STORE_CTX_get_error(ctx);
   errdepth = cl_com_ssl_func__X509_STORE_CTX_get_error_depth(ctx);

   /*
    * Log verification information
    */
   cp  = cl_com_ssl_func__X509_NAME_oneline(cl_com_ssl_func__X509_get_subject_name(xs), NULL, 0);
   cp2 = cl_com_ssl_func__X509_NAME_oneline(cl_com_ssl_func__X509_get_issuer_name(xs),  NULL, 0);
   
   printf("Certificate Verification: depth: %d, subject: %s, issuer: %s\n",
            errdepth, cp != NULL ? cp : "-unknown-",
            cp2 != NULL ? cp2 : "-unknown");
   if (cp)
      cl_com_ssl_func__OPENSSL_free(cp);
   if (cp2)
      cl_com_ssl_func__OPENSSL_free(cp2);
#endif      

   /*
    * Additionally perform CRL-based revocation checks
    */
   is_ok = ssl_callback_SSLVerify_CRL(is_ok, ctx, ssl_private_setup);
   if (!is_ok) {
      /*
       * If we already know it's not ok, log the real reason
       */
      char buf[2048];
      errnum = cl_com_ssl_func__X509_STORE_CTX_get_error(ctx);
      snprintf(buf, sizeof(buf)-1, "Certificate Verification: Error (%d): %s\n",
               errnum, cl_com_ssl_func__X509_verify_cert_error_string(errnum));
      CL_LOG(CL_LOG_ERROR, buf);

      /* TODO: (CR) push application error, the CL_LOG function only logs it to commlib
                    debug buffer */
   }   

   return is_ok;
}

#ifdef __CL_FUNCTION__
#undef __CL_FUNCTION__
#endif
#define __CL_FUNCTION__ "cl_com_ssl_locking_callback()"
static void cl_com_ssl_locking_callback(int mode, int type, const char *file, int line) {
#if 0
   char tmp_buffer[1024];
#endif
   const char* tmp_filename = "n.a.";

   /* 
    * locking cl_com_ssl_global_config_mutex would cause a deadlock
    * because it is locked when setting the callback function with
    * cl_com_ssl_func__CRYPTO_set_locking_callback(). Since 
    * cl_com_ssl_func__CRYPTO_set_locking_callback() is called after
    * malloc of the array and malloc of the cl_com_ssl_global_config_object
    * it is not necessary to lock this array.
    * At cleanup the ssl_library is shutdown before deleting the 
    * cl_com_ssl_global_config_object.
    */

   if (file != NULL) {
      tmp_filename = file;
   }
   if (cl_com_ssl_global_config_object != NULL) {
      if (mode & CRYPTO_LOCK) {
#if 0         
         snprintf(tmp_buffer,1024,"locking ssl object:   %d, file: %s, line: %d", 
                  type, tmp_filename, line);
         CL_LOG(CL_LOG_DEBUG, tmp_buffer); 
#endif

         if (type < cl_com_ssl_global_config_object->ssl_lib_lock_num) {
            pthread_mutex_lock(&(cl_com_ssl_global_config_object->ssl_lib_lock_mutex_array[type]));
         } else {
            CL_LOG(CL_LOG_ERROR,"lock type is larger than log array");
         }
      } else {
#if 0
         snprintf(tmp_buffer,1024,"unlocking ssl object: %d, file: %s, line: %d", 
                  type, tmp_filename, line);
         CL_LOG(CL_LOG_DEBUG,tmp_buffer); 
#endif
         if (type < cl_com_ssl_global_config_object->ssl_lib_lock_num) {
            pthread_mutex_unlock(&(cl_com_ssl_global_config_object->ssl_lib_lock_mutex_array[type]));
         } else {
            CL_LOG(CL_LOG_ERROR,"lock type is larger than log array");
         }
      }
   } else {
      CL_LOG(CL_LOG_ERROR,"global ssl config object not initalized");

      /* this two debug messages are only used to prevent compiler 
         warnings on IRIX65 compiler (when -Werror is set) 
         (unused symbols line and tmp_filename) when the if-endif parts
         above are disabled */
      CL_LOG_INT(CL_LOG_DEBUG,"dummy debug:", line);
      CL_LOG_STR(CL_LOG_DEBUG,"dummy debug:", tmp_filename);
   }
}


#ifdef __CL_FUNCTION__
#undef __CL_FUNCTION__
#endif
#define __CL_FUNCTION__ "cl_com_ssl_get_thread_id()"
static unsigned long cl_com_ssl_get_thread_id(void) {
   return (unsigned long) pthread_self();  
}

#ifdef __CL_FUNCTION__
#undef __CL_FUNCTION__
#endif
#define __CL_FUNCTION__ "cl_com_ssl_destroy_symbol_table()"
static int cl_com_ssl_destroy_symbol_table(void) {
#ifdef LOAD_OPENSSL
   {
      CL_LOG(CL_LOG_INFO,"shutting down ssl library symbol table ...");
      pthread_mutex_lock(&cl_com_ssl_crypto_handle_mutex);

      if (cl_com_ssl_crypto_handle == NULL) {
         CL_LOG(CL_LOG_ERROR,"there is no symbol table loaded!");
         pthread_mutex_unlock(&cl_com_ssl_crypto_handle_mutex);
         return CL_RETVAL_SSL_NO_SYMBOL_TABLE;
      }

      cl_com_ssl_func__CRYPTO_set_id_callback   = NULL;
      cl_com_ssl_func__CRYPTO_set_locking_callback   = NULL;
      cl_com_ssl_func__CRYPTO_num_locks   = NULL;
      cl_com_ssl_func__ERR_get_error   = NULL;
      cl_com_ssl_func__ERR_error_string_n   = NULL;
      cl_com_ssl_func__ERR_clear_error = NULL;
      cl_com_ssl_func__ERR_free_strings   = NULL;
      cl_com_ssl_func__BIO_free   = NULL;
      cl_com_ssl_func__BIO_new_fp   = NULL;
      cl_com_ssl_func__BIO_new_socket   = NULL;
      cl_com_ssl_func__BIO_new_mem_buf   = NULL;
      cl_com_ssl_func__BIO_printf   = NULL;
      cl_com_ssl_func__SSL_set_bio   = NULL;
      cl_com_ssl_func__SSL_accept   = NULL;
      cl_com_ssl_func__SSL_CTX_free   = NULL;
      cl_com_ssl_func__SSL_CTX_new   = NULL;
      cl_com_ssl_func__SSLv23_method   = NULL;
      cl_com_ssl_func__SSL_CTX_use_certificate_chain_file   = NULL;
      cl_com_ssl_func__SSL_CTX_use_certificate   = NULL;
      cl_com_ssl_func__SSL_CTX_use_PrivateKey_file   = NULL;
      cl_com_ssl_func__SSL_CTX_use_PrivateKey   = NULL;
      cl_com_ssl_func__SSL_CTX_load_verify_locations   = NULL;
      cl_com_ssl_func__SSL_library_init   = NULL;
      cl_com_ssl_func__SSL_load_error_strings   = NULL;
      cl_com_ssl_func__SSL_new   = NULL;
      cl_com_ssl_func__SSL_connect   = NULL;
      cl_com_ssl_func__SSL_shutdown   = NULL;
      cl_com_ssl_func__SSL_clear   = NULL;
      cl_com_ssl_func__SSL_free   = NULL;
      cl_com_ssl_func__SSL_get_fd = NULL;
      cl_com_ssl_func__SSL_get_error   = NULL;
      cl_com_ssl_func__SSL_get_verify_result   = NULL;
      cl_com_ssl_func__SSL_get_peer_certificate   = NULL;
      cl_com_ssl_func__SSL_write   = NULL;
      cl_com_ssl_func__SSL_read   = NULL;
      cl_com_ssl_func__X509_get_subject_name   = NULL;
      cl_com_ssl_func__X509_NAME_get_text_by_NID   = NULL;
      cl_com_ssl_func__SSL_CTX_set_verify = NULL;
      cl_com_ssl_func__X509_STORE_CTX_get_current_cert = NULL;
      cl_com_ssl_func__X509_NAME_get_text_by_OBJ = NULL;
      cl_com_ssl_func__OBJ_nid2obj = NULL;
      cl_com_ssl_func__X509_free = NULL;
      cl_com_ssl_func__EVP_PKEY_free = NULL;
      cl_com_ssl_func__SSL_CTX_ctrl = NULL;
      cl_com_ssl_func__SSL_ctrl = NULL;
      cl_com_ssl_func__RAND_status = NULL;
      cl_com_ssl_func__RAND_load_file = NULL;
      cl_com_ssl_func__SSL_get_cipher_list = NULL;
      cl_com_ssl_func__SSL_CTX_set_cipher_list = NULL;
      cl_com_ssl_func__SSL_set_cipher_list = NULL;
      cl_com_ssl_func__SSL_set_quiet_shutdown = NULL;
      cl_com_ssl_func__PEM_ASN1_read_bio = NULL;
      cl_com_ssl_func__d2i_X509 = NULL;
      cl_com_ssl_func__d2i_PKCS8_PRIV_KEY_INFO = NULL;
      cl_com_ssl_func__d2i_PrivateKey = NULL;
      cl_com_ssl_func__d2i_AutoPrivateKey = NULL;
      cl_com_ssl_func__d2i_PKCS8PrivateKey_bio = NULL;
      cl_com_ssl_func__EVP_PKCS82PKEY = NULL;
      cl_com_ssl_func__ASN1_item_d2i = NULL;
#ifdef ENABLE_CRL
      cl_com_ssl_func__PEM_ASN1_read = NULL;
      cl_com_ssl_func__SSL_CTX_get_cert_store = NULL;
      cl_com_ssl_func__X509_STORE_add_crl = NULL;
      cl_com_ssl_func__d2i_X509_CRL = NULL;
      cl_com_ssl_func__X509_STORE_set_flags = NULL;
      cl_com_ssl_func__X509_STORE_CTX_get_current_cert = NULL;
      cl_com_ssl_func__X509_STORE_new = NULL;
      cl_com_ssl_func__X509_get_subject_name = NULL;
      cl_com_ssl_func__X509_get_issuer_name = NULL;
      cl_com_ssl_func__X509_STORE_add_lookup = NULL;
      cl_com_ssl_func__X509_load_crl_file = NULL;
      cl_com_ssl_func__X509_STORE_CTX_new = NULL;
      cl_com_ssl_func__X509_STORE_CTX_init = NULL;
      cl_com_ssl_func__X509_STORE_CTX_cleanup = NULL;
      cl_com_ssl_func__X509_verify_cert = NULL;
      cl_com_ssl_func__X509_STORE_CTX_get_error = NULL;
      cl_com_ssl_func__ERR_print_errors_fp = NULL;
      cl_com_ssl_func__X509_LOOKUP_file = NULL;
      cl_com_ssl_func__X509_STORE_CTX_get_ex_data = NULL;
      cl_com_ssl_func__SSL_get_SSL_CTX = NULL;
      cl_com_ssl_func__X509_STORE_CTX_get_error_depth = NULL;
      cl_com_ssl_func__X509_NAME_oneline = NULL;
      cl_com_ssl_func__CRYPTO_free = NULL;
      cl_com_ssl_func__X509_verify_cert_error_string = NULL;
      cl_com_ssl_func__SSL_get_ex_data_X509_STORE_CTX_idx = NULL;
      cl_com_ssl_func__SSL_CTX_get_ex_data = NULL;
      cl_com_ssl_func__SSL_CTX_set_ex_data = NULL;
      cl_com_ssl_func__sk_num = NULL;
      cl_com_ssl_func__sk_value = NULL;
      cl_com_ssl_func__X509_STORE_get_by_subject = NULL;
      cl_com_ssl_func__EVP_PKEY_free = NULL;      
      cl_com_ssl_func__X509_STORE_CTX_set_error = NULL;
      cl_com_ssl_func__X509_OBJECT_free_contents = NULL;
      cl_com_ssl_func__X509_get_serialNumber = NULL;
      cl_com_ssl_func__X509_cmp_current_time = NULL;
      cl_com_ssl_func__ASN1_INTEGER_cmp = NULL;      
      cl_com_ssl_func__ASN1_INTEGER_get = NULL;
      cl_com_ssl_func__X509_CRL_verify = NULL;
      cl_com_ssl_func__X509_get_pubkey = NULL;
      cl_com_ssl_func__X509_STORE_set_default_paths = NULL;
      cl_com_ssl_func__X509_STORE_load_locations = NULL;
      cl_com_ssl_func__X509_STORE_free = NULL;
#endif      

      /*
       * INFO: do dlclose() shows memory leaks in dbx when RTLD_NODELETE flag is
       *       not set at dlopen()
       */      
      dlclose(cl_com_ssl_crypto_handle);
      cl_com_ssl_crypto_handle = NULL;

      pthread_mutex_unlock(&cl_com_ssl_crypto_handle_mutex);

      CL_LOG(CL_LOG_INFO,"shuting down ssl library symbol table done");
      return CL_RETVAL_OK;
   }
#else
   {
      return CL_RETVAL_OK;
   }
#endif
}

#ifdef __CL_FUNCTION__
#undef __CL_FUNCTION__
#endif
#define __CL_FUNCTION__ "cl_com_ssl_build_symbol_table()"
static int cl_com_ssl_build_symbol_table(void) {
   
#ifdef LOAD_OPENSSL
   {
      char* func_name = NULL;
      int had_errors = 0;
#if defined(FREEBSD) || defined(DARWIN)
      void* cl_com_ssl_crypto_handle_saved = NULL;
#endif


      CL_LOG(CL_LOG_INFO,"loading ssl library functions with dlopen() ...");

      pthread_mutex_lock(&cl_com_ssl_crypto_handle_mutex);
      if (cl_com_ssl_crypto_handle != NULL) {
         CL_LOG(CL_LOG_WARNING, "ssl library functions already loaded");
         pthread_mutex_unlock(&cl_com_ssl_crypto_handle_mutex);
         return CL_RETVAL_SSL_SYMBOL_TABLE_ALREADY_LOADED;
      }


#if defined(DARWIN)
#ifdef RTLD_NODELETE
      cl_com_ssl_crypto_handle = dlopen ("libssl.dylib", RTLD_NOW | RTLD_GLOBAL | RTLD_NODELETE);
#else
      cl_com_ssl_crypto_handle = dlopen ("libssl.dylib", RTLD_NOW | RTLD_GLOBAL );
#endif /* RTLD_NODELETE */

#elif defined(FREEBSD)
#ifdef RTLD_NODELETE
      cl_com_ssl_crypto_handle = dlopen ("libssl.so", RTLD_LAZY | RTLD_GLOBAL | RTLD_NODELETE);
#else
      cl_com_ssl_crypto_handle = dlopen ("libssl.so", RTLD_LAZY | RTLD_GLOBAL);
#endif /* RTLD_NODELETE */

#elif defined(HPUX)
#ifdef RTLD_NODELETE
      cl_com_ssl_crypto_handle = dlopen ("libssl.sl", RTLD_LAZY | RTLD_NODELETE);
#else
      cl_com_ssl_crypto_handle = dlopen ("libssl.sl", RTLD_LAZY );
#endif /* RTLD_NODELETE */

#else   
#ifdef RTLD_NODELETE
      cl_com_ssl_crypto_handle = dlopen ("libssl.so", RTLD_LAZY | RTLD_NODELETE);
#else
      cl_com_ssl_crypto_handle = dlopen ("libssl.so", RTLD_LAZY);
#endif /* RTLD_NODELETE */
#endif
      
      if (cl_com_ssl_crypto_handle == NULL) {
         CL_LOG_STR(CL_LOG_ERROR, "can't load ssl library: ", dlerror());
         cl_commlib_push_application_error(CL_LOG_ERROR, CL_RETVAL_SSL_DLOPEN_SSL_LIB_FAILED, MSG_CL_SSL_FW_OPEN_SSL_CRYPTO_FAILED);
         pthread_mutex_unlock(&cl_com_ssl_crypto_handle_mutex);
         return CL_RETVAL_SSL_DLOPEN_SSL_LIB_FAILED;
      }
      
#if defined(FREEBSD) || defined(DARWIN)
      cl_com_ssl_crypto_handle_saved = cl_com_ssl_crypto_handle;
      cl_com_ssl_crypto_handle = RTLD_DEFAULT;
#endif


      /* setting up crypto function pointers */
      func_name = "CRYPTO_set_id_callback";
      cl_com_ssl_func__CRYPTO_set_id_callback = (void (*)(unsigned long (*)(void)))dlsym(cl_com_ssl_crypto_handle, func_name);
      if ( cl_com_ssl_func__CRYPTO_set_id_callback == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }

      func_name = "CRYPTO_set_locking_callback";
      cl_com_ssl_func__CRYPTO_set_locking_callback = (void (*) (void (*)(int , int , const char *, int ))) dlsym(cl_com_ssl_crypto_handle, func_name);
      if ( cl_com_ssl_func__CRYPTO_set_locking_callback == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }

      func_name = "CRYPTO_num_locks";
      cl_com_ssl_func__CRYPTO_num_locks = (int (*) (void))   dlsym(cl_com_ssl_crypto_handle, func_name);
      if (cl_com_ssl_func__CRYPTO_num_locks == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }

      func_name = "ERR_get_error";
      cl_com_ssl_func__ERR_get_error = (unsigned long (*)(void)) dlsym(cl_com_ssl_crypto_handle, func_name);
      if (cl_com_ssl_func__ERR_get_error == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }

      func_name = "ERR_error_string_n";
      cl_com_ssl_func__ERR_error_string_n = (void (*)(unsigned long e, char *buf, size_t len))dlsym(cl_com_ssl_crypto_handle, func_name);
      if (cl_com_ssl_func__ERR_error_string_n == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }

      func_name = "ERR_free_strings";
      cl_com_ssl_func__ERR_free_strings = (void (*)(void))dlsym(cl_com_ssl_crypto_handle, func_name);
      if (cl_com_ssl_func__ERR_free_strings == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }

      func_name = "ERR_clear_error";
      cl_com_ssl_func__ERR_clear_error = (void (*)(void))dlsym(cl_com_ssl_crypto_handle, func_name);
      if (cl_com_ssl_func__ERR_clear_error == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }

      func_name = "BIO_free";
      cl_com_ssl_func__BIO_free = (int (*)(BIO *a))dlsym(cl_com_ssl_crypto_handle, func_name);
      if (cl_com_ssl_func__BIO_free == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }

      func_name = "BIO_new_fp";
      cl_com_ssl_func__BIO_new_fp = (BIO* (*)(FILE *stream, int flags))dlsym(cl_com_ssl_crypto_handle, func_name);
      if (cl_com_ssl_func__BIO_new_fp == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }

      func_name = "BIO_new_socket";
      cl_com_ssl_func__BIO_new_socket = (BIO* (*)(int sock, int close_flag))dlsym(cl_com_ssl_crypto_handle, func_name);
      if (cl_com_ssl_func__BIO_new_socket == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }

      func_name = "BIO_new_mem_buf";
      cl_com_ssl_func__BIO_new_mem_buf = (BIO* (*)(void *buf, int len))dlsym(cl_com_ssl_crypto_handle, func_name);
      if (cl_com_ssl_func__BIO_new_mem_buf == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }

      func_name = "BIO_printf";
      cl_com_ssl_func__BIO_printf = (int (*)(BIO *bio, const char *format, ...))dlsym(cl_com_ssl_crypto_handle, func_name);
      if (cl_com_ssl_func__BIO_printf == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }

      func_name = "SSL_set_bio";
      cl_com_ssl_func__SSL_set_bio = (void (*)(SSL *s, BIO *rbio,BIO *wbio))dlsym(cl_com_ssl_crypto_handle, func_name);
      if (cl_com_ssl_func__SSL_set_bio == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }

      func_name = "SSL_accept";
      cl_com_ssl_func__SSL_accept = (int (*)(SSL *ssl))dlsym(cl_com_ssl_crypto_handle, func_name);
      if (cl_com_ssl_func__SSL_accept == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }

      func_name = "SSL_CTX_free";
      cl_com_ssl_func__SSL_CTX_free = (void (*)(SSL_CTX *))dlsym(cl_com_ssl_crypto_handle, func_name);
      if (cl_com_ssl_func__SSL_CTX_free == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }

      func_name = "SSL_CTX_new";
      cl_com_ssl_func__SSL_CTX_new = (SSL_CTX* (*)(SSL_METHOD *meth))dlsym(cl_com_ssl_crypto_handle, func_name);
      if (cl_com_ssl_func__SSL_CTX_new == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }

      func_name = "SSLv23_method";
      cl_com_ssl_func__SSLv23_method = (SSL_METHOD* (*)(void))dlsym(cl_com_ssl_crypto_handle, func_name);
      if (cl_com_ssl_func__SSLv23_method == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }

      func_name = "SSL_CTX_use_certificate_chain_file";
      cl_com_ssl_func__SSL_CTX_use_certificate_chain_file = (int (*)(SSL_CTX *ctx, const char *file))dlsym(cl_com_ssl_crypto_handle, func_name);
      if (cl_com_ssl_func__SSL_CTX_use_certificate_chain_file == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }

      func_name = "SSL_CTX_use_certificate";
      cl_com_ssl_func__SSL_CTX_use_certificate = (int (*)(SSL_CTX *ctx, X509 *cert))dlsym(cl_com_ssl_crypto_handle, func_name);
      if (cl_com_ssl_func__SSL_CTX_use_certificate == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }

      func_name = "SSL_CTX_use_PrivateKey_file";
      cl_com_ssl_func__SSL_CTX_use_PrivateKey_file = (int (*)(SSL_CTX *ctx, const char *file, int type))dlsym(cl_com_ssl_crypto_handle, func_name);
      if (cl_com_ssl_func__SSL_CTX_use_PrivateKey_file == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }

      func_name = "SSL_CTX_use_PrivateKey";
      cl_com_ssl_func__SSL_CTX_use_PrivateKey = (int (*)(SSL_CTX *ctx, EVP_PKEY *pkey))dlsym(cl_com_ssl_crypto_handle, func_name);
      if (cl_com_ssl_func__SSL_CTX_use_PrivateKey == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }

      func_name = "SSL_CTX_load_verify_locations";
      cl_com_ssl_func__SSL_CTX_load_verify_locations = (int (*)(SSL_CTX *ctx, const char *CAfile, const char *CApath))dlsym(cl_com_ssl_crypto_handle, func_name);
      if (cl_com_ssl_func__SSL_CTX_load_verify_locations == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }

      func_name = "SSL_library_init";
      cl_com_ssl_func__SSL_library_init = (int (*)(void))dlsym(cl_com_ssl_crypto_handle, func_name);
      if (cl_com_ssl_func__SSL_library_init == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }

      func_name = "SSL_load_error_strings";
      cl_com_ssl_func__SSL_load_error_strings = (void (*)(void))dlsym(cl_com_ssl_crypto_handle, func_name);
      if (cl_com_ssl_func__SSL_load_error_strings == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }

      func_name = "SSL_new";
      cl_com_ssl_func__SSL_new = (SSL* (*)(SSL_CTX *ctx))dlsym(cl_com_ssl_crypto_handle, func_name);
      if (cl_com_ssl_func__SSL_new == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }

      func_name = "SSL_connect";
      cl_com_ssl_func__SSL_connect = (int (*)(SSL *ssl))dlsym(cl_com_ssl_crypto_handle, func_name);
      if (cl_com_ssl_func__SSL_connect == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }

      func_name = "SSL_shutdown";
      cl_com_ssl_func__SSL_shutdown = (int (*)(SSL *s))dlsym(cl_com_ssl_crypto_handle, func_name);
      if (cl_com_ssl_func__SSL_shutdown == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }

      func_name = "SSL_clear";
      cl_com_ssl_func__SSL_clear = (int (*)(SSL *s))dlsym(cl_com_ssl_crypto_handle, func_name);
      if (cl_com_ssl_func__SSL_clear == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }

      func_name = "SSL_free";
      cl_com_ssl_func__SSL_free = (void (*)(SSL *ssl))dlsym(cl_com_ssl_crypto_handle, func_name);
      if (cl_com_ssl_func__SSL_free == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }

      func_name = "SSL_get_fd";
      cl_com_ssl_func__SSL_get_fd = (int (*)(const SSL *ssl))dlsym(cl_com_ssl_crypto_handle, func_name);
      if (cl_com_ssl_func__SSL_get_fd == NULL) {
         CL_LOG_STR(CL_LOG_ERROR, "dlsym error: can't get function address:", func_name);
         had_errors++;
      }

      func_name = "SSL_get_error";
      cl_com_ssl_func__SSL_get_error = (int (*)(SSL *s,int ret_code))dlsym(cl_com_ssl_crypto_handle, func_name);
      if (cl_com_ssl_func__SSL_get_error == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }

      func_name = "SSL_get_verify_result";
      cl_com_ssl_func__SSL_get_verify_result = (long (*)(SSL *ssl))dlsym(cl_com_ssl_crypto_handle, func_name);
      if (cl_com_ssl_func__SSL_get_verify_result == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }

      func_name = "SSL_get_peer_certificate";
      cl_com_ssl_func__SSL_get_peer_certificate = (X509* (*)(SSL *s))dlsym(cl_com_ssl_crypto_handle, func_name);
      if (cl_com_ssl_func__SSL_get_peer_certificate == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }

      func_name = "SSL_write";
      cl_com_ssl_func__SSL_write = (int (*)(SSL *ssl,const void *buf,int num))dlsym(cl_com_ssl_crypto_handle, func_name);
      if (cl_com_ssl_func__SSL_write == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }

      func_name = "SSL_read";
      cl_com_ssl_func__SSL_read = (int (*)(SSL *ssl,void *buf,int num))dlsym(cl_com_ssl_crypto_handle, func_name);
      if (cl_com_ssl_func__SSL_read == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }

      func_name = "X509_get_subject_name";
      cl_com_ssl_func__X509_get_subject_name = (X509_NAME* (*)(X509 *a))dlsym(cl_com_ssl_crypto_handle, func_name);
      if (cl_com_ssl_func__X509_get_subject_name == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }

      func_name = "X509_NAME_get_text_by_NID";
      cl_com_ssl_func__X509_NAME_get_text_by_NID = (int (*)(X509_NAME *name, int nid, char *buf,int len))dlsym(cl_com_ssl_crypto_handle, func_name);
      if (cl_com_ssl_func__X509_NAME_get_text_by_NID == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }

      func_name = "SSL_CTX_set_verify";
      cl_com_ssl_func__SSL_CTX_set_verify = (void (*)(SSL_CTX *ctx, int mode, int (*verify_callback)(int, X509_STORE_CTX *)))dlsym(cl_com_ssl_crypto_handle, func_name);
      if (cl_com_ssl_func__SSL_CTX_set_verify == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }
 
      func_name = "X509_STORE_CTX_get_current_cert";
      cl_com_ssl_func__X509_STORE_CTX_get_current_cert = (X509* (*)(X509_STORE_CTX *ctx))dlsym(cl_com_ssl_crypto_handle, func_name);
      if ( cl_com_ssl_func__X509_STORE_CTX_get_current_cert == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }

      func_name = "X509_NAME_get_text_by_OBJ";
      cl_com_ssl_func__X509_NAME_get_text_by_OBJ = (int (*)(X509_NAME *name, ASN1_OBJECT *obj, char *buf,int len))dlsym(cl_com_ssl_crypto_handle, func_name);
      if ( cl_com_ssl_func__X509_NAME_get_text_by_OBJ == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }

      func_name = "OBJ_nid2obj";
      cl_com_ssl_func__OBJ_nid2obj = (ASN1_OBJECT* (*)(int n))dlsym(cl_com_ssl_crypto_handle, func_name);
      if (cl_com_ssl_func__OBJ_nid2obj == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }
    
      func_name = "X509_free";
      cl_com_ssl_func__X509_free = (void (*)(X509 *a))dlsym(cl_com_ssl_crypto_handle, func_name);
      if (cl_com_ssl_func__X509_free == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }

      func_name = "EVP_PKEY_free";
      cl_com_ssl_func__EVP_PKEY_free = (void (*)(EVP_PKEY *a))dlsym(cl_com_ssl_crypto_handle, func_name);
      if (cl_com_ssl_func__EVP_PKEY_free == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }

      func_name = "SSL_CTX_ctrl";
      cl_com_ssl_func__SSL_CTX_ctrl = (long (*)(SSL_CTX *ctx, int cmd, long larg, void *parg))dlsym(cl_com_ssl_crypto_handle, func_name);
      if (cl_com_ssl_func__SSL_CTX_ctrl == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }

      func_name = "SSL_ctrl";
      cl_com_ssl_func__SSL_ctrl = (long (*)(SSL *ssl, int cmd, long larg, void *parg))dlsym(cl_com_ssl_crypto_handle, func_name);
      if (cl_com_ssl_func__SSL_ctrl == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }

      func_name = "RAND_status";
      cl_com_ssl_func__RAND_status = (int (*)(void))dlsym(cl_com_ssl_crypto_handle, func_name);
      if (cl_com_ssl_func__RAND_status == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }

      func_name = "RAND_load_file";
      cl_com_ssl_func__RAND_load_file = (int (*)(const char *filename, long max_bytes))dlsym(cl_com_ssl_crypto_handle, func_name);
      if (cl_com_ssl_func__RAND_load_file == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }

      func_name = "SSL_get_cipher_list";
      cl_com_ssl_func__SSL_get_cipher_list = (const char* (*)(SSL *ssl, int priority))dlsym(cl_com_ssl_crypto_handle, func_name);
      if (cl_com_ssl_func__SSL_get_cipher_list == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }

      func_name = "SSL_CTX_set_cipher_list";
      cl_com_ssl_func__SSL_CTX_set_cipher_list = (int (*)(SSL_CTX *,const char *str))dlsym(cl_com_ssl_crypto_handle, func_name);
      if (cl_com_ssl_func__SSL_CTX_set_cipher_list == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }

      func_name = "SSL_set_cipher_list";
      cl_com_ssl_func__SSL_set_cipher_list = (int (*)(SSL *ssl, const char *str))dlsym(cl_com_ssl_crypto_handle, func_name);
      if (cl_com_ssl_func__SSL_set_cipher_list == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }

      func_name = "SSL_set_quiet_shutdown";
      cl_com_ssl_func__SSL_set_quiet_shutdown = (void (*)(SSL *ssl, int mode))dlsym(cl_com_ssl_crypto_handle, func_name);
      if (cl_com_ssl_func__SSL_set_quiet_shutdown == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }

      func_name = "PEM_ASN1_read_bio";
      cl_com_ssl_func__PEM_ASN1_read_bio = (void *(*)(void *(*d2i)(),const char *name,BIO *bp,void **x, pem_password_cb *cb, void *u))dlsym(cl_com_ssl_crypto_handle, func_name);
      if (cl_com_ssl_func__PEM_ASN1_read_bio == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }

#ifdef ENABLE_CRL
      func_name = "PEM_ASN1_read";
      cl_com_ssl_func__PEM_ASN1_read = (void *(*)(void *(*d2i)(),const char *name,FILE *fp,void **x, pem_password_cb *cb, void *u))dlsym(cl_com_ssl_crypto_handle, func_name);
      if (cl_com_ssl_func__PEM_ASN1_read == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }

      func_name = "SSL_CTX_get_cert_store";
      cl_com_ssl_func__SSL_CTX_get_cert_store = (X509_STORE *(*)(SSL_CTX *ctx))dlsym(cl_com_ssl_crypto_handle, func_name);
      if (cl_com_ssl_func__SSL_CTX_get_cert_store == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }

      func_name = "X509_STORE_add_crl";
      cl_com_ssl_func__X509_STORE_add_crl = (int (*)(X509_STORE *ctx, X509_CRL *x))dlsym(cl_com_ssl_crypto_handle, func_name);
      if (cl_com_ssl_func__X509_STORE_add_crl == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }

      func_name = "d2i_X509_CRL";
      cl_com_ssl_func__d2i_X509_CRL = (X509_CRL* (*)(X509_CRL **a, const unsigned char **pp, long length))dlsym(cl_com_ssl_crypto_handle, func_name);
      if (cl_com_ssl_func__d2i_X509_CRL == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }

      func_name = "d2i_X509";
      cl_com_ssl_func__d2i_X509 = (X509* (*)(X509 **a, const unsigned char **pp, long length))dlsym(cl_com_ssl_crypto_handle, func_name);
      if (cl_com_ssl_func__d2i_X509 == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }

      func_name = "d2i_PKCS8_PRIV_KEY_INFO";
      cl_com_ssl_func__d2i_PKCS8_PRIV_KEY_INFO = (PKCS8_PRIV_KEY_INFO* (*)(PKCS8_PRIV_KEY_INFO **a, const unsigned char **pp, long length))dlsym(cl_com_ssl_crypto_handle, func_name);
      if (cl_com_ssl_func__d2i_PKCS8_PRIV_KEY_INFO == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }

      func_name = "d2i_PrivateKey";
      cl_com_ssl_func__d2i_PrivateKey = (EVP_PKEY* (*)(int type, EVP_PKEY **a, const unsigned char **pp, long length))dlsym(cl_com_ssl_crypto_handle, func_name);
      if (cl_com_ssl_func__d2i_PrivateKey == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }

      func_name = "d2i_AutoPrivateKey";
      cl_com_ssl_func__d2i_AutoPrivateKey = (EVP_PKEY* (*)(EVP_PKEY **a, const unsigned char **pp, long length))dlsym(cl_com_ssl_crypto_handle, func_name);
      if (cl_com_ssl_func__d2i_AutoPrivateKey == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }

      func_name = "d2i_PKCS8PrivateKey_bio";
      cl_com_ssl_func__d2i_PKCS8PrivateKey_bio = (EVP_PKEY* (*)(BIO *bp, EVP_PKEY **x, pem_password_cb *cb, void *u))dlsym(cl_com_ssl_crypto_handle, func_name);
      if (cl_com_ssl_func__d2i_PKCS8PrivateKey_bio == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }

      func_name = "EVP_PKCS82PKEY";
      cl_com_ssl_func__EVP_PKCS82PKEY = (EVP_PKEY* (*)(PKCS8_PRIV_KEY_INFO *p8))dlsym(cl_com_ssl_crypto_handle, func_name);
      if (cl_com_ssl_func__EVP_PKCS82PKEY == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }

      func_name = "ASN1_item_d2i";
      cl_com_ssl_func__ASN1_item_d2i = (ASN1_VALUE* (*)(ASN1_VALUE **pval, const unsigned char **in, long len, const ASN1_ITEM *it))dlsym(cl_com_ssl_crypto_handle, func_name);
      if (cl_com_ssl_func__ASN1_item_d2i == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }

      func_name = "X509_STORE_set_flags";
      cl_com_ssl_func__X509_STORE_set_flags = (int (*)(X509_STORE *ctx, unsigned long flags))dlsym(cl_com_ssl_crypto_handle, func_name);
      if (cl_com_ssl_func__X509_STORE_set_flags == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }

      func_name = "X509_STORE_CTX_get_current_cert";
      cl_com_ssl_func__X509_STORE_CTX_get_current_cert = (X509*(*)(X509_STORE_CTX *ctx))dlsym(cl_com_ssl_crypto_handle, func_name);
      if (cl_com_ssl_func__X509_STORE_CTX_get_current_cert == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }

      func_name = "X509_STORE_new";
      cl_com_ssl_func__X509_STORE_new = (X509_STORE*(*)(void))dlsym(cl_com_ssl_crypto_handle, func_name);
      if (cl_com_ssl_func__X509_STORE_new == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }

      func_name = "X509_get_subject_name";
      cl_com_ssl_func__X509_get_subject_name = (X509_NAME*(*)(X509 *a))dlsym(cl_com_ssl_crypto_handle, func_name);
      if (cl_com_ssl_func__X509_get_subject_name == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }
      func_name = "X509_get_issuer_name";
      cl_com_ssl_func__X509_get_issuer_name = (X509_NAME*(*)(X509 *a))dlsym(cl_com_ssl_crypto_handle, func_name);
      if (cl_com_ssl_func__X509_get_issuer_name == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }

      func_name = "X509_STORE_add_lookup";
      cl_com_ssl_func__X509_STORE_add_lookup = (X509_LOOKUP*(*)(X509_STORE *v, X509_LOOKUP_METHOD *m))dlsym(cl_com_ssl_crypto_handle, func_name);
      if (cl_com_ssl_func__X509_STORE_add_lookup == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }

      func_name = "X509_load_crl_file";
      cl_com_ssl_func__X509_load_crl_file = (int (*)(X509_LOOKUP *ctx, const char *file, int type))dlsym(cl_com_ssl_crypto_handle, func_name);
      if (cl_com_ssl_func__X509_load_crl_file == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }

      func_name = "X509_STORE_CTX_new";
      cl_com_ssl_func__X509_STORE_CTX_new = (X509_STORE_CTX*(*)(void))dlsym(cl_com_ssl_crypto_handle, func_name);
      if (cl_com_ssl_func__X509_STORE_CTX_new == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }

      func_name = "X509_STORE_CTX_init";
      cl_com_ssl_func__X509_STORE_CTX_init = (int (*)(X509_STORE_CTX *ctx, X509_STORE *store, X509 *x509, STACK_OF(X509) *chain))dlsym(cl_com_ssl_crypto_handle, func_name);
      if (cl_com_ssl_func__X509_STORE_CTX_init == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }

      func_name = "X509_STORE_CTX_cleanup";
      cl_com_ssl_func__X509_STORE_CTX_cleanup = (void (*)(X509_STORE_CTX *ctx))dlsym(cl_com_ssl_crypto_handle, func_name);
      if (cl_com_ssl_func__X509_STORE_CTX_cleanup == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }

      func_name = "X509_verify_cert";
      cl_com_ssl_func__X509_verify_cert = (int (*)(X509_STORE_CTX *ctx))dlsym(cl_com_ssl_crypto_handle, func_name);
      if (cl_com_ssl_func__X509_verify_cert == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }

      func_name = "X509_STORE_CTX_get_error";
      cl_com_ssl_func__X509_STORE_CTX_get_error = (int (*)(X509_STORE_CTX *ctx))dlsym(cl_com_ssl_crypto_handle, func_name);
      if (cl_com_ssl_func__X509_STORE_CTX_get_error == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }

      func_name = "ERR_print_errors_fp";
      cl_com_ssl_func__ERR_print_errors_fp = (void (*)(FILE *fp))dlsym(cl_com_ssl_crypto_handle, func_name);
      if (cl_com_ssl_func__ERR_print_errors_fp == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }

      func_name = "X509_LOOKUP_file";
      cl_com_ssl_func__X509_LOOKUP_file = (X509_LOOKUP_METHOD* (*)(void))dlsym(cl_com_ssl_crypto_handle, func_name);
      if (cl_com_ssl_func__X509_LOOKUP_file == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }

      func_name = "X509_STORE_CTX_get_ex_data";
      cl_com_ssl_func__X509_STORE_CTX_get_ex_data = (void* (*)(X509_STORE_CTX *ctx,int idx))dlsym(cl_com_ssl_crypto_handle, func_name);
      if (cl_com_ssl_func__X509_STORE_CTX_get_ex_data == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }

      func_name = "SSL_get_SSL_CTX";
      cl_com_ssl_func__SSL_get_SSL_CTX = (SSL_CTX* (*)(SSL *ssl))dlsym(cl_com_ssl_crypto_handle, func_name);
      if (cl_com_ssl_func__SSL_get_SSL_CTX == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }

      func_name = "X509_STORE_CTX_get_error_depth";
      cl_com_ssl_func__X509_STORE_CTX_get_error_depth = (int (*)(X509_STORE_CTX *ctx))dlsym(cl_com_ssl_crypto_handle, func_name);
      if (cl_com_ssl_func__X509_STORE_CTX_get_error_depth == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }

      func_name = "X509_NAME_oneline";
      cl_com_ssl_func__X509_NAME_oneline = (char* (*)(X509_NAME *a,char *buf,int size))dlsym(cl_com_ssl_crypto_handle, func_name);
      if (cl_com_ssl_func__X509_NAME_oneline == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }

      func_name = "CRYPTO_free";
      cl_com_ssl_func__CRYPTO_free = (void (*)(void *))dlsym(cl_com_ssl_crypto_handle, func_name);
      if (cl_com_ssl_func__CRYPTO_free == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }

      func_name = "X509_verify_cert_error_string";
      cl_com_ssl_func__X509_verify_cert_error_string = (const char* (*)(long n))dlsym(cl_com_ssl_crypto_handle, func_name);
      if (cl_com_ssl_func__X509_verify_cert_error_string == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }

      func_name = "SSL_get_ex_data_X509_STORE_CTX_idx";
      cl_com_ssl_func__SSL_get_ex_data_X509_STORE_CTX_idx = (int (*)(void))dlsym(cl_com_ssl_crypto_handle, func_name);
      if (cl_com_ssl_func__SSL_get_ex_data_X509_STORE_CTX_idx == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }

      func_name = "SSL_CTX_get_ex_data";
      cl_com_ssl_func__SSL_CTX_get_ex_data = (void* (*)(SSL_CTX *ssl,int idx))dlsym(cl_com_ssl_crypto_handle, func_name);
      if (cl_com_ssl_func__SSL_CTX_get_ex_data == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }

      func_name = "SSL_CTX_set_ex_data";
      cl_com_ssl_func__SSL_CTX_set_ex_data = (int (*)(SSL_CTX *ssl,int idx,void *data))dlsym(cl_com_ssl_crypto_handle, func_name);
      if (cl_com_ssl_func__SSL_CTX_set_ex_data == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }

      func_name = "sk_num";
      cl_com_ssl_func__sk_num = (int (*)(const STACK *))dlsym(cl_com_ssl_crypto_handle, func_name);
      if (cl_com_ssl_func__sk_num == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }

      func_name = "sk_value";
      cl_com_ssl_func__sk_value = (char* (*)(const STACK *, int))dlsym(cl_com_ssl_crypto_handle, func_name);
      if (cl_com_ssl_func__sk_value == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }

      func_name = "X509_STORE_get_by_subject";
      cl_com_ssl_func__X509_STORE_get_by_subject = (int (*)(X509_STORE_CTX *vs,int type,X509_NAME *name, X509_OBJECT *ret))dlsym(cl_com_ssl_crypto_handle, func_name);
      if (cl_com_ssl_func__X509_STORE_get_by_subject == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }

      func_name = "EVP_PKEY_free";
      cl_com_ssl_func__EVP_PKEY_free = (void (*)(EVP_PKEY *pkey))dlsym(cl_com_ssl_crypto_handle, func_name);
      if (cl_com_ssl_func__EVP_PKEY_free == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }

      func_name = "X509_STORE_CTX_set_error";
      cl_com_ssl_func__X509_STORE_CTX_set_error = (void  (*)(X509_STORE_CTX *ctx,int s))dlsym(cl_com_ssl_crypto_handle, func_name);
      if (cl_com_ssl_func__X509_STORE_CTX_set_error == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }

      func_name = "X509_OBJECT_free_contents";
      cl_com_ssl_func__X509_OBJECT_free_contents = (void (*)(X509_OBJECT *a))dlsym(cl_com_ssl_crypto_handle, func_name);
      if (cl_com_ssl_func__X509_OBJECT_free_contents == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }

      func_name = "X509_get_serialNumber";
      cl_com_ssl_func__X509_get_serialNumber = (ASN1_INTEGER* (*)(X509 *x))dlsym(cl_com_ssl_crypto_handle, func_name);
      if (cl_com_ssl_func__X509_get_serialNumber == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }

      func_name = "X509_cmp_current_time";
      cl_com_ssl_func__X509_cmp_current_time = (int (*)(ASN1_TIME *s))dlsym(cl_com_ssl_crypto_handle, func_name);
      if (cl_com_ssl_func__X509_cmp_current_time == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }

      func_name = "ASN1_INTEGER_cmp";
      cl_com_ssl_func__ASN1_INTEGER_cmp = (int (*)(ASN1_INTEGER *x, ASN1_INTEGER *y))dlsym(cl_com_ssl_crypto_handle, func_name);
      if (cl_com_ssl_func__ASN1_INTEGER_cmp == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }

      func_name = "ASN1_INTEGER_get";
      cl_com_ssl_func__ASN1_INTEGER_get = (long (*)(ASN1_INTEGER *a))dlsym(cl_com_ssl_crypto_handle, func_name);
      if (cl_com_ssl_func__ASN1_INTEGER_get == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }

      func_name = "X509_CRL_verify";
      cl_com_ssl_func__X509_CRL_verify = (int (*)(X509_CRL *a, EVP_PKEY *r))dlsym(cl_com_ssl_crypto_handle, func_name);
      if (cl_com_ssl_func__X509_CRL_verify == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }

      func_name = "X509_get_pubkey";
      cl_com_ssl_func__X509_get_pubkey = (EVP_PKEY* (*)(X509 *x))dlsym(cl_com_ssl_crypto_handle, func_name);
      if (cl_com_ssl_func__X509_get_pubkey == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }

      func_name = "X509_STORE_set_default_paths";
      cl_com_ssl_func__X509_STORE_set_default_paths = (int (*)(X509_STORE *ctx))dlsym(cl_com_ssl_crypto_handle, func_name);
      if (cl_com_ssl_func__X509_STORE_set_default_paths == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }

      func_name = "X509_STORE_load_locations";
      cl_com_ssl_func__X509_STORE_load_locations = (int (*)(X509_STORE *ctx, const char *file, const char *dir))dlsym(cl_com_ssl_crypto_handle, func_name);
      if (cl_com_ssl_func__X509_STORE_load_locations == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }

      func_name = "X509_STORE_free";
      cl_com_ssl_func__X509_STORE_free = (void (*)(X509_STORE *ctx))dlsym(cl_com_ssl_crypto_handle, func_name);
      if (cl_com_ssl_func__X509_STORE_free == NULL) {
         CL_LOG_STR(CL_LOG_ERROR,"dlsym error: can't get function address:", func_name);
         had_errors++;
      }

#endif

      if (had_errors != 0) {
         CL_LOG_INT(CL_LOG_ERROR,"nr of not loaded function addresses:",had_errors);
         cl_commlib_push_application_error(CL_LOG_ERROR, CL_RETVAL_SSL_CANT_LOAD_ALL_FUNCTIONS, MSG_CL_SSL_FW_LOAD_CRYPTO_SYMBOL_FAILED);
         return CL_RETVAL_SSL_CANT_LOAD_ALL_FUNCTIONS;
      }

#if defined(FREEBSD)
      cl_com_ssl_crypto_handle = cl_com_ssl_crypto_handle_saved;
#endif

      pthread_mutex_unlock(&cl_com_ssl_crypto_handle_mutex);
      CL_LOG(CL_LOG_INFO,"loading ssl library functions with dlopen() done");

      return CL_RETVAL_OK;
   }
#else
   {
      CL_LOG(CL_LOG_INFO,"setting up ssl library function pointers ...");
      pthread_mutex_lock(&cl_com_ssl_crypto_handle_mutex);


      /* setting up crypto function pointers */
      cl_com_ssl_func__CRYPTO_set_id_callback              = CRYPTO_set_id_callback;
      cl_com_ssl_func__CRYPTO_set_locking_callback         = CRYPTO_set_locking_callback;
      cl_com_ssl_func__CRYPTO_num_locks                    = CRYPTO_num_locks;
      cl_com_ssl_func__ERR_get_error                       = ERR_get_error;
      cl_com_ssl_func__ERR_error_string_n                  = ERR_error_string_n;
      cl_com_ssl_func__ERR_clear_error                     = ERR_clear_error;
      cl_com_ssl_func__ERR_free_strings                    = ERR_free_strings;
      cl_com_ssl_func__BIO_free                            = BIO_free;
      cl_com_ssl_func__BIO_new_fp                          = BIO_new_fp;
      cl_com_ssl_func__BIO_new_socket                      = BIO_new_socket;
      cl_com_ssl_func__BIO_new_mem_buf                     = BIO_new_mem_buf;
      cl_com_ssl_func__BIO_printf                          = BIO_printf;
      cl_com_ssl_func__SSL_set_bio                         = SSL_set_bio;
      cl_com_ssl_func__SSL_accept                          = SSL_accept;
      cl_com_ssl_func__SSL_CTX_free                        = SSL_CTX_free;
      cl_com_ssl_func__SSL_CTX_new                         = SSL_CTX_new;
      cl_com_ssl_func__SSLv23_method                       = SSLv23_method;
      cl_com_ssl_func__SSL_CTX_use_certificate_chain_file  = SSL_CTX_use_certificate_chain_file;
      cl_com_ssl_func__SSL_CTX_use_certificate  = SSL_CTX_use_certificate;
      cl_com_ssl_func__SSL_CTX_use_PrivateKey_file         = SSL_CTX_use_PrivateKey_file;
      cl_com_ssl_func__SSL_CTX_use_PrivateKey              = SSL_CTX_use_PrivateKey;
      cl_com_ssl_func__SSL_CTX_load_verify_locations       = SSL_CTX_load_verify_locations;
      cl_com_ssl_func__SSL_library_init                    = SSL_library_init;
      cl_com_ssl_func__SSL_load_error_strings              = SSL_load_error_strings;
      cl_com_ssl_func__SSL_new                             = SSL_new;
      cl_com_ssl_func__SSL_connect                         = SSL_connect;
      cl_com_ssl_func__SSL_shutdown                        = SSL_shutdown;
      cl_com_ssl_func__SSL_clear                           = SSL_clear;
      cl_com_ssl_func__SSL_free                            = SSL_free;
      cl_com_ssl_func__SSL_get_fd                          = SSL_get_fd;
      cl_com_ssl_func__SSL_get_error                       = (int (*)(SSL *s,int ret_code))SSL_get_error;
      cl_com_ssl_func__SSL_get_verify_result               = (long (*)(SSL *ssl))SSL_get_verify_result;
      cl_com_ssl_func__SSL_get_peer_certificate            = (X509* (*)(SSL *s))SSL_get_peer_certificate;
      cl_com_ssl_func__SSL_write                           = SSL_write;
      cl_com_ssl_func__SSL_read                            = SSL_read;
      cl_com_ssl_func__X509_get_subject_name               = X509_get_subject_name;
      cl_com_ssl_func__X509_NAME_get_text_by_NID           = X509_NAME_get_text_by_NID;
      cl_com_ssl_func__SSL_CTX_set_verify                  = SSL_CTX_set_verify;
      cl_com_ssl_func__X509_STORE_CTX_get_current_cert     = X509_STORE_CTX_get_current_cert;
      cl_com_ssl_func__X509_NAME_get_text_by_OBJ           = X509_NAME_get_text_by_OBJ;
      cl_com_ssl_func__OBJ_nid2obj                         = OBJ_nid2obj;
      cl_com_ssl_func__X509_free                           = X509_free;
      cl_com_ssl_func__EVP_PKEY_free                       = EVP_PKEY_free;
      cl_com_ssl_func__SSL_CTX_ctrl                        = SSL_CTX_ctrl;
      cl_com_ssl_func__SSL_ctrl                            = SSL_ctrl;
      cl_com_ssl_func__RAND_status                         = RAND_status;
      cl_com_ssl_func__RAND_load_file                      = RAND_load_file;
      cl_com_ssl_func__SSL_get_cipher_list                 = (const char* (*)(SSL *ssl, int priority))SSL_get_cipher_list;
      cl_com_ssl_func__SSL_CTX_set_cipher_list             = SSL_CTX_set_cipher_list;
      cl_com_ssl_func__SSL_set_cipher_list                 = SSL_set_cipher_list;
      cl_com_ssl_func__SSL_set_quiet_shutdown              = SSL_set_quiet_shutdown;
      cl_com_ssl_func__PEM_ASN1_read_bio                   = PEM_ASN1_read_bio;
      cl_com_ssl_func__d2i_X509                            = d2i_X509;
      cl_com_ssl_func__d2i_PKCS8_PRIV_KEY_INFO             = d2i_PKCS8_PRIV_KEY_INFO;
      cl_com_ssl_func__d2i_PrivateKey                      = d2i_PrivateKey;
      cl_com_ssl_func__d2i_AutoPrivateKey                  = d2i_AutoPrivateKey;
      cl_com_ssl_func__d2i_PKCS8PrivateKey_bio             = d2i_PKCS8PrivateKey_bio;
      cl_com_ssl_func__EVP_PKCS82PKEY                      = EVP_PKCS82PKEY;
      cl_com_ssl_func__ASN1_item_d2i                       = ASN1_item_d2i;

#ifdef ENABLE_CRL
      cl_com_ssl_func__PEM_ASN1_read                       = PEM_ASN1_read;
      cl_com_ssl_func__SSL_CTX_get_cert_store              = (X509_STORE *(*)(SSL_CTX *ctx))SSL_CTX_get_cert_store;
      cl_com_ssl_func__X509_STORE_add_crl                  = X509_STORE_add_crl;
      cl_com_ssl_func__d2i_X509_CRL                        = d2i_X509_CRL;
      cl_com_ssl_func__X509_STORE_set_flags = X509_STORE_set_flags;
      cl_com_ssl_func__X509_STORE_CTX_get_current_cert = X509_STORE_CTX_get_current_cert;
      cl_com_ssl_func__X509_STORE_new = X509_STORE_new;
      cl_com_ssl_func__X509_get_subject_name = X509_get_subject_name;
      cl_com_ssl_func__X509_get_issuer_name = X509_get_issuer_name;
      cl_com_ssl_func__X509_STORE_add_lookup = X509_STORE_add_lookup;
      cl_com_ssl_func__X509_load_crl_file = X509_load_crl_file;
      cl_com_ssl_func__X509_STORE_CTX_new = X509_STORE_CTX_new;
      cl_com_ssl_func__X509_STORE_CTX_init = X509_STORE_CTX_init;
      cl_com_ssl_func__X509_STORE_CTX_cleanup = X509_STORE_CTX_cleanup;
      cl_com_ssl_func__X509_verify_cert = X509_verify_cert;
      cl_com_ssl_func__X509_STORE_CTX_get_error = X509_STORE_CTX_get_error;
      cl_com_ssl_func__ERR_print_errors_fp = ERR_print_errors_fp;
      cl_com_ssl_func__X509_LOOKUP_file = X509_LOOKUP_file;
      cl_com_ssl_func__X509_STORE_CTX_get_ex_data = X509_STORE_CTX_get_ex_data;
      cl_com_ssl_func__SSL_get_SSL_CTX = (SSL_CTX* (*)(SSL *ssl))SSL_get_SSL_CTX;
      cl_com_ssl_func__X509_STORE_CTX_get_error_depth = X509_STORE_CTX_get_error_depth;
      cl_com_ssl_func__X509_NAME_oneline = X509_NAME_oneline;
      cl_com_ssl_func__CRYPTO_free = CRYPTO_free;
      cl_com_ssl_func__X509_verify_cert_error_string = X509_verify_cert_error_string;
      cl_com_ssl_func__SSL_get_ex_data_X509_STORE_CTX_idx = SSL_get_ex_data_X509_STORE_CTX_idx;
      cl_com_ssl_func__SSL_CTX_get_ex_data = (void* (*)(SSL_CTX *ssl,int idx))SSL_CTX_get_ex_data;
      cl_com_ssl_func__SSL_CTX_set_ex_data = SSL_CTX_set_ex_data;
      cl_com_ssl_func__sk_num = sk_num;
      cl_com_ssl_func__sk_value = sk_value;
      cl_com_ssl_func__X509_STORE_get_by_subject = X509_STORE_get_by_subject;
      cl_com_ssl_func__EVP_PKEY_free = EVP_PKEY_free;      
      cl_com_ssl_func__X509_STORE_CTX_set_error = X509_STORE_CTX_set_error;
      cl_com_ssl_func__X509_OBJECT_free_contents = X509_OBJECT_free_contents;
      cl_com_ssl_func__X509_get_serialNumber = X509_get_serialNumber;
      cl_com_ssl_func__X509_cmp_current_time = X509_cmp_current_time;
      cl_com_ssl_func__ASN1_INTEGER_cmp = ASN1_INTEGER_cmp;      
      cl_com_ssl_func__ASN1_INTEGER_get = ASN1_INTEGER_get;
      cl_com_ssl_func__X509_CRL_verify = X509_CRL_verify;
      cl_com_ssl_func__X509_get_pubkey = X509_get_pubkey;
      cl_com_ssl_func__X509_STORE_set_default_paths = X509_STORE_set_default_paths;
      cl_com_ssl_func__X509_STORE_load_locations = X509_STORE_load_locations;
      cl_com_ssl_func__X509_STORE_free = X509_STORE_free;
#endif

      pthread_mutex_unlock(&cl_com_ssl_crypto_handle_mutex);
      CL_LOG(CL_LOG_INFO,"setting up ssl library function pointers done");

      return CL_RETVAL_OK;
   }
#endif
}



#ifdef __CL_FUNCTION__
#undef __CL_FUNCTION__
#endif
#define __CL_FUNCTION__ "cl_com_ssl_get_error_text()"
static const char* cl_com_ssl_get_error_text(int ssl_error) {
   switch(ssl_error) {
      case SSL_ERROR_NONE: {
         return "SSL_ERROR_NONE";
      }
      case SSL_ERROR_ZERO_RETURN: {
         return "SSL_ERROR_ZERO_RETURN";
      }
      case SSL_ERROR_WANT_READ: {
         return "SSL_ERROR_WANT_READ";
      }
      case SSL_ERROR_WANT_WRITE: {
         return "SSL_ERROR_WANT_WRITE";
      }
      case SSL_ERROR_WANT_CONNECT: {
         return "SSL_ERROR_WANT_CONNECT";
      }
      case SSL_ERROR_WANT_ACCEPT: {
         return "SSL_ERROR_WANT_ACCEPT";
      }
      case SSL_ERROR_WANT_X509_LOOKUP: {
         return "SSL_ERROR_WANT_X509_LOOKUP";
      }
      case SSL_ERROR_SYSCALL: {
         return "SSL_ERROR_SYSCALL";
      }
      case SSL_ERROR_SSL: {
         return "SSL_ERROR_SSL";
      }
      default: {
         break;
      }
   }
   return "UNEXPECTED SSL ERROR STATE";
}


#ifdef __CL_FUNCTION__
#undef __CL_FUNCTION__
#endif
#define __CL_FUNCTION__ "cl_com_ssl_transform_ssl_error()"
static int cl_com_ssl_transform_ssl_error(unsigned long ssl_error, char* buffer, unsigned long buflen, char** transformed_error) {

   char help_buf[1024];
   unsigned long counter = 0;
   char* help = NULL;
   char* lasts = NULL;

   char* buffer_copy = NULL;
   char* module = NULL;
   char* error_text = NULL;
   cl_bool_t do_ignore = CL_FALSE;

   if (buffer == NULL || transformed_error == NULL) {
      return CL_RETVAL_PARAMS;
   }

   if (*transformed_error != NULL) {
      return CL_RETVAL_PARAMS;
   }

   buffer_copy = (char*) malloc(sizeof(char)*buflen);
   if (buffer_copy == NULL) {
      return CL_RETVAL_MALLOC;
   }
   sge_strlcpy(buffer_copy, buffer, buflen);

   help = strtok_r(buffer_copy, ":", &lasts);
   if (help != NULL) {
      while( (help = strtok_r(NULL, ":", &lasts)) != NULL ) {
         counter++;
         if (counter == 2) {
            module = strdup(help);
            if (module == NULL) {
               free(buffer_copy);
               return CL_RETVAL_MALLOC;
            }
         }
         if (counter == 4) {
            error_text = strdup(help);
            if (error_text == NULL) {
               free(buffer_copy);
               if (module != NULL) {
                  free(module);
               }
               return CL_RETVAL_MALLOC;
            }
         }
      }
   }

   /* buffer copy is malloc()ed here and != NULL , free buffer_copy ...*/
   free(buffer_copy);
   buffer_copy = NULL;

   if (module == NULL) {
      module = strdup("???");
      if (module == NULL) {
         if (error_text != NULL) {
            free(error_text);
         }
         return CL_RETVAL_MALLOC;
      }
   }  

   if (error_text == NULL) {
      error_text = (char*) malloc(sizeof(char)*buflen);
      if (error_text == NULL) {
         free(module);
         return CL_RETVAL_MALLOC;
      }
      sge_strlcpy(error_text, buffer, buflen);
   }  


   switch (ssl_error) {
      case 336445449:
      case 537346050:
      case 336445442:
      case 218595386:
      case 151470093:
      case 185090057: {
         do_ignore = CL_TRUE;
         break;
      }
      case 151441508: {
         *transformed_error = strdup(MSG_CL_COMMLIB_SSL_ERROR_151441508);
         break;
      }
      case 33558541: {
         *transformed_error = strdup(MSG_CL_COMMLIB_SSL_ERROR_33558541);
         break;
      }
      case 336151573: {
         *transformed_error = strdup(MSG_CL_COMMLIB_SSL_ERROR_336151573);
         break;
      }
      case 336105650: {
         *transformed_error = strdup(MSG_CL_COMMLIB_SSL_ERROR_336105650);
         break;
      }
      case 336151576: {
         *transformed_error = strdup(MSG_CL_COMMLIB_SSL_ERROR_336105650);
         break;
      }
      default: {
         snprintf(help_buf, 1024, MSG_CL_COMMLIB_SSL_ERROR_NR_AND_TEXT_USS, sge_u32c(ssl_error), module, error_text);
         *transformed_error = strdup(help_buf);
      }
   }

   /* both variables are malloc()ed and != NULL at this point */
   free(module);
   module = NULL;
   free(error_text);
   error_text = NULL;

   if (do_ignore == CL_TRUE) {
      CL_LOG_STR_STR_INT(CL_LOG_WARNING, "will not report ssl error text to application:", buffer, "ssl id", (int) ssl_error);
      return CL_RETVAL_DO_IGNORE;
   }

   if (*transformed_error == NULL) {
      return CL_RETVAL_MALLOC;
   }
   return CL_RETVAL_OK; /* we have a malloced error */
}

#ifdef __CL_FUNCTION__
#undef __CL_FUNCTION__
#endif
#define __CL_FUNCTION__ "cl_com_ssl_log_ssl_errors()"
static int cl_com_ssl_log_ssl_errors(const char* function_name) {
   const char* func_name = "n.a.";
   unsigned long ssl_error;
   unsigned long ret_val;
   char* transformed_ssl_error = NULL;
   char buffer[512];
   char help_buf[1024];
   cl_bool_t had_errors = CL_FALSE;

   if (function_name != NULL) {
      func_name = function_name;
   }

   if (cl_com_ssl_func__ERR_get_error == NULL) {
      CL_LOG(CL_LOG_ERROR, "no cl_com_ssl_func__ERR_get_error available");
      return CL_RETVAL_OK;
   }   

   while((ssl_error = cl_com_ssl_func__ERR_get_error())) {
      cl_com_ssl_func__ERR_error_string_n(ssl_error,buffer,512);
      snprintf(help_buf, 1024, MSG_CL_COMMLIB_SSL_ERROR_USS, sge_u32c(ssl_error), func_name, buffer);
      CL_LOG(CL_LOG_ERROR,help_buf);

      ret_val = cl_com_ssl_transform_ssl_error(ssl_error,buffer,512, &transformed_ssl_error);

      if (transformed_ssl_error != NULL) {
         sge_strlcpy(help_buf, transformed_ssl_error, 1024);
         free(transformed_ssl_error);
         transformed_ssl_error = NULL;
      } else {
         sge_strlcpy(help_buf, buffer, 1024);
      }

      if (ret_val != CL_RETVAL_DO_IGNORE) {
         cl_commlib_push_application_error(CL_LOG_ERROR, CL_RETVAL_SSL_GET_SSL_ERROR, help_buf );
      }
      had_errors = CL_TRUE;
   }

   if (had_errors == CL_FALSE) {
      CL_LOG(CL_LOG_INFO, "no SSL errors available");
   }

   return CL_RETVAL_OK;
}


#ifdef __CL_FUNCTION__
#undef __CL_FUNCTION__
#endif
#define __CL_FUNCTION__ "cl_com_ssl_free_com_private()"
static int cl_com_ssl_free_com_private(cl_com_connection_t* connection) {
   cl_com_ssl_private_t* private = NULL;

   if (connection == NULL) {
      return CL_RETVAL_PARAMS;
   }

   private = cl_com_ssl_get_private(connection);
   if (private == NULL) {
      return CL_RETVAL_NO_FRAMEWORK_INIT;
   }

   /* free ssl_crl_data */
   if (private->ssl_crl_data != NULL) {

      /* free cl_ssl_verify_crl_data_t content */
      if (private->ssl_crl_data->store != NULL) {
         cl_com_ssl_func__X509_STORE_free(private->ssl_crl_data->store);
         private->ssl_crl_data->store = NULL;
      }
      cl_com_ssl_log_ssl_errors(__CL_FUNCTION__);
      free(private->ssl_crl_data);
      private->ssl_crl_data = NULL;
   }

   /* SSL Specific shutdown */
   if (private->ssl_obj != NULL) {
      int back = 0;
      cl_com_ssl_func__SSL_set_quiet_shutdown(private->ssl_obj, 1);
      back = cl_com_ssl_func__SSL_shutdown(private->ssl_obj);
      if (back != 1) {
         CL_LOG_INT(CL_LOG_WARNING,"SSL shutdown returned:", back);
         cl_com_ssl_log_ssl_errors(__CL_FUNCTION__);
      }
   }
 
   /* clear ssl_obj */
   if (private->ssl_obj != NULL) {
      cl_com_ssl_func__SSL_clear(private->ssl_obj);
   }
      
   /* free ssl_bio_socket */
   if (private->ssl_bio_socket != NULL) {
#if 0
      /* since SSL_set_bio() has associated the bio to the ssl_obj the
      ssl_bio_socket is free at clear or free of ssl_obj */
      /* cl_com_ssl_func__BIO_free(private->ssl_bio_socket); */
#endif
      private->ssl_bio_socket = NULL;
   }

   /* free ssl_obj */
   if (private->ssl_obj != NULL) {
      cl_com_ssl_func__SSL_free(private->ssl_obj);
      private->ssl_obj = NULL;
   }


   /* free ssl_ctx */
   if (private->ssl_ctx != NULL) {
      cl_com_ssl_func__SSL_CTX_free(private->ssl_ctx);
      private->ssl_ctx = NULL;
   }

   /* free ssl_setup */
   if (private->ssl_setup != NULL) {
      cl_com_free_ssl_setup(&(private->ssl_setup));
   }
   cl_com_ssl_log_ssl_errors(__CL_FUNCTION__);

   if (private->ssl_unique_id != NULL) {
      free(private->ssl_unique_id);
      private->ssl_unique_id = NULL;
   }
   /* free struct cl_com_ssl_private_t */
   free(private);
   connection->com_private = NULL;
   return CL_RETVAL_OK;
}

#ifdef __CL_FUNCTION__
#undef __CL_FUNCTION__
#endif
#define __CL_FUNCTION__ "cl_com_ssl_setup_context()"
static int cl_com_ssl_setup_context(cl_com_connection_t* connection, cl_bool_t is_server) {
   cl_com_ssl_private_t* private = NULL;
   int ret_val = CL_RETVAL_OK;
   if (connection == NULL) {
      return CL_RETVAL_PARAMS;
   }

   private = cl_com_ssl_get_private(connection);
   if (private == NULL) {
      return CL_RETVAL_NO_FRAMEWORK_INIT;
   }


   if (private->ssl_ctx == NULL) {
      switch(private->ssl_setup->ssl_method) {
         case CL_SSL_v23:
            CL_LOG(CL_LOG_INFO,"creating ctx with SSLv23_method()");
            private->ssl_ctx = cl_com_ssl_func__SSL_CTX_new(cl_com_ssl_func__SSLv23_method());
            break;
      }
      if (private->ssl_ctx == NULL) {
         return CL_RETVAL_SSL_COULD_NOT_CREATE_CONTEXT;
      }
      /* now set specific modes */
      ret_val = cl_com_ssl_set_default_mode(private->ssl_ctx, NULL);
      if (ret_val != CL_RETVAL_OK) {
         cl_com_ssl_log_ssl_errors(__CL_FUNCTION__);
         return ret_val;
      }

   }

   if (is_server == CL_FALSE) {
      CL_LOG(CL_LOG_INFO, "setting up context as client");
   } else {
      CL_LOG(CL_LOG_INFO, "setting up context as server");
      
      /* set private structure pointer into SSL_CTX for later retrieval from cl_com_ssl_verify_callback */
      CL_LOG(CL_LOG_INFO, "storing ssl private object into ssl ctx object");
      cl_com_ssl_func__SSL_CTX_set_app_data(private->ssl_ctx, (void*)private);

      CL_LOG(CL_LOG_INFO, "setting peer verify mode for clients");
      cl_com_ssl_func__SSL_CTX_set_verify(private->ssl_ctx,
                                          SSL_VERIFY_PEER|SSL_VERIFY_FAIL_IF_NO_PEER_CERT,
                                          cl_com_ssl_verify_callback);
   }

#if 1
   if (private->ssl_setup->ssl_cert_mode == CL_SSL_PEM_BYTE) {
      BIO *mem = NULL;
      X509 *cert = NULL;
      PKCS8_PRIV_KEY_INFO *p8inf = NULL;
      EVP_PKEY *pkey = NULL;
      char *cn = NULL;
      /* set certificate file */
      if (private->ssl_setup->ssl_cert_pem_file != NULL) {
         mem = cl_com_ssl_func__BIO_new_mem_buf(private->ssl_setup->ssl_cert_pem_file, strlen(private->ssl_setup->ssl_cert_pem_file));
         cert = cl_com_ssl_func__PEM_read_bio_X509(mem, NULL, NULL, NULL);
         cl_com_ssl_func__BIO_free(mem);
         if ((cert == NULL) || (cl_com_ssl_func__SSL_CTX_use_certificate(private->ssl_ctx, cert) != 1)) {
            unsigned long ssl_error = cl_com_ssl_func__ERR_get_error();
            char buffer[BUFSIZ];
            cl_com_ssl_func__ERR_error_string_n(ssl_error, buffer, sizeof(buffer)-1);
            CL_LOG_STR(CL_LOG_ERROR,"failed to set ssl_cert:", buffer);
            cl_commlib_push_application_error(CL_LOG_ERROR, CL_RETVAL_SSL_CANT_SET_CERT_PEM_BYTE, "failed to set ssl_cert");
            cl_com_ssl_log_ssl_errors(__CL_FUNCTION__);
            return CL_RETVAL_SSL_CANT_SET_CERT_PEM_BYTE;
         }
         cn = cl_com_ssl_func__X509_NAME_oneline(cl_com_ssl_func__X509_get_subject_name(cert), NULL, 0);
         CL_LOG_STR(CL_LOG_INFO,"ssl_cert:", cn);
         if (cert != NULL) {
            cl_com_ssl_func__X509_free(cert);
         }   
         if (cn != NULL) {
            cl_com_ssl_func__OPENSSL_free(cn);
         }
      } else {
         CL_LOG_STR(CL_LOG_INFO,"ssl_cert:", "is NULL");
         cl_commlib_push_application_error(CL_LOG_ERROR, CL_RETVAL_SSL_SET_CERT_PEM_BYTE_IS_NULL, "cert is NULL");
         cl_com_ssl_log_ssl_errors(__CL_FUNCTION__);
         return CL_RETVAL_SSL_SET_CERT_PEM_BYTE_IS_NULL;
      }

      /* load CA file from file */
      if (cl_com_ssl_func__SSL_CTX_load_verify_locations(private->ssl_ctx, 
                                         private->ssl_setup->ssl_CA_cert_pem_file, 
                                         NULL) != 1) {

         CL_LOG(CL_LOG_ERROR,"can't read trusted CA certificates file(s)");
         cl_commlib_push_application_error(CL_LOG_ERROR, CL_RETVAL_SSL_CANT_READ_CA_LIST, private->ssl_setup->ssl_CA_cert_pem_file );
         cl_com_ssl_log_ssl_errors(__CL_FUNCTION__);
         return CL_RETVAL_SSL_CANT_READ_CA_LIST;
      }
      CL_LOG_STR(CL_LOG_INFO,"ssl_CA_cert_pem_file:", private->ssl_setup->ssl_CA_cert_pem_file);

      /* set private key (private key comes from java in PKCS8 pem format */
      if (private->ssl_setup->ssl_key_pem_file != NULL) {
         mem = cl_com_ssl_func__BIO_new_mem_buf(private->ssl_setup->ssl_key_pem_file, strlen(private->ssl_setup->ssl_key_pem_file));
         p8inf = cl_com_ssl_func__PEM_read_bio_PKCS8_PRIV_KEY_INFO(mem, NULL, NULL, NULL);
         pkey = cl_com_ssl_func__EVP_PKCS82PKEY(p8inf);
         cl_com_ssl_func__BIO_free(mem);
         if ((pkey == NULL) || (cl_com_ssl_func__SSL_CTX_use_PrivateKey(private->ssl_ctx, pkey) != 1)) {
            unsigned long ssl_error = cl_com_ssl_func__ERR_get_error();
            char buffer[BUFSIZ];
            cl_com_ssl_func__ERR_error_string_n(ssl_error, buffer, sizeof(buffer)-1);
            CL_LOG_STR(CL_LOG_ERROR,"failed to set ssl_key_pem_bytes:", buffer);
            cl_commlib_push_application_error(CL_LOG_ERROR, CL_RETVAL_SSL_CANT_SET_KEY_PEM_BYTE, private->ssl_setup->ssl_key_pem_file);
            cl_com_ssl_log_ssl_errors(__CL_FUNCTION__);
            return CL_RETVAL_SSL_CANT_SET_KEY_PEM_BYTE;
         }
         CL_LOG_STR(CL_LOG_INFO,"ssl_key_pem_file:", private->ssl_setup->ssl_key_pem_file);
         if (pkey != NULL) {
            cl_com_ssl_func__EVP_PKEY_free(pkey);
         }   
      } else {
         CL_LOG_STR(CL_LOG_INFO,"private key:", "is NULL");
         cl_commlib_push_application_error(CL_LOG_ERROR, CL_RETVAL_SSL_CANT_SET_KEY_PEM_BYTE, "private key is NULL");
         cl_com_ssl_log_ssl_errors(__CL_FUNCTION__);
         return CL_RETVAL_SSL_CANT_SET_KEY_PEM_BYTE;
      }
   } else 
#endif
   { 
      /* load certificate chain file */
      if (cl_com_ssl_func__SSL_CTX_use_certificate_chain_file(private->ssl_ctx, private->ssl_setup->ssl_cert_pem_file) != 1) {
         CL_LOG_STR(CL_LOG_ERROR,"failed to set ssl_cert_pem_file:", private->ssl_setup->ssl_cert_pem_file);
         cl_commlib_push_application_error(CL_LOG_ERROR, CL_RETVAL_SSL_COULD_NOT_SET_CA_CHAIN_FILE, private->ssl_setup->ssl_cert_pem_file);
         cl_com_ssl_log_ssl_errors(__CL_FUNCTION__);
         return CL_RETVAL_SSL_COULD_NOT_SET_CA_CHAIN_FILE;
      }
      CL_LOG_STR(CL_LOG_INFO,"ssl_cert_pem_file:", private->ssl_setup->ssl_cert_pem_file);

      if (cl_com_ssl_func__SSL_CTX_load_verify_locations( private->ssl_ctx, 
                                         private->ssl_setup->ssl_CA_cert_pem_file, 
                                         NULL) != 1) {

         CL_LOG(CL_LOG_ERROR,"can't read trusted CA certificates file(s)");
         cl_commlib_push_application_error(CL_LOG_ERROR, CL_RETVAL_SSL_CANT_READ_CA_LIST, private->ssl_setup->ssl_CA_cert_pem_file );
         cl_com_ssl_log_ssl_errors(__CL_FUNCTION__);
         return CL_RETVAL_SSL_CANT_READ_CA_LIST;
      }
      CL_LOG_STR(CL_LOG_INFO,"ssl_CA_cert_pem_file:", private->ssl_setup->ssl_CA_cert_pem_file);

      /* load private key */
      if (cl_com_ssl_func__SSL_CTX_use_PrivateKey_file(private->ssl_ctx, private->ssl_setup->ssl_key_pem_file, SSL_FILETYPE_PEM) != 1) {
         CL_LOG_STR(CL_LOG_ERROR,"failed to set ssl_key_pem_file:", private->ssl_setup->ssl_key_pem_file);
         cl_commlib_push_application_error(CL_LOG_ERROR, CL_RETVAL_SSL_CANT_SET_CA_KEY_PEM_FILE, private->ssl_setup->ssl_key_pem_file);
         cl_com_ssl_log_ssl_errors(__CL_FUNCTION__);
         return CL_RETVAL_SSL_CANT_SET_CA_KEY_PEM_FILE;
      }
      CL_LOG_STR(CL_LOG_INFO,"ssl_key_pem_file:", private->ssl_setup->ssl_key_pem_file);
   }

   return CL_RETVAL_OK;
}



#ifdef __CL_FUNCTION__
#undef __CL_FUNCTION__
#endif
#define __CL_FUNCTION__ "cl_com_ssl_get_private()"
static cl_com_ssl_private_t* cl_com_ssl_get_private(cl_com_connection_t* connection) {
   if (connection != NULL) {
      return (cl_com_ssl_private_t*) connection->com_private;
   }
   return NULL;
}

#ifdef __CL_FUNCTION__
#undef __CL_FUNCTION__
#endif
#define __CL_FUNCTION__ "cl_com_ssl_framework_setup()"
int cl_com_ssl_framework_setup(void) {
   int ret_val = CL_RETVAL_OK;
   pthread_mutex_lock(&cl_com_ssl_global_config_mutex);
   if (cl_com_ssl_global_config_object == NULL) {
      cl_com_ssl_global_config_object = (cl_com_ssl_global_t*) malloc(sizeof(cl_com_ssl_global_t));
      if (cl_com_ssl_global_config_object == NULL) {
         ret_val = CL_RETVAL_MALLOC;
      } else {
         cl_com_ssl_global_config_object->ssl_initialized = CL_FALSE;
         cl_com_ssl_global_config_object->ssl_lib_lock_mutex_array = NULL;
         cl_com_ssl_global_config_object->ssl_lib_lock_num = 0;
      }
   }
   pthread_mutex_unlock(&cl_com_ssl_global_config_mutex);
   CL_LOG(CL_LOG_INFO,"ssl framework configuration object setup done");
   return ret_val;
}

#ifdef __CL_FUNCTION__
#undef __CL_FUNCTION__
#endif
#define __CL_FUNCTION__ "cl_com_ssl_framework_cleanup()"
int cl_com_ssl_framework_cleanup(void) {
   int ret_val = CL_RETVAL_OK;
   int counter = 0;
   pthread_mutex_lock(&cl_com_ssl_global_config_mutex);
   if (cl_com_ssl_global_config_object != NULL) {
      if (cl_com_ssl_global_config_object->ssl_initialized == CL_TRUE) {

         CL_LOG(CL_LOG_INFO,"shutting down ssl framework ...");
         /* free error strings from ERR_load_crypto_strings() 
            and/or SSL_load_error_strings() */

         
         cl_com_ssl_func__CRYPTO_set_locking_callback(NULL);
         cl_com_ssl_func__CRYPTO_set_id_callback(NULL);

         cl_com_ssl_func__ERR_free_strings();
         cl_com_ssl_destroy_symbol_table();

         /* destroy ssl mutexes */
         CL_LOG(CL_LOG_INFO,"destroying ssl mutexes");
         for (counter=0; counter<cl_com_ssl_global_config_object->ssl_lib_lock_num; counter++) {
            pthread_mutex_destroy(&(cl_com_ssl_global_config_object->ssl_lib_lock_mutex_array[counter]));
         }

         /* free mutex array */
         CL_LOG(CL_LOG_INFO,"free mutex array");
         if (cl_com_ssl_global_config_object->ssl_lib_lock_mutex_array != NULL) {
            free(cl_com_ssl_global_config_object->ssl_lib_lock_mutex_array);
         }

         /* free config object */
         CL_LOG(CL_LOG_INFO,"free ssl configuration object");

         free(cl_com_ssl_global_config_object);
         cl_com_ssl_global_config_object = NULL;

         CL_LOG(CL_LOG_INFO,"shutting down ssl framework done");
      } else {
         CL_LOG(CL_LOG_INFO,"ssl was not initialized");
         /* free config object */
         CL_LOG(CL_LOG_INFO,"free ssl configuration object");

         free(cl_com_ssl_global_config_object);
         cl_com_ssl_global_config_object = NULL;

         ret_val = CL_RETVAL_OK;
      }
   } else {
      CL_LOG(CL_LOG_ERROR,"ssl config object not initialized");
      ret_val = CL_RETVAL_NO_FRAMEWORK_INIT;
   }
   pthread_mutex_unlock(&cl_com_ssl_global_config_mutex);
   CL_LOG(CL_LOG_INFO,"ssl framework cleanup done");

   return ret_val;
}



#ifdef __CL_FUNCTION__
#undef __CL_FUNCTION__
#endif
#define __CL_FUNCTION__ "cl_dump_ssl_private()"
void cl_dump_ssl_private(cl_com_connection_t* connection) {

   cl_com_ssl_private_t* private = NULL;
   if (connection == NULL) {
      CL_LOG(CL_LOG_DEBUG, "connection is NULL");
   } else {
      if ( (private=cl_com_ssl_get_private(connection)) != NULL) {
         CL_LOG_INT(CL_LOG_DEBUG,"server port:   ",private->server_port);
         CL_LOG_INT(CL_LOG_DEBUG,"connect_port:  ",private->connect_port);
         CL_LOG_INT(CL_LOG_DEBUG,"socked fd:     ",private->sockfd);
         CL_LOG_INT(CL_LOG_DEBUG,"ssl_last_error:",private->ssl_last_error);
         if (private->ssl_ctx == NULL) {
            CL_LOG_STR(CL_LOG_DEBUG,"ssl_ctx:       ", "n.a.");
         } else {
            CL_LOG_STR(CL_LOG_DEBUG,"ssl_ctx:       ", "initialized");
         }
         if (private->ssl_obj == NULL) {
            CL_LOG_STR(CL_LOG_DEBUG,"ssl_obj:       ", "n.a.");
         } else {
            CL_LOG_STR(CL_LOG_DEBUG,"ssl_obj:       ", "initialized");
         }
         if (private->ssl_bio_socket == NULL) {
            CL_LOG_STR(CL_LOG_DEBUG,"ssl_bio_socket:", "n.a.");
         } else {
            CL_LOG_STR(CL_LOG_DEBUG,"ssl_bio_socket:", "initialized");
         }
         if (private->ssl_setup == NULL) {
            CL_LOG_STR(CL_LOG_DEBUG,"ssl_setup:     ", "n.a.");
         } else {
            CL_LOG_STR(CL_LOG_DEBUG,"ssl_setup:     ", "initialized");
         }
         if (private->ssl_unique_id == NULL) {
            CL_LOG_STR(CL_LOG_DEBUG,"ssl_unique_id: ", "n.a.");
         } else {
            CL_LOG_STR(CL_LOG_DEBUG,"ssl_unique_id: ", private->ssl_unique_id);
         }
      }
   }
}


#ifdef __CL_FUNCTION__
#undef __CL_FUNCTION__
#endif
#define __CL_FUNCTION__ "cl_com_ssl_get_connect_port()"
int cl_com_ssl_get_connect_port(cl_com_connection_t* connection, int* port) {
   cl_com_ssl_private_t* private = NULL;

   if (connection == NULL || port == NULL ) {
      return CL_RETVAL_PARAMS;
   }
   if ( (private=cl_com_ssl_get_private(connection)) != NULL) {
      *port = private->connect_port;
      return CL_RETVAL_OK;
   }
   return CL_RETVAL_UNKNOWN;
}

#ifdef __CL_FUNCTION__
#undef __CL_FUNCTION__
#endif
#define __CL_FUNCTION__ "cl_com_ssl_get_fd()"
int cl_com_ssl_get_fd(cl_com_connection_t* connection, int* fd) {
   cl_com_ssl_private_t* private = NULL;

   if (connection == NULL || fd == NULL ) {
      return CL_RETVAL_PARAMS;
   }
   if ( (private=cl_com_ssl_get_private(connection)) != NULL) {
      if (private->sockfd < 0) {
         *fd = private->pre_sockfd;
      } else {
         *fd = private->sockfd;
      }
      return CL_RETVAL_OK;
   }
   return CL_RETVAL_UNKNOWN;
}

#ifdef __CL_FUNCTION__
#undef __CL_FUNCTION__
#endif
#define __CL_FUNCTION__ "cl_com_ssl_set_connect_port()"
int cl_com_ssl_set_connect_port(cl_com_connection_t* connection, int port) {

   cl_com_ssl_private_t* private = NULL;
   if (connection == NULL) {
      return CL_RETVAL_PARAMS;
   }
   if ( (private=cl_com_ssl_get_private(connection)) != NULL) {
      private->connect_port = port;
      return CL_RETVAL_OK;
   }
   return CL_RETVAL_UNKNOWN;
}

#ifdef __CL_FUNCTION__
#undef __CL_FUNCTION__
#endif
#define __CL_FUNCTION__ "cl_com_ssl_get_service_port()"
int cl_com_ssl_get_service_port(cl_com_connection_t* connection, int* port) {
   cl_com_ssl_private_t* private = NULL;

   if (connection == NULL || port == NULL ) {
      return CL_RETVAL_PARAMS;
   }

   if ( (private=cl_com_ssl_get_private(connection)) != NULL) {
      *port = private->server_port;
      return CL_RETVAL_OK;
   }
   return CL_RETVAL_UNKNOWN;
}

#ifdef __CL_FUNCTION__
#undef __CL_FUNCTION__
#endif
#define __CL_FUNCTION__ "cl_com_ssl_get_client_socket_in_port()"
int cl_com_ssl_get_client_socket_in_port(cl_com_connection_t* connection, int* port) {
   cl_com_ssl_private_t* private = NULL;
   if (connection == NULL || port == NULL ) {
      return CL_RETVAL_PARAMS;
   }

   if ( (private=cl_com_ssl_get_private(connection)) != NULL) {
      *port = private->connect_in_port;
      return CL_RETVAL_OK;
   }
   return CL_RETVAL_UNKNOWN;
}



#ifdef __CL_FUNCTION__
#undef __CL_FUNCTION__
#endif
#define __CL_FUNCTION__ "cl_com_ssl_setup_connection()"
int cl_com_ssl_setup_connection(cl_com_connection_t**          connection, 
                                int                            server_port,
                                int                            connect_port,
                                cl_xml_connection_type_t       data_flow_type,
                                cl_xml_connection_autoclose_t  auto_close_mode,
                                cl_framework_t                 framework_type,
                                cl_xml_data_format_t           data_format_type,
                                cl_tcp_connect_t               tcp_connect_mode,
                                cl_ssl_setup_t*                ssl_setup) {
   
   cl_com_ssl_private_t* com_private = NULL;
   int ret_val;
   int counter;

   if (connection == NULL) {
      return CL_RETVAL_PARAMS;
   }
   if (ssl_setup == NULL) {
      CL_LOG(CL_LOG_ERROR,"no ssl setup parameter specified");
      return CL_RETVAL_PARAMS;
   }

   if (*connection != NULL) {
      return CL_RETVAL_PARAMS;
   }

   if (data_flow_type != CL_CM_CT_STREAM && data_flow_type != CL_CM_CT_MESSAGE) {
      return CL_RETVAL_PARAMS;
   }

   /* create new connection */
   if ( (ret_val=cl_com_create_connection(connection)) != CL_RETVAL_OK) {
      return ret_val;
   }

   /* check for correct framework specification */
   switch(framework_type) {
      case CL_CT_SSL:
         break;
      case CL_CT_UNDEFINED:
      case CL_CT_TCP: {
         CL_LOG_STR(CL_LOG_ERROR,"unexpected framework:", cl_com_get_framework_type(*connection));
         cl_com_close_connection(connection);
         return CL_RETVAL_WRONG_FRAMEWORK;
      }
   }

   /* create private data structure */
   com_private = (cl_com_ssl_private_t*) malloc(sizeof(cl_com_ssl_private_t));
   if (com_private == NULL) {
      cl_com_close_connection(connection);
      return CL_RETVAL_MALLOC;
   }
   memset(com_private, 0, sizeof(cl_com_ssl_private_t));


   /* set com_private to com_private pointer */
   (*connection)->com_private = com_private;

   /* set modes */
   (*connection)->auto_close_type = auto_close_mode;
   (*connection)->data_flow_type = data_flow_type;
   (*connection)->connection_type = CL_COM_SEND_RECEIVE;
   (*connection)->framework_type = framework_type;
   (*connection)->data_format_type = data_format_type;
   (*connection)->tcp_connect_mode = tcp_connect_mode;


   /* setup ssl private struct */
   com_private->sockfd = -1;
   com_private->pre_sockfd = -1;
   com_private->server_port = server_port;
   com_private->connect_port = connect_port;

   /* check ssl setup, setup ssl if neccessary  */
   pthread_mutex_lock(&cl_com_ssl_global_config_mutex);
   /* check if cl_com_ssl_framework_setup() was called */
   if (cl_com_ssl_global_config_object == NULL) {
      pthread_mutex_unlock(&cl_com_ssl_global_config_mutex);
      cl_com_close_connection(connection);
      CL_LOG(CL_LOG_ERROR,"cl_com_ssl_framework_setup() not called");
      return CL_RETVAL_NO_FRAMEWORK_INIT;
   } else {
      /* check if we have already initalized the global ssl functionality */
      if (cl_com_ssl_global_config_object->ssl_initialized == CL_FALSE) {
         /* init ssl lib */
         CL_LOG(CL_LOG_INFO, "init ssl library ...");
         
         /* first load function table */
         if ( cl_com_ssl_build_symbol_table() != CL_RETVAL_OK) {
            CL_LOG(CL_LOG_ERROR,"can't build crypto symbol table");
            pthread_mutex_unlock(&cl_com_ssl_global_config_mutex);
            cl_com_close_connection(connection);
            return CL_RETVAL_NO_FRAMEWORK_INIT;
         }

         /* setup ssl error strings */
         cl_com_ssl_func__SSL_load_error_strings();

         /* init lib */
         cl_com_ssl_func__SSL_library_init();


         /* use -lcrypto threads(3) interface here to allow OpenSSL 
            safely be used by multiple threads */
         cl_com_ssl_global_config_object->ssl_lib_lock_num = cl_com_ssl_func__CRYPTO_num_locks();
         CL_LOG_INT(CL_LOG_INFO,"   ssl lib mutex malloc count:", 
                    cl_com_ssl_global_config_object->ssl_lib_lock_num);

         cl_com_ssl_global_config_object->ssl_lib_lock_mutex_array = 
            malloc(cl_com_ssl_global_config_object->ssl_lib_lock_num * sizeof(pthread_mutex_t));

         if (cl_com_ssl_global_config_object->ssl_lib_lock_mutex_array == NULL) {
            CL_LOG(CL_LOG_ERROR,"can't malloc ssl library mutex array");
            pthread_mutex_unlock(&cl_com_ssl_global_config_mutex);
            cl_com_close_connection(connection);
            return CL_RETVAL_MALLOC;
         }

         for (counter=0; counter<cl_com_ssl_global_config_object->ssl_lib_lock_num; counter++) {
            if ( pthread_mutex_init(&(cl_com_ssl_global_config_object->ssl_lib_lock_mutex_array[counter]), NULL) != 0 ) {
               CL_LOG(CL_LOG_ERROR,"can't setup mutex for ssl library mutex array");
               pthread_mutex_unlock(&cl_com_ssl_global_config_mutex);
               cl_com_close_connection(connection);
               return CL_RETVAL_MUTEX_ERROR;
            } 
         }

         /* structures are freed at cl_com_ssl_framework_cleanup() */
         cl_com_ssl_func__CRYPTO_set_id_callback(cl_com_ssl_get_thread_id);
         cl_com_ssl_func__CRYPTO_set_locking_callback(cl_com_ssl_locking_callback);

         /* 
          * SSL_library_init() only registers ciphers. Another important
          * initialization is the seeding of the PRNG (Pseudo Random
          * Number Generator), which has to be performed separately.
          */

         if (cl_com_ssl_func__RAND_status() != 1) {
            CL_LOG(CL_LOG_INFO, "PRNG is not seeded with enough data, reading RAND file ...");
            if ( ssl_setup->ssl_rand_file != NULL) {
               int bytes_read;

               /*
                * try to read the complete rand file
                */
               bytes_read = cl_com_ssl_func__RAND_load_file(ssl_setup->ssl_rand_file, -1);
               CL_LOG_STR(CL_LOG_INFO, "using RAND file:", ssl_setup->ssl_rand_file);
               CL_LOG_INT(CL_LOG_INFO, "nr of RAND bytes read:", bytes_read);
            } else {
               CL_LOG(CL_LOG_ERROR, "need RAND file, but there is no RAND file specified");
            }

            /* 
             * check RAND status again and return error if there is still 
             * not enough RAND data
             */
            if (cl_com_ssl_func__RAND_status() != 1) {
               CL_LOG(CL_LOG_ERROR, "couldn't setup PRNG with enough data" );
               pthread_mutex_unlock(&cl_com_ssl_global_config_mutex);
               cl_com_close_connection(connection);
               cl_commlib_push_application_error(CL_LOG_ERROR, CL_RETVAL_SSL_RAND_SEED_FAILURE, "error reading RAND data file" );
               return CL_RETVAL_SSL_RAND_SEED_FAILURE;
            }
         } else {
            CL_LOG(CL_LOG_INFO, "PRNG is seeded with enough data");
         }

         cl_com_ssl_global_config_object->ssl_initialized = CL_TRUE;

         CL_LOG(CL_LOG_INFO, "init ssl library done");
      } else {
         CL_LOG(CL_LOG_INFO,"ssl library already initalized");
      }
   }
   pthread_mutex_unlock(&cl_com_ssl_global_config_mutex);

   /* create context object */
  
   /* ssl_ctx */
   com_private->ssl_ctx   = NULL;   /* created in cl_com_ssl_setup_context() */

   /* ssl_obj */
   com_private->ssl_obj   = NULL;

   /* ssl_bio_socket */
   com_private->ssl_bio_socket = NULL;

   /* ssl_setup */
   com_private->ssl_setup = NULL;
   if ( (ret_val = cl_com_dup_ssl_setup(&(com_private->ssl_setup),ssl_setup)) != CL_RETVAL_OK) {
      cl_com_close_connection(connection);
      return ret_val;
   } 

   /* ssl_crl_data */
   com_private->ssl_crl_data = (cl_ssl_verify_crl_data_t*) malloc(sizeof(cl_ssl_verify_crl_data_t));
   if (com_private->ssl_crl_data == NULL) {
      cl_com_close_connection(connection);
      return CL_RETVAL_MALLOC;
   }
   memset(com_private->ssl_crl_data, 0, sizeof(cl_ssl_verify_crl_data_t));
   
#ifdef CL_COM_ENABLE_SSL_THREAD_RETRY_BUGFIX
   CL_LOG(CL_LOG_WARNING,"ignoring SSL_ERROR_SYSCALL for this platform!");
#endif
   return CL_RETVAL_OK;
}

#ifdef __CL_FUNCTION__
#undef __CL_FUNCTION__
#endif
#define __CL_FUNCTION__ "cl_com_ssl_close_connection()"
int cl_com_ssl_close_connection(cl_com_connection_t** connection) {
   cl_com_ssl_private_t* private = NULL;
   int sock_fd = -1;
   int ret_val = CL_RETVAL_OK;
   
   if (connection == NULL) {
      return CL_RETVAL_PARAMS;
   }
   if (*connection == NULL) {
      return CL_RETVAL_PARAMS;
   }

   private = cl_com_ssl_get_private(*connection);

   if (private == NULL) {
      return CL_RETVAL_NO_FRAMEWORK_INIT;
   }

   /* save socket fd */
   sock_fd = private->sockfd;

   /* free com private structure (shutdown of ssl)*/
   ret_val = cl_com_ssl_free_com_private(*connection);
   
   /* shutdown socket fd (after ssl shutdown) */
   if (sock_fd >= 0) {
      /* shutdown socket connection */
      shutdown(sock_fd, 2);
      close(sock_fd);
   }
   return ret_val;
}


#ifdef __CL_FUNCTION__
#undef __CL_FUNCTION__
#endif
#define __CL_FUNCTION__ "cl_com_ssl_connection_complete_shutdown()"
int cl_com_ssl_connection_complete_shutdown(cl_com_connection_t*  connection) {
   cl_com_ssl_private_t* private = NULL;
   int back = 0;
   int ssl_error;

   if (connection == NULL) {
      return CL_RETVAL_PARAMS;
   }

   private = cl_com_ssl_get_private(connection);
   if (private == NULL) {
      return CL_RETVAL_NO_FRAMEWORK_INIT;
   }

   /* SSL Specific shutdown */
   if (private->ssl_obj != NULL) {
      back = cl_com_ssl_func__SSL_shutdown(private->ssl_obj);
      if (back == 1) {
         return CL_RETVAL_OK;
      }

      if (back == 0) {
         return CL_RETVAL_UNCOMPLETE_READ;
      }
     
      ssl_error = cl_com_ssl_func__SSL_get_error(private->ssl_obj, back);
      private->ssl_last_error = ssl_error;
      CL_LOG_STR(CL_LOG_INFO,"ssl_error:", cl_com_ssl_get_error_text(ssl_error));
      switch(ssl_error) {
         case SSL_ERROR_WANT_READ:  {
            return CL_RETVAL_UNCOMPLETE_READ;
         }
         case SSL_ERROR_WANT_WRITE: {
            return CL_RETVAL_UNCOMPLETE_WRITE;
         }
#ifdef CL_COM_ENABLE_SSL_THREAD_RETRY_BUGFIX
         case SSL_ERROR_SYSCALL: {
            CL_LOG(CL_LOG_ERROR,"SSL_ERROR_SYSCALL error");
            return CL_RETVAL_UNCOMPLETE_READ;
         }
#endif
         default: {
            CL_LOG(CL_LOG_ERROR,"SSL shutdown error");
            cl_com_ssl_log_ssl_errors(__CL_FUNCTION__);
            return CL_RETVAL_SSL_SHUTDOWN_ERROR;
         }
      }
   }
   return CL_RETVAL_OK;
}

#ifdef __CL_FUNCTION__
#undef __CL_FUNCTION__
#endif
#define __CL_FUNCTION__ "cl_com_ssl_connection_complete_accept()"
int cl_com_ssl_connection_complete_accept(cl_com_connection_t*  connection,
                                          long                  timeout) {

   cl_com_ssl_private_t* private = NULL;
   cl_com_ssl_private_t* service_private = NULL;
   struct timeval now;
   int ret_val = CL_RETVAL_OK;
   char tmp_buffer[1024];


   if (connection == NULL) {
      return CL_RETVAL_PARAMS;
   }

   private = cl_com_ssl_get_private(connection);
   if (private == NULL) {
      return CL_RETVAL_NO_FRAMEWORK_INIT;
   }

   if (connection->handler == NULL) {
      CL_LOG(CL_LOG_ERROR,"This conneciton has no handler");
      return CL_RETVAL_PARAMS;
   }
  
   if (connection->handler->service_handler == NULL) {
      CL_LOG(CL_LOG_ERROR,"The connection handler has no service handler");
      return CL_RETVAL_PARAMS;
   }

   service_private = cl_com_ssl_get_private(connection->handler->service_handler);
   if (service_private == NULL) {
      CL_LOG(CL_LOG_ERROR,"The connection handler has not setup his private connection data");
      return CL_RETVAL_PARAMS;
   }

   if (connection->was_accepted != CL_TRUE) {
      CL_LOG(CL_LOG_ERROR,"This is not an accepted connection from service (was_accepted flag is not set)");
      return CL_RETVAL_PARAMS;
   }

   if ( connection->connection_state != CL_ACCEPTING) {
      CL_LOG(CL_LOG_ERROR,"state is not CL_ACCEPTING - return connect error");
      return CL_RETVAL_UNKNOWN;   
   }

   CL_LOG_STR(CL_LOG_INFO,"connection state:", cl_com_get_connection_state(connection));
   if ( connection->connection_sub_state == CL_COM_ACCEPT_INIT) {
      CL_LOG_STR(CL_LOG_INFO,"connection sub state:", cl_com_get_connection_sub_state(connection));
   
      /* setup new ssl_obj with ctx from service connection */
      private->ssl_obj = cl_com_ssl_func__SSL_new(service_private->ssl_ctx);
      if (private->ssl_obj == NULL) {
         cl_com_ssl_log_ssl_errors(__CL_FUNCTION__);
         cl_commlib_push_application_error(CL_LOG_ERROR, CL_RETVAL_SSL_CANT_CREATE_SSL_OBJECT, NULL);
         CL_LOG(CL_LOG_ERROR,"can't setup ssl object");
         return CL_RETVAL_SSL_CANT_CREATE_SSL_OBJECT;
      }

      /* set default modes */
      ret_val = cl_com_ssl_set_default_mode(NULL, private->ssl_obj);
      if (ret_val != CL_RETVAL_OK) {
         cl_commlib_push_application_error(CL_LOG_ERROR, ret_val, NULL);
         cl_com_ssl_log_ssl_errors(__CL_FUNCTION__);
         return ret_val;
      }

      /* create a new ssl bio socket associated with the connected tcp connection */
      private->ssl_bio_socket = cl_com_ssl_func__BIO_new_socket(private->sockfd, BIO_NOCLOSE);
      if (private->ssl_bio_socket == NULL) {
         cl_com_ssl_log_ssl_errors(__CL_FUNCTION__);
         cl_commlib_push_application_error(CL_LOG_ERROR, CL_RETVAL_SSL_CANT_CREATE_BIO_SOCKET, NULL);
         CL_LOG(CL_LOG_ERROR,"can't setup bio socket");
         return CL_RETVAL_SSL_CANT_CREATE_BIO_SOCKET;
      }
   
      /* connect the SSL object with the BIO (the same BIO is used for read/write) */
      cl_com_ssl_func__SSL_set_bio(private->ssl_obj, private->ssl_bio_socket, private->ssl_bio_socket);

      gettimeofday(&now,NULL);
      connection->write_buffer_timeout_time = now.tv_sec + timeout;
      connection->connection_sub_state = CL_COM_ACCEPT;
   }

   if ( connection->connection_sub_state == CL_COM_ACCEPT) {
      int ssl_accept_back;
      int ssl_error;
      CL_LOG_STR(CL_LOG_INFO,"connection sub state:", cl_com_get_connection_sub_state(connection));
      
      ssl_accept_back = cl_com_ssl_func__SSL_accept(private->ssl_obj);

      if (ssl_accept_back != 1) {
#if 0
         if (ssl_accept_back == 0) {
            /* 
             * TLS/SSL handshake was not successful but was shutdown controlled 
             * reason -> check SSL_get_error()
             */
         }
#endif

         /* Try to find out more about the error and set save error in private object */
         ssl_error = cl_com_ssl_func__SSL_get_error(private->ssl_obj, ssl_accept_back);
         CL_LOG_STR(CL_LOG_INFO,"ssl_error:", cl_com_ssl_get_error_text(ssl_error));
         private->ssl_last_error = ssl_error;

         switch(ssl_error) {
            case SSL_ERROR_WANT_READ:
            case SSL_ERROR_WANT_WRITE:
            case SSL_ERROR_WANT_ACCEPT:
#ifdef CL_COM_ENABLE_SSL_THREAD_RETRY_BUGFIX
            case SSL_ERROR_SYSCALL:
#endif
            {
               gettimeofday(&now,NULL);
               if (connection->write_buffer_timeout_time <= now.tv_sec || 
                   cl_com_get_ignore_timeouts_flag()     == CL_TRUE       ) {

                  /* we had an timeout */
                  CL_LOG(CL_LOG_ERROR,"ssl accept timeout error");
                  connection->write_buffer_timeout_time = 0;

                  if (connection->client_host_name != NULL) {
                     snprintf(tmp_buffer,1024, MSG_CL_COMMLIB_SSL_ACCEPT_TIMEOUT_ERROR_S, connection->client_host_name);
                  } else {
                     sge_strlcpy(tmp_buffer,MSG_CL_COMMLIB_SSL_ACCEPT_TIMEOUT_ERROR, 1024);
                  }

                  cl_commlib_push_application_error(CL_LOG_ERROR, CL_RETVAL_SSL_ACCEPT_HANDSHAKE_TIMEOUT, tmp_buffer);
                  return CL_RETVAL_SSL_ACCEPT_HANDSHAKE_TIMEOUT;
               }

               return CL_RETVAL_UNCOMPLETE_WRITE;
            }

            default: {
               CL_LOG(CL_LOG_ERROR,"SSL handshake not successful and no clear cleanup");
               if (connection->client_host_name != NULL) {
                  snprintf(tmp_buffer, 1024, MSG_CL_COMMLIB_SSL_ACCEPT_ERROR_S, connection->client_host_name);
               } else {
                  sge_strlcpy(tmp_buffer, MSG_CL_COMMLIB_SSL_ACCEPT_ERROR, 1024);
               }

               cl_commlib_push_application_error(CL_LOG_ERROR, CL_RETVAL_SSL_ACCEPT_ERROR, tmp_buffer);
               cl_com_ssl_log_ssl_errors(__CL_FUNCTION__);
               break;
            }
         }
         return CL_RETVAL_SSL_ACCEPT_ERROR;
      }

      CL_LOG(CL_LOG_INFO,"SSL Accept successful");
      connection->write_buffer_timeout_time = 0;

      return cl_com_ssl_fill_private_from_peer_cert(private, CL_TRUE);
   }

   return CL_RETVAL_UNKNOWN;
}

#ifdef __CL_FUNCTION__
#undef __CL_FUNCTION__
#endif
#define __CL_FUNCTION__ "cl_com_ssl_open_connection()"
int cl_com_ssl_open_connection(cl_com_connection_t* connection, int timeout) {
   cl_com_ssl_private_t* private = NULL;
   int tmp_error = CL_RETVAL_OK;
   char tmp_buffer[256];


   if (connection == NULL) { 
      return  CL_RETVAL_PARAMS;
   }

   if (connection->remote   == NULL ||
       connection->local    == NULL) {
      return CL_RETVAL_PARAMS;
   }

   private = cl_com_ssl_get_private(connection);
   if (private == NULL) {
      return CL_RETVAL_NO_FRAMEWORK_INIT;
   }

   if ( private->connect_port <= 0 ) {
      CL_LOG(CL_LOG_ERROR, cl_get_error_text(CL_RETVAL_NO_PORT_ERROR));
      return CL_RETVAL_NO_PORT_ERROR; 
   }

   if ( connection->connection_state != CL_OPENING ) {
      CL_LOG(CL_LOG_ERROR,"state is not CL_OPENING - return connect error");
      return CL_RETVAL_CONNECT_ERROR;   
   }

   if ( connection->connection_sub_state == CL_COM_OPEN_INIT) {
      int ret;
      int on = 1;
      char* unique_host = NULL;
      struct timeval now;
      int res_port = IPPORT_RESERVED -1;


      CL_LOG(CL_LOG_DEBUG,"connection_sub_state is CL_COM_OPEN_INIT");
      private->sockfd = -1;
  
      if( (tmp_error=cl_com_ssl_setup_context(connection, CL_FALSE)) != CL_RETVAL_OK) {
         return tmp_error;
      }
      
      switch(connection->tcp_connect_mode) {
         case CL_TCP_DEFAULT: {
            /* create socket */
            if ((private->sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
               CL_LOG(CL_LOG_ERROR,"could not create socket");
               private->sockfd = -1;
               cl_commlib_push_application_error(CL_LOG_ERROR, CL_RETVAL_CREATE_SOCKET, MSG_CL_TCP_FW_SOCKET_ERROR );
               return CL_RETVAL_CREATE_SOCKET;
            }
            break;
         }
         case CL_TCP_RESERVED_PORT: {
            /* create reserved port socket */
            if ((private->sockfd = rresvport(&res_port)) < 0) {
               CL_LOG(CL_LOG_ERROR,"could not create reserved port socket");
               private->sockfd = -1;
               cl_commlib_push_application_error(CL_LOG_ERROR, CL_RETVAL_CREATE_SOCKET, MSG_CL_TCP_FW_RESERVED_SOCKET_ERROR );
               return CL_RETVAL_CREATE_RESERVED_PORT_SOCKET;
            }
            break;
         }
      }
      
      if (private->sockfd < 3) {
         CL_LOG_INT(CL_LOG_WARNING, "The file descriptor is < 3. Will dup fd to be >= 3! fd value: ", private->sockfd);
         ret = sge_dup_fd_above_stderr(&private->sockfd);
         if (ret != 0) {
            CL_LOG_INT(CL_LOG_ERROR, "can't dup socket fd to be >=3, errno = ", ret);
            shutdown(private->sockfd, 2);
            close(private->sockfd);
            private->sockfd = -1;
            cl_commlib_push_application_error(CL_LOG_ERROR, CL_RETVAL_DUP_SOCKET_FD_ERROR, MSG_CL_COMMLIB_CANNOT_DUP_SOCKET_FD);
            return CL_RETVAL_DUP_SOCKET_FD_ERROR;
         }
         CL_LOG_INT(CL_LOG_INFO, "fd value after dup: ", private->sockfd);
      }

#ifndef USE_POLL
      if (private->sockfd >= FD_SETSIZE) {
          CL_LOG(CL_LOG_ERROR,"filedescriptors exeeds FD_SETSIZE of this system");
          shutdown(private->sockfd, 2);
          close(private->sockfd);
          private->sockfd = -1;
          cl_commlib_push_application_error(CL_LOG_ERROR, CL_RETVAL_REACHED_FILEDESCRIPTOR_LIMIT, MSG_CL_COMMLIB_COMPILE_SOURCE_WITH_LARGER_FD_SETSIZE);
          return CL_RETVAL_REACHED_FILEDESCRIPTOR_LIMIT;
      } 
#endif

      /* set local address reuse socket option */
      if ( setsockopt(private->sockfd, SOL_SOCKET, SO_REUSEADDR, (char *) &on, sizeof(on)) != 0) {
         CL_LOG(CL_LOG_ERROR,"could not set SO_REUSEADDR");
         private->sockfd = -1;
         cl_commlib_push_application_error(CL_LOG_ERROR, CL_RETVAL_SETSOCKOPT_ERROR, MSG_CL_TCP_FW_SETSOCKOPT_ERROR);
         return CL_RETVAL_SETSOCKOPT_ERROR;
      }
   
      /* this is a non blocking socket */
      if ( fcntl(private->sockfd, F_SETFL, O_NONBLOCK) != 0) {
         CL_LOG(CL_LOG_ERROR,"could not set O_NONBLOCK");
         private->sockfd = -1;
         cl_commlib_push_application_error(CL_LOG_ERROR, CL_RETVAL_FCNTL_ERROR, MSG_CL_TCP_FW_FCNTL_ERROR);
         return CL_RETVAL_FCNTL_ERROR;
      }


      /* set address  */
      memset((char *) &(private->client_addr), 0, sizeof(struct sockaddr_in));
      private->client_addr.sin_port = htons(private->connect_port);
      private->client_addr.sin_family = AF_INET;
      if ( (tmp_error=cl_com_cached_gethostbyname(connection->remote->comp_host, &unique_host, &(private->client_addr.sin_addr),NULL , NULL)) != CL_RETVAL_OK) {
   
         shutdown(private->sockfd, 2);
         close(private->sockfd);
         free(unique_host);
         CL_LOG(CL_LOG_ERROR,"could not get hostname");
         private->sockfd = -1;
         
         if ( connection != NULL && connection->remote != NULL && connection->remote->comp_host != NULL) {
            snprintf(tmp_buffer,256, MSG_CL_TCP_FW_CANT_RESOLVE_HOST_S, connection->remote->comp_host );
         } else {
            snprintf(tmp_buffer,256, "%s", cl_get_error_text(tmp_error));
         }
         cl_commlib_push_application_error(CL_LOG_ERROR, tmp_error, tmp_buffer);
         return tmp_error; 
      } 
      free(unique_host);

      /* connect */
      gettimeofday(&now,NULL);
      connection->write_buffer_timeout_time = now.tv_sec + timeout;
      connection->connection_sub_state = CL_COM_OPEN_CONNECT;
   }
   
   if ( connection->connection_sub_state == CL_COM_OPEN_CONNECT) {
      int my_error;
      int i;
      cl_bool_t connect_state = CL_FALSE;

      CL_LOG(CL_LOG_DEBUG,"connection_sub_state is CL_COM_OPEN_CONNECT");

      errno = 0;
      i = connect(private->sockfd, (struct sockaddr *) &(private->client_addr), sizeof(struct sockaddr_in));
      my_error = errno;
      if (i == 0) {
         /* we are connected */
         connect_state = CL_TRUE;
      } else {
         switch(my_error) {
            case EISCONN: {
               CL_LOG(CL_LOG_INFO,"already connected");
               connect_state = CL_TRUE;
               break;
            }
            case ECONNREFUSED: {
               /* can't open connection */
               CL_LOG_INT(CL_LOG_ERROR,"connection refused to port ",private->connect_port);
               shutdown(private->sockfd, 2);
               close(private->sockfd);
               private->sockfd = -1;
               cl_commlib_push_application_error(CL_LOG_ERROR, CL_RETVAL_CONNECT_ERROR, strerror(my_error));
               return CL_RETVAL_CONNECT_ERROR;
            }
            case EADDRNOTAVAIL: {
               /* can't open connection */
               CL_LOG_INT(CL_LOG_ERROR,"address not available for port ",private->connect_port);
               shutdown(private->sockfd, 2);
               close(private->sockfd);
               private->sockfd = -1;
               cl_commlib_push_application_error(CL_LOG_ERROR, CL_RETVAL_CONNECT_ERROR, strerror(my_error));
               return CL_RETVAL_CONNECT_ERROR;
            }
            case EINPROGRESS:
            case EALREADY: {
               connection->connection_sub_state = CL_COM_OPEN_CONNECT_IN_PROGRESS;
               return CL_RETVAL_UNCOMPLETE_WRITE;
            }
            default: {
               /* we have an connect error */
               CL_LOG_INT(CL_LOG_ERROR,"connect error errno:", my_error);
               shutdown(private->sockfd, 2);
               close(private->sockfd);
               private->sockfd = -1;
               cl_commlib_push_application_error(CL_LOG_ERROR, CL_RETVAL_CONNECT_ERROR, strerror(my_error));
               return CL_RETVAL_CONNECT_ERROR;
            }
         }
      } 
      if (connect_state == CL_TRUE) {
         connection->write_buffer_timeout_time = 0;
         connection->connection_sub_state = CL_COM_OPEN_CONNECTED;
      }
   }

   if ( connection->connection_sub_state == CL_COM_OPEN_CONNECT_IN_PROGRESS ) {
      struct timeval now;
      int socket_error = 0;

#if defined(IRIX65) || defined(INTERIX) || defined(DARWIN6) || defined(ALPHA5) || defined(HPUX)
      int socklen = sizeof(socket_error);
#else
      socklen_t socklen = sizeof(socket_error);
#endif

      CL_LOG(CL_LOG_DEBUG,"connection_sub_state is CL_COM_OPEN_CONNECT_IN_PROGRESS");

#if defined(SOLARIS) && !defined(SOLARIS64)
      getsockopt(private->sockfd,SOL_SOCKET, SO_ERROR, (void*)&socket_error, &socklen);
#else
      getsockopt(private->sockfd,SOL_SOCKET, SO_ERROR, &socket_error, &socklen);
#endif
      if (socket_error == 0 || socket_error == EISCONN) {
         CL_LOG(CL_LOG_INFO,"connected");
         connection->write_buffer_timeout_time = 0;
         connection->connection_sub_state = CL_COM_OPEN_CONNECTED;
         /* we are connected */
      } else {
         if (socket_error != EINPROGRESS && socket_error != EALREADY) {
            CL_LOG_INT(CL_LOG_ERROR,"socket error errno:", socket_error);
            shutdown(private->sockfd, 2);
            close(private->sockfd);
            private->sockfd = -1;
            cl_commlib_push_application_error(CL_LOG_ERROR, CL_RETVAL_CONNECT_ERROR, strerror(socket_error));
            return CL_RETVAL_CONNECT_ERROR;
         }

         gettimeofday(&now,NULL);
         if (connection->write_buffer_timeout_time <= now.tv_sec || 
             cl_com_get_ignore_timeouts_flag()     == CL_TRUE       ) {

            /* we had an timeout */
            CL_LOG(CL_LOG_ERROR,"connect timeout error");
            connection->write_buffer_timeout_time = 0;
            shutdown(private->sockfd, 2);
            close(private->sockfd);
            private->sockfd = -1;
            cl_commlib_push_application_error(CL_LOG_ERROR, CL_RETVAL_CONNECT_TIMEOUT, MSG_CL_TCP_FW_CONNECT_TIMEOUT );
            return CL_RETVAL_CONNECT_TIMEOUT;
         }

         return CL_RETVAL_UNCOMPLETE_WRITE;
      }
   }

   if ( connection->connection_sub_state == CL_COM_OPEN_CONNECTED) {
      int on = 1; 

      CL_LOG(CL_LOG_DEBUG,"connection_sub_state is CL_COM_OPEN_CONNECTED");

  
#if defined(SOLARIS) && !defined(SOLARIS64)
      if (setsockopt(private->sockfd, IPPROTO_TCP, TCP_NODELAY, (const char *) &on, sizeof(int)) != 0) {
         CL_LOG(CL_LOG_ERROR,"could not set TCP_NODELAY");
      } 
#else
      if (setsockopt(private->sockfd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(int))!= 0) {
         CL_LOG(CL_LOG_ERROR,"could not set TCP_NODELAY");
      }
#endif
      connection->connection_sub_state = CL_COM_OPEN_SSL_CONNECT_INIT;
   }

   if ( connection->connection_sub_state == CL_COM_OPEN_SSL_CONNECT_INIT) {
      struct timeval now;

      CL_LOG(CL_LOG_DEBUG,"connection_sub_state is CL_COM_OPEN_SSL_CONNECT");
      /* now connect the tcp socket to SSL socket */

      /* create a new ssl object */
      private->ssl_obj        = cl_com_ssl_func__SSL_new(private->ssl_ctx);
      if ( private->ssl_obj == NULL) {
         cl_com_ssl_log_ssl_errors(__CL_FUNCTION__);
         CL_LOG(CL_LOG_ERROR,"can't create ssl object");
         return CL_RETVAL_SSL_CANT_CREATE_SSL_OBJECT;
      }

      /* set default modes */
      tmp_error = cl_com_ssl_set_default_mode(NULL, private->ssl_obj);
      if (tmp_error != CL_RETVAL_OK) {
         cl_com_ssl_log_ssl_errors(__CL_FUNCTION__);
         CL_LOG(CL_LOG_ERROR,"can't set default ssl mode");
         return tmp_error;
      }


      /* create a new ssl bio socket associated with the connected tcp connection */
      private->ssl_bio_socket = cl_com_ssl_func__BIO_new_socket(private->sockfd, BIO_NOCLOSE);

      /* check for errors */
      if ( private->ssl_bio_socket == NULL) {
         cl_com_ssl_log_ssl_errors(__CL_FUNCTION__);
         CL_LOG(CL_LOG_ERROR,"can't create bio socket");
         return CL_RETVAL_SSL_CANT_CREATE_BIO_SOCKET;
      }
 
      /* connect the SSL object with the BIO (the same BIO is used for read/write) */
      cl_com_ssl_func__SSL_set_bio(private->ssl_obj, private->ssl_bio_socket, private->ssl_bio_socket);

      /* set timeout time */
      gettimeofday(&now,NULL);
      connection->write_buffer_timeout_time = now.tv_sec + timeout;
      connection->connection_sub_state = CL_COM_OPEN_SSL_CONNECT;
   }

   if ( connection->connection_sub_state == CL_COM_OPEN_SSL_CONNECT) {
      int ssl_connect_error = 0;
      int ssl_error = 0;
      struct timeval now;

      CL_LOG(CL_LOG_DEBUG,"connection_sub_state is CL_COM_OPEN_SSL_CONNECT");

       /* now do a SSL Connect */
      ssl_connect_error = cl_com_ssl_func__SSL_connect(private->ssl_obj);
      if (ssl_connect_error != 1) {
#if 0
         if (ssl_connect_error == 0) {
            /* 
             * TLS/SSL handshake was not successful but was shutdown controlled 
             * reason -> check SSL_get_error()
             */
         }
#endif
         /* Try to find out more about the connect error */
         ssl_error = cl_com_ssl_func__SSL_get_error(private->ssl_obj, ssl_connect_error);
         CL_LOG_STR(CL_LOG_INFO,"ssl_error:", cl_com_ssl_get_error_text(ssl_error) );
         private->ssl_last_error = ssl_error;
         switch(ssl_error) {
            case SSL_ERROR_WANT_READ:
            case SSL_ERROR_WANT_WRITE:
            case SSL_ERROR_WANT_CONNECT:
#ifdef CL_COM_ENABLE_SSL_THREAD_RETRY_BUGFIX
            case SSL_ERROR_SYSCALL:
#endif
            {
               gettimeofday(&now,NULL);
               if (connection->write_buffer_timeout_time <= now.tv_sec || 
                   cl_com_get_ignore_timeouts_flag()     == CL_TRUE       ) {

                  /* we had an timeout */
                  CL_LOG(CL_LOG_ERROR,"ssl connect timeout error");
                  connection->write_buffer_timeout_time = 0;
                  cl_commlib_push_application_error(CL_LOG_ERROR, CL_RETVAL_SSL_CONNECT_HANDSHAKE_TIMEOUT, MSG_CL_TCP_FW_SSL_CONNECT_TIMEOUT );
                  return CL_RETVAL_SSL_CONNECT_HANDSHAKE_TIMEOUT;
               }

               return CL_RETVAL_UNCOMPLETE_WRITE;
            }

            default: {
               CL_LOG(CL_LOG_ERROR,"SSL handshake not successful and no clear cleanup");
               cl_commlib_push_application_error(CL_LOG_ERROR, CL_RETVAL_SSL_CONNECT_ERROR, MSG_CL_COMMLIB_SSL_HANDSHAKE_ERROR);
               cl_com_ssl_log_ssl_errors(__CL_FUNCTION__);
               return CL_RETVAL_SSL_CONNECT_ERROR;
            }
         }
      }
      CL_LOG(CL_LOG_INFO,"SSL Connect successful");
      connection->write_buffer_timeout_time = 0;

      return cl_com_ssl_fill_private_from_peer_cert(private, CL_FALSE);

   }
   return CL_RETVAL_UNKNOWN;
}

#ifdef __CL_FUNCTION__
#undef __CL_FUNCTION__
#endif
#define __CL_FUNCTION__ "cl_com_ssl_read_GMSH()"
int cl_com_ssl_read_GMSH(cl_com_connection_t* connection, unsigned long *only_one_read) {
   int retval = CL_RETVAL_OK;
   unsigned long data_read = 0;
   unsigned long processed_data = 0;

   if (connection == NULL || only_one_read == NULL) {
      return CL_RETVAL_PARAMS;
   }

   /* first read size of gmsh header without data */
   if (connection->data_read_buffer_pos < CL_GMSH_MESSAGE_SIZE) {
      data_read = 0;
      retval = cl_com_ssl_read(connection,
                               &(connection->data_read_buffer[connection->data_read_buffer_pos]),
                               CL_GMSH_MESSAGE_SIZE - connection->data_read_buffer_pos,
                               &data_read);
      connection->data_read_buffer_pos = connection->data_read_buffer_pos + data_read;
      *only_one_read = data_read;

      if ( retval != CL_RETVAL_OK) {
         CL_LOG_STR(CL_LOG_INFO,"uncomplete read:", cl_get_error_text(retval));
         return retval;
      }
   }

   /* now read complete header */
   while ( connection->data_read_buffer[connection->data_read_buffer_pos - 1] != '>' ||
           connection->data_read_buffer[connection->data_read_buffer_pos - 2] != 'h'   ) {

      /* check buffer overflow */
      if ( connection->data_read_buffer_pos >= connection->data_buffer_size) {
         CL_LOG(CL_LOG_WARNING,"buffer overflow");
         return CL_RETVAL_STREAM_BUFFER_OVERFLOW;
      }
      
      data_read = 0;
      retval = cl_com_ssl_read(connection,
                               &(connection->data_read_buffer[connection->data_read_buffer_pos]),
                               1,
                               &data_read);
      connection->data_read_buffer_pos = connection->data_read_buffer_pos + data_read;
      *only_one_read = data_read;

      if (retval != CL_RETVAL_OK) {
         CL_LOG(CL_LOG_WARNING,"uncomplete read(2):");
         return retval;
      }
   }

   if ( connection->data_read_buffer_pos >= connection->data_buffer_size) {
       CL_LOG(CL_LOG_WARNING,"buffer overflow (2)");
       return CL_RETVAL_STREAM_BUFFER_OVERFLOW;
   }


   connection->data_read_buffer[connection->data_read_buffer_pos] = 0;
   /* header should be now complete */
   if ( strcmp((char*)&(connection->data_read_buffer[connection->data_read_buffer_pos - 7]) ,"</gmsh>") != 0) {
      CL_LOG(CL_LOG_WARNING,"can't find gmsh end tag");
      return CL_RETVAL_GMSH_ERROR;
   }
   
   /* parse header */
   retval = cl_xml_parse_GMSH(connection->data_read_buffer, connection->data_read_buffer_pos, connection->read_gmsh_header, &processed_data);
   connection->data_read_buffer_processed = connection->data_read_buffer_processed + processed_data ;
   if ( connection->read_gmsh_header->dl == 0) {
      CL_LOG(CL_LOG_ERROR,"gmsh header has dl=0 entry");
      return CL_RETVAL_GMSH_ERROR;
   }
   if ( connection->read_gmsh_header->dl > CL_DEFINE_MAX_MESSAGE_LENGTH ) {
      CL_LOG(CL_LOG_ERROR,"gmsh header dl entry is larger than CL_DEFINE_MAX_MESSAGE_LENGTH");
      cl_commlib_push_application_error(CL_LOG_ERROR, CL_RETVAL_MAX_MESSAGE_LENGTH_ERROR, NULL);
      return CL_RETVAL_MAX_MESSAGE_LENGTH_ERROR;
   }
   return retval;
}

#ifdef __CL_FUNCTION__
#undef __CL_FUNCTION__
#endif
#define __CL_FUNCTION__ "cl_com_ssl_connection_request_handler_setup_finalize()"
static int cl_com_ssl_connection_request_handler_setup_finalize(cl_com_connection_t* connection) {
   int sockfd = 0;
   cl_com_ssl_private_t* private = NULL;

   if (connection == NULL ) {
      CL_LOG(CL_LOG_ERROR,"no connection");
      return CL_RETVAL_PARAMS;
   }
   private = cl_com_ssl_get_private(connection);
   if (private == NULL) {
      CL_LOG(CL_LOG_ERROR,"framework not initalized");
      return CL_RETVAL_PARAMS; 
   }
 
   sockfd = private->pre_sockfd;
   if (sockfd < 0) {
      CL_LOG(CL_LOG_ERROR, "pre_sockfd not valid");
      return CL_RETVAL_PARAMS;
   }


   /* make socket listening for incoming connects */
   if (listen(sockfd, 5) != 0) {   /* TODO: set listen params */
      shutdown(sockfd, 2);
      close(sockfd);
      CL_LOG(CL_LOG_ERROR,"listen error");
      return CL_RETVAL_LISTEN_ERROR;
   }
   CL_LOG_INT(CL_LOG_INFO,"listening with backlog=", 5);

   /* set server socked file descriptor and mark connection as service handler */
   private->sockfd = sockfd;
   

   CL_LOG(CL_LOG_INFO,"===============================");
   CL_LOG(CL_LOG_INFO,"SSL server setup done:");
   CL_LOG_INT(CL_LOG_INFO,"server fd:", private->sockfd);
   CL_LOG_STR(CL_LOG_INFO,"host:     ", connection->local->comp_host);
   CL_LOG_STR(CL_LOG_INFO,"component:", connection->local->comp_name);
   CL_LOG_INT(CL_LOG_INFO,"id:       ", (int) connection->local->comp_id);
   CL_LOG(CL_LOG_INFO,"===============================");
   return CL_RETVAL_OK;
}


#ifdef __CL_FUNCTION__
#undef __CL_FUNCTION__
#endif
#define __CL_FUNCTION__ "cl_com_ssl_connection_request_handler_setup()"
int cl_com_ssl_connection_request_handler_setup(cl_com_connection_t* connection, cl_bool_t only_prepare_service) {
   int ret;
   int sockfd = 0;
   struct sockaddr_in serv_addr;
   cl_com_ssl_private_t* private = NULL;
   int tmp_error = CL_RETVAL_OK;

   CL_LOG(CL_LOG_INFO,"setting up SSL request handler ...");
    
   if (connection == NULL ) {
      CL_LOG(CL_LOG_ERROR,"no connection");
      return CL_RETVAL_PARAMS;
   }

   private = cl_com_ssl_get_private(connection);
   if (private == NULL) {
      CL_LOG(CL_LOG_ERROR,"framework not initalized");
      return CL_RETVAL_NO_FRAMEWORK_INIT;
   }

   if ( private->server_port < 0 ) {
      CL_LOG(CL_LOG_ERROR,cl_get_error_text(CL_RETVAL_NO_PORT_ERROR));
      return CL_RETVAL_NO_PORT_ERROR;
   }

   if( (tmp_error=cl_com_ssl_setup_context(connection, CL_TRUE)) != CL_RETVAL_OK) {
      return tmp_error;
   }


   /* create socket */
   if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
      CL_LOG(CL_LOG_ERROR,"could not create socket");
      return CL_RETVAL_CREATE_SOCKET;
   }

   if (sockfd < 3) {
      CL_LOG_INT(CL_LOG_WARNING, "The file descriptor is < 3. Will dup fd to be >= 3! fd value: ", sockfd);
      ret = sge_dup_fd_above_stderr(&sockfd);
      if (ret != 0) {
         CL_LOG_INT(CL_LOG_ERROR, "can't dup socket fd to be >=3, errno = ", ret);
         shutdown(sockfd, 2);
         close(sockfd);
         sockfd = -1;
         cl_commlib_push_application_error(CL_LOG_ERROR, CL_RETVAL_DUP_SOCKET_FD_ERROR, MSG_CL_COMMLIB_CANNOT_DUP_SOCKET_FD);
         return CL_RETVAL_DUP_SOCKET_FD_ERROR;
      }
      CL_LOG_INT(CL_LOG_INFO, "fd value after dup: ", sockfd);
   }

#ifndef USE_POLL
   if (sockfd >= FD_SETSIZE) {
       CL_LOG(CL_LOG_ERROR,"filedescriptors exeeds FD_SETSIZE of this system");
       shutdown(sockfd, 2);
       close(sockfd);
       cl_commlib_push_application_error(CL_LOG_ERROR, CL_RETVAL_REACHED_FILEDESCRIPTOR_LIMIT, MSG_CL_COMMLIB_COMPILE_SOURCE_WITH_LARGER_FD_SETSIZE );
       return CL_RETVAL_REACHED_FILEDESCRIPTOR_LIMIT;
   }
#endif
   
   { 
      int on = 1;

      if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (char *) &on, sizeof(on)) != 0) {
         CL_LOG(CL_LOG_ERROR,"could not set SO_REUSEADDR");
         return CL_RETVAL_SETSOCKOPT_ERROR;
      }
   }

   /* bind an address to socket */
   /* TODO FEATURE: we can also try to use a specified port range */
   memset((char *) &serv_addr, 0, sizeof(serv_addr));
   serv_addr.sin_port = htons(private->server_port);
   serv_addr.sin_family = AF_INET;
   serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);

  
   if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
      shutdown(sockfd, 2);
      close(sockfd);
      CL_LOG_INT(CL_LOG_ERROR, "could not bind server socket port:", private->server_port);
      return CL_RETVAL_BIND_SOCKET;
   }

   if (private->server_port == 0) {
#if defined(IRIX65) || defined(INTERIX) || defined(DARWIN6) || defined(ALPHA5) || defined(HPUX)
      int length;
#else
      socklen_t length;
#endif
      length = sizeof(serv_addr);
      /* find out assigned port number and pass it to caller */
      if (getsockname(sockfd,(struct sockaddr *) &serv_addr, &length ) == -1) {
         shutdown(sockfd, 2);
         close(sockfd);
         CL_LOG_INT(CL_LOG_ERROR, "could not bind random server socket port:", private->server_port);
         return CL_RETVAL_BIND_SOCKET;
      }
      private->server_port = ntohs(serv_addr.sin_port);
      CL_LOG_INT(CL_LOG_INFO,"random server port is:", private->server_port);
   }

   /* if only_prepare_service is enabled we don't want to set the port into
      listen mode now, we have to do it later */
   private->pre_sockfd = sockfd;
   if (only_prepare_service == CL_TRUE) {
      CL_LOG_INT(CL_LOG_INFO,"service socket prepared for listen, using sockfd=", sockfd);
      return CL_RETVAL_OK;
   }

   return cl_com_ssl_connection_request_handler_setup_finalize(connection);
}


#ifdef __CL_FUNCTION__
#undef __CL_FUNCTION__
#endif
#define __CL_FUNCTION__ "cl_com_ssl_connection_request_handler()"
int cl_com_ssl_connection_request_handler(cl_com_connection_t* connection,cl_com_connection_t** new_connection) {
   cl_com_connection_t* tmp_connection = NULL;
   struct sockaddr_in cli_addr;
   int new_sfd = 0;
   int sso;
#if defined(IRIX65) || defined(INTERIX) || defined(DARWIN6) || defined(ALPHA5) || defined(HPUX)
   int fromlen = 0;
#else
   socklen_t fromlen = 0;
#endif
   int retval;
   int server_fd = -1;
   cl_com_ssl_private_t* private = NULL;
   
   if (connection == NULL || new_connection == NULL) {
      CL_LOG(CL_LOG_ERROR,"no connection or no accept connection");
      return CL_RETVAL_PARAMS;
   }

   if (*new_connection != NULL) {
      CL_LOG(CL_LOG_ERROR,"accept connection is not free");
      return CL_RETVAL_PARAMS;
   }
   
   private = cl_com_ssl_get_private(connection);
   if (private == NULL) {
      CL_LOG(CL_LOG_ERROR,"framework is not initalized");
      return CL_RETVAL_NO_FRAMEWORK_INIT;
   }

   if (connection->service_handler_flag != CL_COM_SERVICE_HANDLER) {
      CL_LOG(CL_LOG_ERROR,"connection is no service handler");
      return CL_RETVAL_NOT_SERVICE_HANDLER;
   }
   server_fd = private->sockfd;

   /* got new connect */
   fromlen = sizeof(cli_addr);
   memset((char *) &cli_addr, 0, sizeof(cli_addr));
   new_sfd = accept(server_fd, (struct sockaddr *) &cli_addr, &fromlen);
   if (new_sfd > -1) {
      char* resolved_host_name = NULL;
      cl_com_ssl_private_t* tmp_private = NULL;

      if (new_sfd < 3) {
         CL_LOG_INT(CL_LOG_WARNING, "The file descriptor is < 3. Will dup fd to be >= 3! fd value: ", new_sfd);
         retval = sge_dup_fd_above_stderr(&new_sfd);
         if (retval != 0) {
            CL_LOG_INT(CL_LOG_ERROR, "can't dup socket fd to be >=3, errno = ", retval);
            shutdown(new_sfd, 2);
            close(new_sfd);
            new_sfd = -1;
            cl_commlib_push_application_error(CL_LOG_ERROR, CL_RETVAL_DUP_SOCKET_FD_ERROR, MSG_CL_COMMLIB_CANNOT_DUP_SOCKET_FD);
            return CL_RETVAL_DUP_SOCKET_FD_ERROR;
         }
         CL_LOG_INT(CL_LOG_INFO, "fd value after dup: ", new_sfd);
      }

#ifndef USE_POLL
      if (new_sfd >= FD_SETSIZE) {
         CL_LOG(CL_LOG_ERROR,"filedescriptors exeeds FD_SETSIZE of this system");
         shutdown(new_sfd, 2);
         close(new_sfd);
         cl_commlib_push_application_error(CL_LOG_ERROR, CL_RETVAL_REACHED_FILEDESCRIPTOR_LIMIT, MSG_CL_COMMLIB_COMPILE_SOURCE_WITH_LARGER_FD_SETSIZE );
         return CL_RETVAL_REACHED_FILEDESCRIPTOR_LIMIT;
      }
#endif

      cl_com_cached_gethostbyaddr(&(cli_addr.sin_addr), &resolved_host_name ,NULL, NULL); 
      if (resolved_host_name != NULL) {
         CL_LOG_STR(CL_LOG_INFO,"new connection from host", resolved_host_name  );
      } else {
         CL_LOG(CL_LOG_WARNING,"could not resolve incoming hostname");
      }

      fcntl(new_sfd, F_SETFL, O_NONBLOCK);         /* HP needs O_NONBLOCK, was O_NDELAY */
      sso = 1;
#if defined(SOLARIS) && !defined(SOLARIS64)
      if (setsockopt(new_sfd, IPPROTO_TCP, TCP_NODELAY, (const char *) &sso, sizeof(int)) == -1) {
         CL_LOG(CL_LOG_ERROR,"could not set TCP_NODELAY");
      }
#else
      if (setsockopt(new_sfd, IPPROTO_TCP, TCP_NODELAY, &sso, sizeof(int))== -1) { 
         CL_LOG(CL_LOG_ERROR,"could not set TCP_NODELAY");
      }
#endif
      /* here we can investigate more information about the client */
      /* ntohs(cli_addr.sin_port) ... */

      tmp_connection = NULL;
      /* setup a ssl connection where autoclose is still undefined */
      if ( (retval=cl_com_ssl_setup_connection(&tmp_connection, 
                                               private->server_port,
                                               private->connect_port,
                                               connection->data_flow_type,
                                               CL_CM_AC_UNDEFINED,
                                               connection->framework_type,
                                               connection->data_format_type,
                                               connection->tcp_connect_mode,
                                               private->ssl_setup)) != CL_RETVAL_OK) {
         cl_com_ssl_close_connection(&tmp_connection); 
         if (resolved_host_name != NULL) {
            free(resolved_host_name);
         }
         shutdown(new_sfd, 2);
         close(new_sfd);
         return retval;
      }

      tmp_connection->client_host_name = resolved_host_name; /* set resolved hostname of client */

      /* setup cl_com_ssl_private_t */
      tmp_private = cl_com_ssl_get_private(tmp_connection);
      if (tmp_private != NULL) {
         tmp_private->sockfd = new_sfd;   /* fd from accept() call */
         tmp_private->connect_in_port = ntohs(cli_addr.sin_port);
      }
      *new_connection = tmp_connection;
      return CL_RETVAL_OK;
   }
   return CL_RETVAL_OK;
}

#ifdef __CL_FUNCTION__
#undef __CL_FUNCTION__
#endif
#define __CL_FUNCTION__ "cl_com_ssl_connection_request_handler_cleanup()"
int cl_com_ssl_connection_request_handler_cleanup(cl_com_connection_t* connection) {
   cl_com_ssl_private_t* private = NULL;

   CL_LOG(CL_LOG_INFO,"cleanup of SSL request handler ...");
   if (connection == NULL ) {
      return CL_RETVAL_PARAMS;
   }

   private = cl_com_ssl_get_private(connection);
   if (private == NULL) {
      return CL_RETVAL_NO_FRAMEWORK_INIT;
   }

   shutdown(private->sockfd, 2);
   close(private->sockfd);
   private->sockfd = -1;

   return CL_RETVAL_OK;
}

#ifdef __CL_FUNCTION__
#undef __CL_FUNCTION__
#endif
#define __CL_FUNCTION__ "cl_com_ssl_open_connection_request_handler()"
#ifdef USE_POLL
int cl_com_ssl_open_connection_request_handler(cl_com_poll_t* poll_handle, cl_com_handle_t* handle, cl_raw_list_t* connection_list, cl_com_connection_t* service_connection, int timeout_val_sec, int timeout_val_usec, cl_select_method_t select_mode)
#else
int cl_com_ssl_open_connection_request_handler(cl_com_handle_t* handle, cl_raw_list_t* connection_list, cl_com_connection_t* service_connection, int timeout_val_sec, int timeout_val_usec, cl_select_method_t select_mode)
#endif
{

   int select_back;
   cl_connection_list_elem_t* con_elem = NULL;
   cl_com_connection_t*  connection = NULL;
   cl_com_ssl_private_t* con_private = NULL;

   int max_fd = -1;
   int server_fd = -1;
   int retval = CL_RETVAL_UNKNOWN;
   int do_read_select = 0;
   int do_write_select = 0;
   int my_errno = 0;
   int nr_of_descriptors = 0;
   cl_connection_list_data_t* ldata = NULL;
   int socket_error = 0;
   int get_sock_opt_error = 0;
   char tmp_string[1024];

#if defined(IRIX65) || defined(INTERIX) || defined(DARWIN6) || defined(ALPHA5) || defined(HPUX)
   int socklen = sizeof(socket_error);
#else
   socklen_t socklen = sizeof(socket_error);
#endif

#ifdef USE_POLL
   struct pollfd* ufds = NULL;
   cl_com_connection_t** ufds_con = NULL;
   unsigned long ufds_index = 0;
   unsigned long fd_index = 0;
   int fd_offset = 2;
#else
   fd_set my_read_fds;
   fd_set my_write_fds;
#endif
   struct timeval timeout;

#ifdef USE_POLL
   if (poll_handle == NULL) {
      CL_LOG(CL_LOG_ERROR, "poll_handle == NULL");
      return CL_RETVAL_PARAMS;
   }
#endif

   if (handle == NULL) {
      CL_LOG(CL_LOG_ERROR,"handle == NULL");
      return CL_RETVAL_PARAMS;
   }

   if (connection_list == NULL ) {
      CL_LOG(CL_LOG_ERROR,"no connection list");
      return CL_RETVAL_PARAMS;
   }

   if (select_mode == CL_RW_SELECT || select_mode == CL_R_SELECT) {
      do_read_select = 1;
   }
   if (select_mode == CL_RW_SELECT || select_mode == CL_W_SELECT) {
      do_write_select = 1;
   }

   if (select_mode == CL_W_SELECT) {
      timeout.tv_sec = 0;
      timeout.tv_usec = 5*1000; /* 5 ms */
   } else {
      timeout.tv_sec = timeout_val_sec; 
      timeout.tv_usec = timeout_val_usec;
   }

   /* lock list */
   if ( cl_raw_list_lock(connection_list) != CL_RETVAL_OK) {
      CL_LOG(CL_LOG_ERROR,"could not lock connection list");
      return CL_RETVAL_LOCK_ERROR;
   }

   if ( connection_list->list_data == NULL) {
      cl_raw_list_unlock(connection_list);
      return CL_RETVAL_NO_FRAMEWORK_INIT;
   } else {
      ldata = (cl_connection_list_data_t*) connection_list->list_data;
   }

#ifdef USE_POLL
   /* first check if we have a poll_array of the correct size*/
   fd_offset = fd_offset + cl_raw_list_get_elem_count(handle->file_descriptor_list);
   if (poll_handle->poll_fd_count != handle->max_open_connections + fd_offset) {
      /* max_open_connections might have changed */
      int poll_return = cl_com_malloc_poll_array(poll_handle, handle->max_open_connections + fd_offset);
      if (poll_return != CL_RETVAL_OK) {
         cl_raw_list_unlock(connection_list);
         return poll_return;
      }
   }

   /* check poll_array size */
   if (poll_handle->poll_fd_count < cl_raw_list_get_elem_count(connection_list) + fd_offset) {
      /* This should not happen, but we want to be on the save side */
      int poll_return = cl_com_malloc_poll_array(poll_handle, cl_raw_list_get_elem_count(connection_list) + fd_offset);
      CL_LOG(CL_LOG_WARNING, "max_open_connection count < current connection size - this must NOT happen!");
      if (poll_return != CL_RETVAL_OK) {
         cl_raw_list_unlock(connection_list);
         return poll_return;
      }
   }

   /* init poll_array data */
   ufds = poll_handle->poll_array;
   ufds_con = poll_handle->poll_con;

   /* cleanup first arrays */
   ufds_con[ufds_index] = NULL;
   memset(&(ufds[ufds_index]), 0, sizeof(struct pollfd));
#else
   FD_ZERO(&my_read_fds);
   FD_ZERO(&my_write_fds);
#endif
 
   if (service_connection != NULL && do_read_select != 0) {
      cl_com_ssl_private_t* private = NULL;
      int tmp_retval = CL_RETVAL_OK;

      /* this is to come out of select when for new connections */
      if(cl_com_ssl_get_private(service_connection) == NULL ) {
         CL_LOG(CL_LOG_ERROR,"service framework is not initalized");
         cl_raw_list_unlock(connection_list);
         return CL_RETVAL_NO_FRAMEWORK_INIT;
      }
      if( service_connection->service_handler_flag != CL_COM_SERVICE_HANDLER) {
         CL_LOG(CL_LOG_ERROR,"service connection is no service handler");
         cl_raw_list_unlock(connection_list);
         return CL_RETVAL_NOT_SERVICE_HANDLER;
      }
      private = cl_com_ssl_get_private(service_connection);
      /* check if service is already in listen mode. This might happen
         when only_prepare_service was set to true at 
         cl_com_tcp_connection_request_handler_setup() */
      if (private->sockfd == -1 && private->pre_sockfd != -1 ) {
         /* finalize server socket setup */
         tmp_retval = cl_com_ssl_connection_request_handler_setup_finalize(service_connection);
         if (tmp_retval != CL_RETVAL_OK ) {
            cl_raw_list_unlock(connection_list);
            return tmp_retval;
         } else {
            private->pre_sockfd = -1;
         }
      }
      server_fd = private->sockfd;
      max_fd = MAX(max_fd,server_fd);

#ifdef USE_POLL
      ufds_con[ufds_index] = service_connection;
      ufds[ufds_index].fd = server_fd;
      ufds[ufds_index].events = POLLIN|POLLPRI;
      ufds_index++;
      ufds_con[ufds_index] = NULL;
      memset(&(ufds[ufds_index]), 0, sizeof(struct pollfd));
#else
      FD_SET(server_fd, &my_read_fds);
#endif
      service_connection->is_read_selected = CL_TRUE;
      nr_of_descriptors++;
      service_connection->data_read_flag = CL_COM_DATA_NOT_READY;
   }


   /* reset connection data_read flags */
   con_elem = cl_connection_list_get_first_elem(connection_list);
   while(con_elem) {
      connection = con_elem->connection;

      if ( (con_private=cl_com_ssl_get_private(connection)) == NULL) {
         cl_raw_list_unlock(connection_list);
         CL_LOG(CL_LOG_ERROR,"no private data pointer");
         return CL_RETVAL_NO_FRAMEWORK_INIT;
      }

      if (do_read_select != 0) {
         connection->is_read_selected = CL_FALSE;
      }
      if (do_write_select != 0) {
         connection->is_write_selected = CL_FALSE;
      }

      if (con_private->sockfd >= 0) {
         switch(connection->framework_type) {
            case CL_CT_SSL: {
               switch (connection->connection_state) {
                  case CL_CONNECTED:
                     if (connection->connection_sub_state != CL_COM_DONE) {
                        if (do_read_select != 0) {
#ifdef USE_POLL
                           ufds[ufds_index].fd = con_private->sockfd;
                           ufds[ufds_index].events = POLLIN|POLLPRI;
                           ufds_con[ufds_index] = connection;
#else
                           FD_SET(con_private->sockfd,&my_read_fds);
#endif
                           connection->is_read_selected = CL_TRUE;
                           max_fd = MAX(max_fd,con_private->sockfd);
                           nr_of_descriptors++;
                           connection->data_read_flag = CL_COM_DATA_NOT_READY;
                        }
                        if (do_write_select != 0) {
                           if (connection->data_write_flag == CL_COM_DATA_READY) {
                              /* this is to come out of select when data is ready to write */
#ifdef USE_POLL
                              ufds[ufds_index].fd = con_private->sockfd;
                              ufds[ufds_index].events |= POLLOUT;
                              ufds_con[ufds_index] = connection;
#else
                              FD_SET(con_private->sockfd,&my_write_fds);
#endif
                              connection->is_write_selected = CL_TRUE;
                              max_fd = MAX(max_fd, con_private->sockfd);
                              connection->fd_ready_for_write = CL_COM_DATA_NOT_READY;
                           } 
                           if (con_private->ssl_last_error == SSL_ERROR_WANT_WRITE) {
                              max_fd = MAX(max_fd, con_private->sockfd);
#ifdef USE_POLL
                              ufds[ufds_index].fd = con_private->sockfd;
                              ufds[ufds_index].events |= POLLOUT;
                              ufds_con[ufds_index] = connection;
#else
                              FD_SET(con_private->sockfd,&my_write_fds);
#endif
                              connection->is_write_selected = CL_TRUE;
                              connection->fd_ready_for_write = CL_COM_DATA_NOT_READY;
                              connection->data_write_flag = CL_COM_DATA_READY;
                           }
                        }
#ifdef USE_POLL
                        if (ufds[ufds_index].events) {
                           ufds_index++;
                           ufds_con[ufds_index] = NULL;
                           memset(&(ufds[ufds_index]), 0, sizeof(struct pollfd));
                        }
#endif
                     }
                     break;
                  case CL_CONNECTING:
                     if (do_read_select != 0) {
                        max_fd = MAX(max_fd,con_private->sockfd);
#ifdef USE_POLL
                        ufds[ufds_index].fd = con_private->sockfd;
                        ufds[ufds_index].events = POLLIN|POLLPRI;
                        ufds_con[ufds_index] = connection;
#else
                        FD_SET(con_private->sockfd,&my_read_fds);
#endif
                        connection->is_read_selected = CL_TRUE;
                        nr_of_descriptors++;
                        connection->data_read_flag = CL_COM_DATA_NOT_READY;
                     }
                     if (do_write_select != 0) {
                        if (connection->data_write_flag == CL_COM_DATA_READY) {
                           /* this is to come out of select when data is ready to write */
                           max_fd = MAX(max_fd, con_private->sockfd);
#ifdef USE_POLL
                           ufds[ufds_index].fd = con_private->sockfd;
                           ufds[ufds_index].events |= POLLOUT;
                           ufds_con[ufds_index] = connection;
#else
                           FD_SET(con_private->sockfd,&my_write_fds);
#endif
                           connection->is_write_selected = CL_TRUE;
                           connection->fd_ready_for_write = CL_COM_DATA_NOT_READY;
                        }
                        if (con_private->ssl_last_error == SSL_ERROR_WANT_WRITE) {
                           max_fd = MAX(max_fd, con_private->sockfd);
#ifdef USE_POLL
                           ufds[ufds_index].fd = con_private->sockfd;
                           ufds[ufds_index].events |= POLLOUT;
                           ufds_con[ufds_index] = connection;
#else
                           FD_SET(con_private->sockfd,&my_write_fds);
#endif
                           connection->is_write_selected = CL_TRUE;
                           connection->fd_ready_for_write = CL_COM_DATA_NOT_READY;
                           connection->data_write_flag = CL_COM_DATA_READY;
                        }
                     }
#ifdef USE_POLL
                     if (ufds[ufds_index].events) {
                         ufds_index++;
                         ufds_con[ufds_index] = NULL;
                         memset(&(ufds[ufds_index]), 0, sizeof(struct pollfd));
                     }
#endif
                     break;
                  case CL_ACCEPTING: {
                     if (connection->connection_sub_state == CL_COM_ACCEPT_INIT ||
                         connection->connection_sub_state == CL_COM_ACCEPT) {
                           if (do_read_select != 0) {
                              max_fd = MAX(max_fd,con_private->sockfd);
#ifdef USE_POLL
                              ufds[ufds_index].fd = con_private->sockfd;
                              ufds[ufds_index].events = POLLIN|POLLPRI;
                              ufds_con[ufds_index] = connection;
                              ufds_index++;
                              ufds_con[ufds_index] = NULL;
                              memset(&(ufds[ufds_index]), 0, sizeof(struct pollfd));
#else
                              FD_SET(con_private->sockfd,&my_read_fds); 
#endif
                              connection->is_read_selected = CL_TRUE;
                              nr_of_descriptors++;
                              connection->data_read_flag = CL_COM_DATA_NOT_READY;
                           }
                     }
                     break;
                  }


                  case CL_OPENING:
                     CL_LOG_STR(CL_LOG_DEBUG,"connection_sub_state:", cl_com_get_connection_sub_state(connection));
                     switch(connection->connection_sub_state) {
                        case CL_COM_OPEN_INIT:
                        case CL_COM_OPEN_CONNECT: {
                           if (do_read_select != 0) {
                              connection->data_read_flag = CL_COM_DATA_READY;
                           }
                           break;
                        }
                        case CL_COM_OPEN_CONNECTED:
                        case CL_COM_OPEN_CONNECT_IN_PROGRESS: {
                           if (do_read_select != 0) {
                              max_fd = MAX(max_fd,con_private->sockfd);
#ifdef USE_POLL
                              ufds[ufds_index].fd = con_private->sockfd;
                              ufds[ufds_index].events = POLLIN|POLLPRI;
                              ufds_con[ufds_index] = connection;
#else
                              FD_SET(con_private->sockfd,&my_read_fds);
#endif
                              connection->is_read_selected = CL_TRUE;
                              nr_of_descriptors++;
                              connection->data_read_flag = CL_COM_DATA_NOT_READY;
                           }
                           if ( do_write_select != 0) {
                              max_fd = MAX(max_fd, con_private->sockfd);
#ifdef USE_POLL
                              ufds[ufds_index].fd = con_private->sockfd;
                              ufds[ufds_index].events |= POLLOUT;
                              ufds_con[ufds_index] = connection;
#else
                              FD_SET(con_private->sockfd,&my_write_fds);
#endif
                              connection->is_write_selected = CL_TRUE;
                              connection->fd_ready_for_write = CL_COM_DATA_NOT_READY;
                              connection->data_write_flag = CL_COM_DATA_READY;
                           }
#ifdef USE_POLL
                           if (ufds[ufds_index].events) {
                              ufds_index++;
                              ufds_con[ufds_index] = NULL;
                              memset(&(ufds[ufds_index]), 0, sizeof(struct pollfd));
                           }
#endif
                           break;
                        }
                        case CL_COM_OPEN_SSL_CONNECT:
                        case CL_COM_OPEN_SSL_CONNECT_INIT: {
                           if (do_read_select != 0) {
                              max_fd = MAX(max_fd,con_private->sockfd);
#ifdef USE_POLL
                              ufds[ufds_index].fd = con_private->sockfd;
                              ufds[ufds_index].events = POLLIN|POLLPRI;
                              ufds_con[ufds_index] = connection;
#else
                              FD_SET(con_private->sockfd,&my_read_fds);
#endif
                              connection->is_read_selected = CL_TRUE;
                              nr_of_descriptors++;
                              connection->data_read_flag = CL_COM_DATA_NOT_READY;
                           }
                           if (do_write_select != 0) {
                              if (con_private->ssl_last_error == SSL_ERROR_WANT_WRITE || 
                                  con_private->ssl_last_error == SSL_ERROR_WANT_CONNECT) {
                                 max_fd = MAX(max_fd, con_private->sockfd);
#ifdef USE_POLL
                                 ufds[ufds_index].fd = con_private->sockfd;
                                 ufds[ufds_index].events |= POLLOUT;
                                 ufds_con[ufds_index] = connection;
#else
                                 FD_SET(con_private->sockfd,&my_write_fds);
#endif
                                 connection->is_write_selected = CL_TRUE;
                                 connection->fd_ready_for_write = CL_COM_DATA_NOT_READY;
                                 connection->data_write_flag = CL_COM_DATA_READY;
                              }
                           }
#ifdef USE_POLL
                           if (ufds[ufds_index].events) {
                              ufds_index++;
                              ufds_con[ufds_index] = NULL;
                              memset(&(ufds[ufds_index]), 0, sizeof(struct pollfd));
                           }
#endif
                           break;
                        }
                        default:
                           break;
                     }
                     break;
                  case CL_DISCONNECTED:
                     break;
                  case CL_CLOSING: {
                     if (connection->connection_sub_state != CL_COM_SHUTDOWN_DONE) {
                        if (do_read_select != 0) {
#ifdef USE_POLL
                           ufds[ufds_index].fd = con_private->sockfd;
                           ufds[ufds_index].events = POLLIN|POLLPRI;
                           ufds_con[ufds_index] = connection;
#else
                           FD_SET(con_private->sockfd,&my_read_fds);
#endif
                           connection->is_read_selected = CL_TRUE;
                           max_fd = MAX(max_fd,con_private->sockfd);
                           nr_of_descriptors++;
                           connection->data_read_flag = CL_COM_DATA_NOT_READY;
                        }
                        if (connection->data_write_flag == CL_COM_DATA_READY && do_write_select != 0) {
                           /* this is to come out of select when data is ready to write */
#ifdef USE_POLL
                           ufds[ufds_index].fd = con_private->sockfd;
                           ufds[ufds_index].events |= POLLOUT;
                           ufds_con[ufds_index] = connection;
#else
                           FD_SET(con_private->sockfd,&my_write_fds);
#endif
                           connection->is_write_selected = CL_TRUE;
                           max_fd = MAX(max_fd, con_private->sockfd);
                           connection->fd_ready_for_write = CL_COM_DATA_NOT_READY;
                        }
                     }
#ifdef USE_POLL
                     if (ufds[ufds_index].events) {
                        ufds_index++;
                        ufds_con[ufds_index] = NULL;
                        memset(&(ufds[ufds_index]), 0, sizeof(struct pollfd));
                     }
#endif
                     break;
                  }
               }
               break;
            }
            case CL_CT_UNDEFINED:
            case CL_CT_TCP: {
               CL_LOG_STR(CL_LOG_WARNING,"ignoring unexpected connection type:",
                          cl_com_get_framework_type(connection));
            }
         }
      }
      con_elem = cl_connection_list_get_next_elem(con_elem);
   }

   /* add the external file descriptors to the FD_SETS */
   if (handle->file_descriptor_list != NULL){
      cl_fd_list_elem_t* elem = NULL;
      cl_raw_list_lock(handle->file_descriptor_list);
      elem = cl_fd_list_get_first_elem(handle->file_descriptor_list);
      while (elem) {
         if(do_read_select == 1 || do_write_select == 1){
#ifdef USE_POLL
            ufds[ufds_index].fd = elem->data->fd;
#endif
            if(do_read_select == 1){
#ifdef USE_POLL
               ufds[ufds_index].events |= POLLIN|POLLPRI;
#else
               FD_SET(elem->data->fd, &my_read_fds);
#endif
            }
            if(do_write_select == 1){
               if (elem->data->ready_for_writing == CL_TRUE) {
#ifdef USE_POLL
                  ufds[ufds_index].events |= POLLOUT;
#else
                  FD_SET(elem->data->fd, &my_write_fds);
#endif
               }
            }
            max_fd = MAX(max_fd, elem->data->fd);
#ifdef USE_POLL
            ufds_index++;
            ufds_con[ufds_index] = NULL;
            memset(&(ufds[ufds_index]), 0, sizeof(struct pollfd));
#endif
            nr_of_descriptors++;
         }
         
         elem = cl_fd_list_get_next_elem(elem);
      }
      cl_raw_list_unlock(handle->file_descriptor_list);
   }

   /* we don't have any file descriptor for select(), find out why: */
   if (max_fd == -1) {
      CL_LOG_INT(CL_LOG_INFO,"max fd =", max_fd);

/* TODO: remove CL_W_SELECT and CL_R_SELECT handling and use one handling for 
         CL_W_SELECT, CL_R_SELECT and CL_RW_SELECT ? */
      if ( select_mode == CL_W_SELECT ) {
         /* return immediate for only write select ( only called by write thread) */
         cl_raw_list_unlock(connection_list); 
         CL_LOG(CL_LOG_INFO,"returning, because of no select descriptors (CL_W_SELECT)");
         return CL_RETVAL_NO_SELECT_DESCRIPTORS;
      } else {
         /* (only when not multithreaded): 
          *    don't return immediately when the last call to this function was also
          *    with no possible descriptors! ( which may be caused by a not connectable service )
          *    This must be done to prevent the application to poll endless ( with 100% CPU usage)
          *
          *    we have no file descriptors, but we do a select with standard timeout
          *    because we don't want to overload the cpu by endless trigger() calls 
          *    from application when there is no connection client 
          *    (no descriptors part 1)
          *
          *    we have a handler of the connection list, try to find out if 
          *    this is the first call without guilty file descriptors 
          */
         if ( ldata->select_not_called_count < 3 ) { 
            CL_LOG_INT(CL_LOG_INFO, "no usable file descriptor for select() call nr.:", ldata->select_not_called_count);
            ldata->select_not_called_count += 1;
            cl_raw_list_unlock(connection_list); 
            return CL_RETVAL_NO_SELECT_DESCRIPTORS; 
         } else {
            CL_LOG(CL_LOG_WARNING, "no usable file descriptors (repeated!) - select() will be used for wait");
            ldata->select_not_called_count = 0;
            CL_LOG(CL_LOG_INFO,"no select descriptors");
            cl_raw_list_unlock(connection_list);
            sge_sleep(timeout.tv_sec, timeout.tv_usec);
            return CL_RETVAL_NO_SELECT_DESCRIPTORS;
         }
      }
   }

   
   /* TODO: Fix this problem (multithread mode):
         -  find a way to wake up select when a new connection was added by another thread
            (perhaps with dummy read file descriptor)
   */
    
   if ((nr_of_descriptors != ldata->last_nr_of_descriptors) && 
       (nr_of_descriptors == 1 && service_connection != NULL && do_read_select != 0)) {
      /* This is to return as far as possible if this connection has a service and
          a client was disconnected */

      /* a connection is done and no more connections (beside service connection itself) is alive,
         return to application as far as possible, don't wait for a new connect */
      ldata->last_nr_of_descriptors = nr_of_descriptors;
      cl_raw_list_unlock(connection_list); 
      CL_LOG(CL_LOG_INFO,"last connection closed");
      retval = CL_RETVAL_NO_SELECT_DESCRIPTORS;
   } else {

      ldata->last_nr_of_descriptors = nr_of_descriptors;

      cl_raw_list_unlock(connection_list); 


      errno = 0;
#ifdef USE_POLL
      select_back = poll(ufds, ufds_index, timeout.tv_sec*1000 + timeout.tv_usec/1000);
#else
      select_back = select(max_fd + 1, &my_read_fds, &my_write_fds, NULL, &timeout);
#endif

      my_errno = errno;
      switch(select_back) {
         case -1: {
            /*
             * poll() and select() set errno to EINTR if interrupted
             */
            if (my_errno == EINTR) {
               CL_LOG(CL_LOG_WARNING,"select interrupted (errno=EINTR)");
               retval = CL_RETVAL_SELECT_INTERRUPT;
               break;
            }

            CL_LOG_STR(CL_LOG_ERROR,"select error", strerror(my_errno));
            retval = CL_RETVAL_SELECT_ERROR;
            /*
             * 1) select() set errno to EBADF for not valid file descriptors
             * 2) poll() and select() set errno to EINVAL for file descriptors that are
             *    > OPEN_MAX or FD_SETSIZE
             * => In both cases we check the filedescriptors with get_sock_opt()
             */
            if (my_errno == EBADF || my_errno == EINVAL) {
               if (my_errno == EBADF) {
                  CL_LOG(CL_LOG_WARNING, "errno=EBADF, checking file descriptors");
               } else {
                  CL_LOG(CL_LOG_WARNING, "errno=EINVAL, checking file descriptors");
               }
               /* now check all file descriptors and close those which errors */
               cl_raw_list_lock(connection_list); 
               con_elem = cl_connection_list_get_first_elem(connection_list);
               while(con_elem) {
                  connection  = con_elem->connection;
                  con_private = cl_com_ssl_get_private(connection);
                  socket_error = 0;
#if defined(SOLARIS) && !defined(SOLARIS64)
                  get_sock_opt_error = getsockopt(con_private->sockfd,SOL_SOCKET, SO_ERROR, (void*)&socket_error, &socklen);
#else
                  get_sock_opt_error = getsockopt(con_private->sockfd,SOL_SOCKET, SO_ERROR, &socket_error, &socklen);
#endif
                  if (socket_error != 0 || get_sock_opt_error != 0) {
                     connection->connection_state = CL_CLOSING;
                     connection->connection_sub_state = CL_COM_DO_SHUTDOWN;
                     CL_LOG_STR(CL_LOG_ERROR, "select() or poll() - socket error is: ", strerror(socket_error));
                     cl_commlib_push_application_error(CL_LOG_ERROR, CL_RETVAL_SELECT_ERROR, strerror(socket_error));

                     if (connection->remote            != NULL && 
                         connection->remote->comp_host != NULL &&
                         connection->remote->comp_name != NULL ) {
                        snprintf(tmp_string, 1024, MSG_CL_COMMLIB_CLOSING_SSU,
                                 connection->remote->comp_host,
                                 connection->remote->comp_name,
                                 sge_u32c(connection->remote->comp_id));
                        CL_LOG_STR(CL_LOG_ERROR, "select error:", tmp_string);
                        cl_commlib_push_application_error(CL_LOG_ERROR, CL_RETVAL_SELECT_ERROR, tmp_string);
                     }
                  }
                  con_elem = cl_connection_list_get_next_elem(con_elem);
               } /* while */
               cl_raw_list_unlock(connection_list);
               /* look for a broken external file descriptor call its callback function and remove it afterwards */
               if (handle->file_descriptor_list != NULL) {
                  cl_fd_list_elem_t* elem = NULL;
                  cl_raw_list_lock(handle->file_descriptor_list);
#ifdef USE_POLL
                  for (fd_index = 0; fd_index < ufds_index; fd_index++){
                     if(ufds_con[fd_index] != NULL) {
                        continue;
                     }
                     if(ufds[fd_index].revents & (POLLHUP|POLLERR|POLLNVAL)) {
                        elem = cl_fd_list_get_first_elem(handle->file_descriptor_list);
                        while(elem) {
                           if (elem->data->fd == ufds[fd_index].fd) {
                              elem->data->callback(elem->data->fd,
                                                   elem->data->read_ready, 
                                                   elem->data->write_ready, 
                                                   elem->data->user_data, 
                                                   ufds[fd_index].revents);
                              cl_fd_list_unregister_fd(handle->file_descriptor_list, elem, 0);
                              break;
                           }
                           elem = cl_fd_list_get_next_elem(elem);
                        }
                     }
                  }
#else
                  elem = cl_fd_list_get_first_elem(handle->file_descriptor_list);
                  while(elem) {
                     SGE_STRUCT_STAT buf;
                     if(SGE_FSTAT(elem->data->fd, &buf) == -1) {
                        elem->data->callback(elem->data->fd, elem->data->read_ready, elem->data->write_ready, elem->data->user_data, errno);
                        cl_fd_list_unregister_fd(handle->file_descriptor_list, elem, 0);
                     }
                     elem = cl_fd_list_get_next_elem(elem);
                  }
#endif
                  cl_raw_list_unlock(handle->file_descriptor_list);
               }
               break;
            }
            CL_LOG_INT(CL_LOG_WARNING, "unexpected errno value: ", (int) my_errno);
            break;
         }
         case 0:
#ifdef USE_POLL
            CL_LOG(CL_LOG_INFO,"----->>>>>>>>>>> poll() timeout <<<<<<<<<<<<<<<<<---");
#else
            CL_LOG_INT(CL_LOG_INFO,"----->>>>>>>>>>> select() timeout <<<<<<<<<<<<<<<--- maxfd=", max_fd);
#endif
            retval = CL_RETVAL_SELECT_TIMEOUT;
            break;
         default:
#ifdef USE_POLL
         {
            cl_raw_list_lock(connection_list); 
            /* now set the read flags for connections, where data is available */
            for (fd_index = 0; fd_index < ufds_index ; fd_index++) {
               connection = ufds_con[fd_index];
               if (connection != NULL) {
                  if (do_read_select != 0) {
                     if (ufds[fd_index].revents & (POLLIN|POLLPRI)) {
                        connection->data_read_flag = CL_COM_DATA_READY;
                     }
                     connection->is_read_selected = CL_FALSE;
                  }
                  if (do_write_select != 0) {
                     if (ufds[fd_index].revents & POLLOUT) {
                        connection->fd_ready_for_write = CL_COM_DATA_READY;
                     }
                     connection->is_write_selected = CL_FALSE;
                  }

                  /* Do we have poll errors ? */
                  if ((ufds[fd_index].revents & (POLLERR|POLLHUP|POLLNVAL)) && connection != service_connection) {
                     if (ufds[fd_index].revents & POLLNVAL) {
                         CL_LOG_INT(CL_LOG_WARNING, "poll() revents POLLNVAL is set - checking file descriptor: ", (int)ufds[fd_index].fd);
                     }
                     if (ufds[fd_index].revents & POLLERR) {
                         CL_LOG_INT(CL_LOG_WARNING, "poll() revents POLLERR is set - checking file descriptor: ", (int)ufds[fd_index].fd);
                     }
                     if (ufds[fd_index].revents & POLLHUP) {
                         CL_LOG_INT(CL_LOG_WARNING, "poll() revents POLLHUP is set - checking file descriptor: ", (int)ufds[fd_index].fd);
                     }
                     /* check the connection */
                     con_private = cl_com_ssl_get_private(connection);
                     socket_error = 0;
#if defined(SOLARIS) && !defined(SOLARIS64) 
                     get_sock_opt_error = getsockopt(con_private->sockfd,SOL_SOCKET, SO_ERROR, (void*)&socket_error, &socklen);
#else
                     get_sock_opt_error = getsockopt(con_private->sockfd,SOL_SOCKET, SO_ERROR, &socket_error, &socklen);
#endif
                     if (socket_error != 0 || get_sock_opt_error != 0) {
                        connection->connection_state = CL_CLOSING;
                        connection->connection_sub_state = CL_COM_DO_SHUTDOWN;
                        CL_LOG_STR(CL_LOG_ERROR, "socket error: ", strerror(socket_error));
                        cl_commlib_push_application_error(CL_LOG_ERROR, CL_RETVAL_SELECT_ERROR, strerror(socket_error));
                        if (connection->remote            != NULL && 
                            connection->remote->comp_host != NULL &&
                            connection->remote->comp_name != NULL ) {
                           char tmp_string[1024];
                           snprintf(tmp_string, 1024, MSG_CL_COMMLIB_CLOSING_SSU,
                                    connection->remote->comp_host,
                                    connection->remote->comp_name,
                                    sge_u32c(connection->remote->comp_id));
                           CL_LOG_STR(CL_LOG_ERROR, "poll() revents error:", tmp_string);
                           cl_commlib_push_application_error(CL_LOG_ERROR, CL_RETVAL_SELECT_ERROR, tmp_string);
                        }
                     }
                  }
               }
               /* look for external file descriptors and set its ready flags */
               if (handle->file_descriptor_list != NULL) {
                  cl_fd_list_elem_t *elem = NULL;
                  cl_raw_list_lock(handle->file_descriptor_list);
                  elem = cl_fd_list_get_first_elem(handle->file_descriptor_list);
                  while(elem) {
                     if (elem->data->fd == ufds[fd_index].fd) {
                        if(do_read_select == 1) {
                           if(ufds[fd_index].revents & (POLLIN|POLLPRI)){
                              elem->data->read_ready = CL_TRUE;
                           }else{
                              elem->data->read_ready = CL_FALSE;
                           }
                        }
                        if(do_write_select == 1) {
                           if(ufds[fd_index].revents & POLLOUT){
                              elem->data->write_ready = CL_TRUE;
                           }else{
                              elem->data->write_ready = CL_FALSE;
                           }
                        }
                     }
                     elem = cl_fd_list_get_next_elem(elem);
                  }
                  cl_raw_list_unlock(handle->file_descriptor_list);
               }
            }
            cl_raw_list_unlock(connection_list);
            return CL_RETVAL_OK; /* OK - done */
         }
#else
         {
            cl_fd_list_elem_t* fd_elem = NULL;
            cl_raw_list_lock(connection_list); 
            /* now set the read flags for connections, where data is available */
            con_elem = cl_connection_list_get_first_elem(connection_list);
            while(con_elem) {
               connection  = con_elem->connection;
               con_private = cl_com_ssl_get_private(connection);
               if (do_read_select != 0) {
                  if (con_private->sockfd >= 0 && con_private->sockfd <= max_fd) {
                     if (FD_ISSET(con_private->sockfd, &my_read_fds)) {
                        connection->data_read_flag = CL_COM_DATA_READY;
                     }
                  }
                  connection->is_read_selected = CL_FALSE;
               }
               if (do_write_select != 0) {
                  if (con_private->sockfd >= 0 && con_private->sockfd <= max_fd) {
                     if (FD_ISSET(con_private->sockfd, &my_write_fds)) {
                        connection->fd_ready_for_write = CL_COM_DATA_READY;
                     }
                  }
                  connection->is_write_selected = CL_FALSE;
               }
               con_elem = cl_connection_list_get_next_elem(con_elem);
            } /* while */
            cl_raw_list_unlock(connection_list);

            if (server_fd != -1) {
               if (FD_ISSET(server_fd, &my_read_fds)) {
                  service_connection->data_read_flag = CL_COM_DATA_READY;
               }
               service_connection->is_read_selected = CL_FALSE;
            }

            /* look for ready external file descriptors and set their ready flags */
            if (handle->file_descriptor_list != NULL){
               cl_raw_list_lock(handle->file_descriptor_list);
               fd_elem = cl_fd_list_get_first_elem(handle->file_descriptor_list);

               while(fd_elem) {
                  if (FD_ISSET(fd_elem->data->fd, &my_read_fds)) {
                     fd_elem->data->read_ready = CL_TRUE;
                  }else{
                     fd_elem->data->read_ready = CL_FALSE;
                  }
                  if (FD_ISSET(fd_elem->data->fd, &my_write_fds)) {
                     fd_elem->data->write_ready = CL_TRUE;
                  }else{
                     fd_elem->data->write_ready = CL_FALSE;
                  }
                  fd_elem = cl_fd_list_get_next_elem(fd_elem);
               }
               cl_raw_list_unlock(handle->file_descriptor_list);
            }
            return CL_RETVAL_OK; /* OK - done */
         }
#endif
      } /* switch */
   }
   /* 
    * reset all is_XXXXX_selected flags for the connection
    */
#ifdef USE_POLL
   cl_raw_list_lock(connection_list); 
   for (fd_index = 0; fd_index < ufds_index ; fd_index++) {
      connection = ufds_con[fd_index];
      if (connection != NULL) {
         if (do_read_select != 0) {
            connection->is_read_selected = CL_FALSE;
         }
         if (do_write_select != 0) {
            connection->is_write_selected = CL_FALSE;
         }
      }
   }
   cl_raw_list_unlock(connection_list);
#else
   cl_raw_list_lock(connection_list); 
   con_elem = cl_connection_list_get_first_elem(connection_list);
   while(con_elem) {
      connection  = con_elem->connection;
      if (do_read_select != 0) {
         connection->is_read_selected = CL_FALSE;
      }
      if (do_write_select != 0) {
         connection->is_write_selected = CL_FALSE;
      }
      con_elem = cl_connection_list_get_next_elem(con_elem);
   }
   cl_raw_list_unlock(connection_list);

   if (server_fd != -1) {
      service_connection->is_read_selected = CL_FALSE;
   }
#endif
   return retval;
}

#ifdef __CL_FUNCTION__
#undef __CL_FUNCTION__
#endif
#define __CL_FUNCTION__ "cl_com_ssl_write()"
int cl_com_ssl_write(cl_com_connection_t* connection, cl_byte_t* message, unsigned long size, unsigned long *only_one_write) {
   size_t int_size = sizeof(int);
   struct timeval now;
   cl_com_ssl_private_t* private = NULL;
   long data_written = 0;
   int ssl_error;

   if (only_one_write == NULL) {
      CL_LOG(CL_LOG_ERROR,"only_one_write == NULL");
      return CL_RETVAL_PARAMS;
   }

   if (connection == NULL) {
      CL_LOG(CL_LOG_ERROR,"no connection object");
      return CL_RETVAL_PARAMS;
   }

   private = cl_com_ssl_get_private(connection);
   if (private == NULL) {
      return CL_RETVAL_NO_FRAMEWORK_INIT;
   }

   if ( message == NULL) {
      CL_LOG(CL_LOG_ERROR,"no message to write");
      return CL_RETVAL_PARAMS;
   }
   
   if ( size == 0 ) {
      CL_LOG(CL_LOG_ERROR,"data size is zero");
      return CL_RETVAL_PARAMS;
   }

   if (private->sockfd < 0) {
      CL_LOG(CL_LOG_ERROR,"no file descriptor");
      return CL_RETVAL_PARAMS;
   }

   if (size > CL_DEFINE_MAX_MESSAGE_LENGTH) {
      CL_LOG_INT(CL_LOG_ERROR,"data to write is > max message length =", CL_DEFINE_MAX_MESSAGE_LENGTH);
      cl_commlib_push_application_error(CL_LOG_ERROR, CL_RETVAL_MAX_READ_SIZE, NULL);
      return CL_RETVAL_MAX_READ_SIZE;
   }

   if (int_size < CL_COM_SSL_FRAMEWORK_MIN_INT_SIZE && size > CL_COM_SSL_FRAMEWORK_MAX_INT ) {
      CL_LOG_INT(CL_LOG_ERROR,"can't send such a long message, because on this architecture the sizeof integer is", (int)int_size);
      cl_commlib_push_application_error(CL_LOG_ERROR, CL_RETVAL_MAX_READ_SIZE, MSG_CL_COMMLIB_SSL_MESSAGE_SIZE_EXEED_ERROR);
      return CL_RETVAL_MAX_READ_SIZE;
   }

   cl_com_ssl_func__ERR_clear_error();
   data_written = cl_com_ssl_func__SSL_write(private->ssl_obj, message, (int)size);
   if (data_written <= 0) {
      /* Try to find out more about the connect error */
      ssl_error = cl_com_ssl_func__SSL_get_error(private->ssl_obj, data_written);
      private->ssl_last_error = ssl_error;
      switch(ssl_error) {
#ifdef CL_COM_ENABLE_SSL_THREAD_RETRY_BUGFIX
         case SSL_ERROR_SYSCALL:
#endif
         case SSL_ERROR_WANT_READ: 
         case SSL_ERROR_WANT_WRITE: {
            CL_LOG_STR(CL_LOG_INFO,"ssl_error:", cl_com_ssl_get_error_text(ssl_error));
            break;
         }
         default: {
            CL_LOG_STR(CL_LOG_ERROR,"SSL write error", cl_com_ssl_get_error_text(ssl_error));
            cl_com_ssl_log_ssl_errors(__CL_FUNCTION__);
            return CL_RETVAL_SEND_ERROR;
         }
      }
      data_written = 0;
   }

   *only_one_write = data_written;
   if (data_written != size) {
      gettimeofday(&now,NULL);
      if ( now.tv_sec >= connection->write_buffer_timeout_time ) {
         CL_LOG(CL_LOG_ERROR,"send timeout error");
         return CL_RETVAL_SEND_TIMEOUT;
      }
      return CL_RETVAL_UNCOMPLETE_WRITE;
   }
   return CL_RETVAL_OK;
}

#ifdef __CL_FUNCTION__
#undef __CL_FUNCTION__
#endif
#define __CL_FUNCTION__ "cl_com_ssl_read()"
int cl_com_ssl_read(cl_com_connection_t* connection, cl_byte_t* message, unsigned long size, unsigned long *only_one_read) {
   size_t int_size = sizeof(int);
   struct timeval now;
   cl_com_ssl_private_t* private = NULL;
   long data_read = 0;
   int ssl_error;

   if (connection == NULL || only_one_read == NULL) {
      CL_LOG(CL_LOG_ERROR,"no connection object");
      return CL_RETVAL_PARAMS;
   }

   private = cl_com_ssl_get_private(connection);
   if (private == NULL) {
      return CL_RETVAL_NO_FRAMEWORK_INIT;
   }

   if (message == NULL) {
      CL_LOG(CL_LOG_ERROR,"no message buffer");
      return CL_RETVAL_PARAMS;
   }

   if (private->sockfd < 0) {
      CL_LOG(CL_LOG_ERROR,"no file descriptor");
      return CL_RETVAL_PARAMS;
   }


   if (size == 0) {
      CL_LOG(CL_LOG_ERROR,"no data size");
      return CL_RETVAL_PARAMS;
   }

   if (size > CL_DEFINE_MAX_MESSAGE_LENGTH) {
      CL_LOG_INT(CL_LOG_ERROR,"data to read is > max message length =", CL_DEFINE_MAX_MESSAGE_LENGTH );
      cl_commlib_push_application_error(CL_LOG_ERROR, CL_RETVAL_MAX_READ_SIZE, NULL);
      return CL_RETVAL_MAX_READ_SIZE;
   }

   if (int_size < CL_COM_SSL_FRAMEWORK_MIN_INT_SIZE && size > CL_COM_SSL_FRAMEWORK_MAX_INT ) {
      CL_LOG_INT(CL_LOG_ERROR,"can't read such a long message, because on this architecture the sizeof integer is", (int)int_size );
      cl_commlib_push_application_error(CL_LOG_ERROR, CL_RETVAL_MAX_READ_SIZE, MSG_CL_COMMLIB_SSL_MESSAGE_SIZE_EXEED_ERROR);
      return CL_RETVAL_MAX_READ_SIZE;
   }

   cl_com_ssl_func__ERR_clear_error();
   data_read = cl_com_ssl_func__SSL_read(private->ssl_obj, message, (int)size);
   if (data_read <= 0) {

      if (data_read == 0) {
         CL_LOG(CL_LOG_WARNING, "SSL_read() returned 0 - checking ssl_error ...");
      }
      /* Try to find out more about the connect error */
      ssl_error = cl_com_ssl_func__SSL_get_error(private->ssl_obj, data_read);
      private->ssl_last_error = ssl_error;
      switch(ssl_error) {
         case SSL_ERROR_NONE: {
            CL_LOG_STR(CL_LOG_INFO, "ssl_error:", cl_com_ssl_get_error_text(ssl_error));
            break;
         }
#ifdef CL_COM_ENABLE_SSL_THREAD_RETRY_BUGFIX
         case SSL_ERROR_SYSCALL:
#endif
         case SSL_ERROR_WANT_READ: 
         case SSL_ERROR_WANT_WRITE: {
            CL_LOG_STR(CL_LOG_INFO,"ssl_error:", cl_com_ssl_get_error_text(ssl_error));
            break;
         }
         default: {
            CL_LOG_STR(CL_LOG_ERROR,"SSL read error:", cl_com_ssl_get_error_text(ssl_error));
            cl_com_ssl_log_ssl_errors(__CL_FUNCTION__);
            return CL_RETVAL_READ_ERROR;
         }
      }
      data_read = 0;
   }

   *only_one_read = data_read;
   if (data_read != size) {
      gettimeofday(&now,NULL);
      if ( now.tv_sec >= connection->read_buffer_timeout_time ) {
         return CL_RETVAL_READ_TIMEOUT;
      }
      return CL_RETVAL_UNCOMPLETE_READ;
   }
   return CL_RETVAL_OK;
}

#ifdef __CL_FUNCTION__
#undef __CL_FUNCTION__
#endif
#define __CL_FUNCTION__ "cl_com_ssl_get_unique_id()"
int cl_com_ssl_get_unique_id(cl_com_handle_t* handle, 
                             char* un_resolved_hostname, char* component_name, unsigned long component_id, 
                             char** uniqueIdentifier ) {
   char* unique_hostname = NULL;
   cl_com_endpoint_t client;
   cl_com_connection_t* connection = NULL;
   cl_connection_list_elem_t* elem = NULL;
   cl_com_ssl_private_t* private = NULL;
   int function_return_value = CL_RETVAL_UNKNOWN_ENDPOINT;
   int return_value = CL_RETVAL_OK;

   if (handle               == NULL || 
       un_resolved_hostname == NULL || 
       component_name       == NULL ||
       uniqueIdentifier     == NULL   ) {
      return CL_RETVAL_PARAMS;
   }

   if (*uniqueIdentifier != NULL) {
      CL_LOG(CL_LOG_ERROR,"uniqueIdentifer is already set");
      return CL_RETVAL_PARAMS;
   }

   /* resolve hostname */
   return_value = cl_com_cached_gethostbyname(un_resolved_hostname, &unique_hostname,NULL, NULL, NULL);
   if (return_value != CL_RETVAL_OK) {
      CL_LOG(CL_LOG_ERROR,cl_get_error_text(return_value));
      return return_value;
   }

   /* setup endpoint */
   client.comp_host = unique_hostname;
   client.comp_name = component_name;
   client.comp_id   = component_id;

   /* lock handle connection list */
   cl_raw_list_lock(handle->connection_list);

   elem = cl_connection_list_get_first_elem(handle->connection_list);
   while(elem) {
      connection = elem->connection;
      if (connection != NULL) {
         /* find correct client */
         if ( cl_com_compare_endpoints(connection->remote, &client) ) {
            private = cl_com_ssl_get_private(connection);
            if (private != NULL) {
               if (private->ssl_unique_id != NULL) {
                  *uniqueIdentifier = strdup(private->ssl_unique_id);
                  if ( *uniqueIdentifier == NULL) {
                     function_return_value = CL_RETVAL_MALLOC;
                  } else {
                     function_return_value = CL_RETVAL_OK;
                  }
                  break;
               }
            }
         }
      }
      elem = cl_connection_list_get_next_elem(elem);
   }

   /* unlock handle connection list */
   cl_raw_list_unlock(handle->connection_list);
   free(unique_hostname);
   unique_hostname = NULL;
   return function_return_value;
}

/* fill private structure */
/* is_server = CL_TRUE  -> peer certificate comes from client */
/* is_server = CL_FALSE -> peer certificate comes from server */
static int cl_com_ssl_fill_private_from_peer_cert(cl_com_ssl_private_t *private, cl_bool_t is_server) {

      X509* peer = NULL;
      char peer_CN[256];

      if (private == NULL) {
        return CL_RETVAL_SSL_CERTIFICATE_ERROR;
      } 

      if (is_server == CL_TRUE) {
        CL_LOG(CL_LOG_INFO, "Checking Client Authentication");
      } else {  
        CL_LOG(CL_LOG_INFO, "Checking Server Authentication");
      }  
      
      if (cl_com_ssl_func__SSL_get_verify_result(private->ssl_obj) != X509_V_OK) {
         if (is_server == CL_TRUE) {
            CL_LOG(CL_LOG_ERROR,"client certificate doesn't verify");
            cl_commlib_push_application_error(CL_LOG_ERROR, CL_RETVAL_SSL_CERTIFICATE_ERROR, MSG_CL_COMMLIB_SSL_CLIENT_CERTIFICATE_ERROR);
         } else {   
            CL_LOG(CL_LOG_ERROR,"server certificate doesn't verify");
            cl_commlib_push_application_error(CL_LOG_ERROR, CL_RETVAL_SSL_CERTIFICATE_ERROR, MSG_CL_COMMLIB_CHECK_SSL_CERTIFICATE );
         }
         cl_com_ssl_log_ssl_errors(__CL_FUNCTION__);
         return CL_RETVAL_SSL_CERTIFICATE_ERROR;
      }

      /* Check the common name */
      peer = cl_com_ssl_func__SSL_get_peer_certificate(private->ssl_obj);
      if (peer != NULL) {
         char uniqueIdentifier[1024];
         cl_com_ssl_func__X509_NAME_get_text_by_NID(cl_com_ssl_func__X509_get_subject_name(peer),
                                                    NID_commonName, peer_CN, 256);


         if (peer_CN != NULL) {
            int retval;
            CL_LOG_STR(CL_LOG_INFO,"calling ssl verify callback with peer name:",peer_CN);
            retval = private->ssl_setup->ssl_verify_func(CL_SSL_PEER_NAME, is_server, peer_CN);

            if ( retval != CL_TRUE) {
               CL_LOG(CL_LOG_ERROR, "commlib ssl verify callback function failed in peer name check");
               cl_commlib_push_application_error(CL_LOG_ERROR, CL_RETVAL_SSL_PEER_CERTIFICATE_ERROR, MSG_CL_COMMLIB_SSL_VERIFY_CALLBACK_FUNC_ERROR);
               cl_com_ssl_log_ssl_errors(__CL_FUNCTION__);
               cl_com_ssl_func__X509_free(peer);
               peer = NULL;
               return CL_RETVAL_SSL_PEER_CERTIFICATE_ERROR;
            }
         } else {
            CL_LOG(CL_LOG_ERROR, "could not get peer_CN from peer certificate");
            cl_commlib_push_application_error(CL_LOG_ERROR, CL_RETVAL_SSL_PEER_CERTIFICATE_ERROR, MSG_CL_COMMLIB_SSL_PEER_CERT_GET_ERROR);
            cl_com_ssl_log_ssl_errors(__CL_FUNCTION__);
            cl_com_ssl_func__X509_free(peer);
            peer = NULL;
            return CL_RETVAL_SSL_PEER_CERTIFICATE_ERROR;
         }
         
         if (cl_com_ssl_func__X509_NAME_get_text_by_OBJ(cl_com_ssl_func__X509_get_subject_name(peer), 
                                        cl_com_ssl_func__OBJ_nid2obj(NID_userId), 
                                        uniqueIdentifier, 
                                        sizeof(uniqueIdentifier))) {
            if (uniqueIdentifier != NULL) {
               CL_LOG_STR(CL_LOG_INFO,"unique identifier:", uniqueIdentifier);
               CL_LOG_STR(CL_LOG_INFO,"calling ssl_verify_func with user name:",uniqueIdentifier);
               if ( private->ssl_setup->ssl_verify_func(CL_SSL_USER_NAME, is_server, uniqueIdentifier) != CL_TRUE) {
                  CL_LOG(CL_LOG_ERROR, "commlib ssl verify callback function failed in user name check");
                  cl_commlib_push_application_error(CL_LOG_ERROR, CL_RETVAL_SSL_PEER_CERTIFICATE_ERROR, MSG_CL_COMMLIB_SSL_USER_ID_VERIFY_ERROR);
                  cl_com_ssl_log_ssl_errors(__CL_FUNCTION__);
                  cl_com_ssl_func__X509_free(peer);
                  peer = NULL;
                  return CL_RETVAL_SSL_PEER_CERTIFICATE_ERROR;
               }
               /* store uniqueIdentifier into private structure */
               private->ssl_unique_id = strdup(uniqueIdentifier);
               if ( private->ssl_unique_id == NULL) {
                  CL_LOG(CL_LOG_ERROR, "could not malloc unique identifier memory");
                  cl_com_ssl_log_ssl_errors(__CL_FUNCTION__);
                  cl_com_ssl_func__X509_free(peer);
                  peer = NULL;
                  return CL_RETVAL_MALLOC;
               }

            } else {
               CL_LOG(CL_LOG_ERROR, "could not get uniqueIdentifier from peer certificate");
               cl_commlib_push_application_error(CL_LOG_ERROR, CL_RETVAL_SSL_PEER_CERTIFICATE_ERROR, MSG_CL_COMMLIB_SSL_USER_ID_GET_ERROR);
               cl_com_ssl_log_ssl_errors(__CL_FUNCTION__);
               cl_com_ssl_func__X509_free(peer);
               peer = NULL;
               return CL_RETVAL_SSL_PEER_CERTIFICATE_ERROR;
            }
         } else {
            CL_LOG(CL_LOG_ERROR,"client certificate error: could not get identifier");
            cl_commlib_push_application_error(CL_LOG_ERROR, CL_RETVAL_SSL_PEER_CERTIFICATE_ERROR, MSG_CL_COMMLIB_SSL_USER_ID_GET_ERROR);
            cl_com_ssl_log_ssl_errors(__CL_FUNCTION__);
            cl_com_ssl_func__X509_free(peer);
            peer = NULL;
            return CL_RETVAL_SSL_PEER_CERTIFICATE_ERROR;
         }
         cl_com_ssl_func__X509_free(peer);
         peer = NULL;
      } else {
         if (is_server == CL_TRUE) {
            CL_LOG(CL_LOG_ERROR,"client did not send peer certificate");
            cl_commlib_push_application_error(CL_LOG_ERROR, CL_RETVAL_SSL_PEER_CERTIFICATE_ERROR, MSG_CL_COMMLIB_SSL_CLIENT_CERT_NOT_SENT_ERROR);
         } else {
            CL_LOG(CL_LOG_ERROR,"service did not send peer certificate");
            cl_commlib_push_application_error(CL_LOG_ERROR, CL_RETVAL_SSL_PEER_CERTIFICATE_ERROR, MSG_CL_COMMLIB_SSL_SERVER_CERT_NOT_SENT_ERROR);
         }
         cl_com_ssl_log_ssl_errors(__CL_FUNCTION__);
         return CL_RETVAL_SSL_PEER_CERTIFICATE_ERROR;
      }
      return CL_RETVAL_OK;
}
#else

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <netdb.h>
#include <string.h>
#include <netinet/tcp.h> 
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <limits.h>

#include "cl_errors.h"
#include "cl_connection_list.h"
#include "cl_ssl_framework.h"
#include "cl_communication.h"
#include "cl_commlib.h"
#include "msg_commlib.h"


/* dummy functions for compilation without openssl lib */
/* ssl specific functions */
int cl_com_ssl_framework_setup(void) {
   return CL_RETVAL_OK;
}

int cl_com_ssl_framework_cleanup(void) {
   return CL_RETVAL_OK;
}



/* debug functions */
void cl_dump_ssl_private(cl_com_connection_t* connection) {
   cl_commlib_push_application_error(CL_LOG_ERROR, CL_RETVAL_SSL_NOT_SUPPORTED, "");
}

/* global security function */
int cl_com_ssl_get_unique_id(cl_com_handle_t* handle, 
                             char* un_resolved_hostname, char* component_name, unsigned long component_id, 
                             char** uniqueIdentifier ) {
   cl_commlib_push_application_error(CL_LOG_ERROR, CL_RETVAL_SSL_NOT_SUPPORTED, "");
   return CL_RETVAL_SSL_NOT_SUPPORTED;
}



/* get/set functions */
int cl_com_ssl_get_connect_port(cl_com_connection_t* connection,
                                int*                 port) {
   cl_commlib_push_application_error(CL_LOG_ERROR, CL_RETVAL_SSL_NOT_SUPPORTED, "");
   return CL_RETVAL_SSL_NOT_SUPPORTED;
}

int cl_com_ssl_set_connect_port(cl_com_connection_t* connection,
                                int                  port) {
   cl_commlib_push_application_error(CL_LOG_ERROR, CL_RETVAL_SSL_NOT_SUPPORTED, "");
   return CL_RETVAL_SSL_NOT_SUPPORTED;
}

int cl_com_ssl_get_service_port(cl_com_connection_t* connection,
                                int*                 port) {
   cl_commlib_push_application_error(CL_LOG_ERROR, CL_RETVAL_SSL_NOT_SUPPORTED, "");
   return CL_RETVAL_SSL_NOT_SUPPORTED;
}

int cl_com_ssl_get_fd(cl_com_connection_t* connection,
                      int*                 fd) {
   cl_commlib_push_application_error(CL_LOG_ERROR, CL_RETVAL_SSL_NOT_SUPPORTED, "");
   return CL_RETVAL_SSL_NOT_SUPPORTED;
}

int cl_com_ssl_get_client_socket_in_port(cl_com_connection_t* connection,
                                         int*                 port) {
   cl_commlib_push_application_error(CL_LOG_ERROR, CL_RETVAL_SSL_NOT_SUPPORTED, "");
   return CL_RETVAL_SSL_NOT_SUPPORTED;
}

/* create new connection object */
int cl_com_ssl_setup_connection(cl_com_connection_t**         connection,
                                int                           server_port,
                                int                           connect_port,
                                cl_xml_connection_type_t      data_flow_type,
                                cl_xml_connection_autoclose_t auto_close_mode,
                                cl_framework_t                framework_type,
                                cl_xml_data_format_t          data_format_type,
                                cl_tcp_connect_t              tcp_connect_mode,
                                cl_ssl_setup_t*               ssl_setup) {
   cl_commlib_push_application_error(CL_LOG_ERROR, CL_RETVAL_SSL_NOT_SUPPORTED, "");
   return CL_RETVAL_SSL_NOT_SUPPORTED;
}



/* create/destroy connection functions */
int cl_com_ssl_open_connection(cl_com_connection_t*   connection,
                               int                    timeout) {
   cl_commlib_push_application_error(CL_LOG_ERROR, CL_RETVAL_SSL_NOT_SUPPORTED, "");
   return CL_RETVAL_SSL_NOT_SUPPORTED;
}

int cl_com_ssl_close_connection(cl_com_connection_t** connection) {
   cl_commlib_push_application_error(CL_LOG_ERROR, CL_RETVAL_SSL_NOT_SUPPORTED, "");
   return CL_RETVAL_SSL_NOT_SUPPORTED;
}

int cl_com_ssl_connection_complete_shutdown(cl_com_connection_t*  connection) {
   cl_commlib_push_application_error(CL_LOG_ERROR, CL_RETVAL_SSL_NOT_SUPPORTED, "");
   return CL_RETVAL_SSL_NOT_SUPPORTED;
}

int cl_com_ssl_connection_complete_accept(cl_com_connection_t*  connection,
                                          long                  timeout) {
   cl_commlib_push_application_error(CL_LOG_ERROR, CL_RETVAL_SSL_NOT_SUPPORTED, "");
   return CL_RETVAL_SSL_NOT_SUPPORTED;
}



/* read/write functions */
int cl_com_ssl_write(cl_com_connection_t* connection,
                     cl_byte_t*       message,
                     unsigned long    size,
                     unsigned long*   only_one_write) {
   cl_commlib_push_application_error(CL_LOG_ERROR, CL_RETVAL_SSL_NOT_SUPPORTED, "");
   return CL_RETVAL_SSL_NOT_SUPPORTED;
}

int cl_com_ssl_read(cl_com_connection_t* connection,
                    cl_byte_t*        message,
                    unsigned long     size,
                    unsigned long*    only_one_read) {
   cl_commlib_push_application_error(CL_LOG_ERROR, CL_RETVAL_SSL_NOT_SUPPORTED, "");
   return CL_RETVAL_SSL_NOT_SUPPORTED;
}

int cl_com_ssl_read_GMSH(cl_com_connection_t*        connection,
                         unsigned long*              only_one_read) {
   cl_commlib_push_application_error(CL_LOG_ERROR, CL_RETVAL_SSL_NOT_SUPPORTED, "");
   return CL_RETVAL_SSL_NOT_SUPPORTED;
}



/* create service, accept new connections */
int cl_com_ssl_connection_request_handler_setup(cl_com_connection_t* connection, cl_bool_t only_prepare_service) {
   cl_commlib_push_application_error(CL_LOG_ERROR, CL_RETVAL_SSL_NOT_SUPPORTED, "");
   return CL_RETVAL_SSL_NOT_SUPPORTED;
}

int cl_com_ssl_connection_request_handler(cl_com_connection_t*   connection,
                                          cl_com_connection_t**  new_connection) {
   cl_commlib_push_application_error(CL_LOG_ERROR, CL_RETVAL_SSL_NOT_SUPPORTED, "");
   return CL_RETVAL_SSL_NOT_SUPPORTED;
}

int cl_com_ssl_connection_request_handler_cleanup(cl_com_connection_t* connection) {
   cl_commlib_push_application_error(CL_LOG_ERROR, CL_RETVAL_SSL_NOT_SUPPORTED, "");
   return CL_RETVAL_SSL_NOT_SUPPORTED;
}

/* select mechanism */
#ifdef USE_POLL
int cl_com_ssl_open_connection_request_handler(cl_com_poll_t*        poll_handle,
                                               cl_com_handle_t*      handle, 
                                               cl_raw_list_t*        connection_list, 
                                               cl_com_connection_t*  service_connection,
                                               int                   timeout_val_sec,
                                               int                   timeout_val_usec, 
                                               cl_select_method_t    select_mode)
#else
int cl_com_ssl_open_connection_request_handler(cl_com_handle_t*      handle,
                                               cl_raw_list_t*        connection_list,
                                               cl_com_connection_t*  service_connection,
                                               int                   timeout_val_sec,
                                               int                   timeout_val_usec,
                                               cl_select_method_t    select_mode)
#endif
{
   cl_commlib_push_application_error(CL_LOG_ERROR, CL_RETVAL_SSL_NOT_SUPPORTED, "");
   return CL_RETVAL_SSL_NOT_SUPPORTED;
}


#endif /* SECURE */

