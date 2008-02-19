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
 *   Copyright: 2003 by Sun Microsystems, Inc.
 * 
 *   All Rights Reserved.
 * 
 ************************************************************************/
/*___INFO__MARK_END__*/

#include <signal.h>
#include <pthread.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>

#include "basis_types.h"
#include "sge_qmaster_threads.h"
#include "sgermon.h"
#include "sge_mt_init.h"
#include "sge_prog.h"
#include "sge_log.h"
#include "sge_unistd.h"
#include "sge_answer.h"
#include "setup_qmaster.h"
#include "sge_security.h"
#include "sge_manop.h"
#include "sge_mtutil.h"
#include "sge_lock.h"
#include "sge_qmaster_process_message.h"
#include "sge_event_master.h"
#include "sge_persistence_qmaster.h"
#include "sge_reporting_qmaster.h"
#include "sge_qmaster_timed_event.h"
#include "sge_host_qmaster.h"
#include "sge_userprj_qmaster.h"
#include "sge_give_jobs.h"
#include "sge_all_listsL.h"
#include "sge_calendar_qmaster.h"
#include "sge_time.h"
#include "lock.h"
#include "qmaster_heartbeat.h"
#include "shutdown.h"
#include "sge_spool.h"
#include "cl_commlib.h"
#include "sge_uidgid.h"
#include "sge_bootstrap.h"
#include "msg_common.h"
#include "msg_qmaster.h"
#include "msg_daemons_common.h"
#include "msg_utilib.h"  /* remove once 'sge_daemonize_qmaster' did become 'sge_daemonize' */
#include "sge.h"
#include "sge_qmod_qmaster.h"
#include "reschedule.h"
#include "sge_job_qmaster.h"
#include "sge_profiling.h"
#include "sgeobj/sge_conf.h"
#include "qm_name.h"
#include "setup_path.h"
#include "sge_advance_reservation_qmaster.h"
#include "sge_sched_process_events.h"
#include "sge_follow.h"

#include "uti/sge_os.h"
#include "uti/sge_thread_ctrl.h"

#include "sge_thread_main.h"
#include "sge_thread_signaler.h"
#include "sge_thread_listener.h"
#include "sge_thread_worker.h"
#include "sge_thread_scheduler.h"

void
sge_signaler_initialize(sge_gdi_ctx_class_t *ctx)
{
   cl_thread_settings_t* dummy_thread_p = NULL;
   dstring thread_name = DSTRING_INIT; 

   DENTER(TOP_LAYER, "sge_signaler_initialize");

   sge_dstring_sprintf(&thread_name, "%s%03d", threadnames[SIGNALER_THREAD], 0);
   cl_thread_list_setup(&(Main_Control.signal_thread_pool), "signal thread pool");
   cl_thread_list_create_thread(Main_Control.signal_thread_pool, &dummy_thread_p,
                                NULL, sge_dstring_get_string(&thread_name), 0, 
                                sge_signaler_main, NULL, NULL);
   sge_dstring_free(&thread_name);
   DRETURN_VOID;
}

void 
sge_signaler_initiate_termination(void)
{
   cl_thread_settings_t* thread = NULL;
   DENTER(TOP_LAYER, "sge_signaler_initiate_termination");

   thread = cl_thread_list_get_first_thread(Main_Control.signal_thread_pool);
   if (thread != NULL) {
      pthread_kill(*(thread->thread_pointer), SIGINT);
      INFO((SGE_EVENT, "send SIGINT to "SFN"\n", thread->thread_name));
   }
   DRETURN_VOID;
}

void
sge_signaler_terminate(void)
{
   cl_thread_settings_t* thread = NULL;
   DENTER(TOP_LAYER, "sge_signaler_terminate");

   thread = cl_thread_list_get_first_thread(Main_Control.signal_thread_pool);
   if (thread != NULL) {
      DPRINTF((SFN" gets canceled\n", thread->thread_name));
      cl_thread_list_delete_thread(Main_Control.signal_thread_pool, thread);
   }
   DRETURN_VOID;
}


/****** qmaster/sge_qmaster_main/signal_thread() *******************************
*  NAME
*     signal_thread() -- signal thread function
*
*  SYNOPSIS
*     void* signal_thread(void* anArg) 
*
*  FUNCTION
*     Signal handling thread function. Establish recognized signal set. Enter
*     signal wait loop. Wait for signal. Handle signal.
*
*     If signal is 'SIGINT' or 'SIGTERM', kick-off shutdown and invalidate
*     signal thread.
*
*     NOTE: The signal thread will terminate on return of this function.
*
*  INPUTS
*     void* anArg - not used 
*
*  RESULT
*     void* - none 
*
*  NOTES
*     MT-NOTE: signal_thread() is a thread function. Do NOT use this function
*     MT-NOTE: in any other way!
*
*******************************************************************************/
void* sge_signaler_main(void* arg)
{
   cl_thread_settings_t *thread_config = (cl_thread_settings_t*)arg;
   bool is_continue = true;
   sigset_t sig_set;
   int sig_num;
   time_t next_prof_output = 0;
   monitoring_t monitor;
   sge_gdi_ctx_class_t *ctx = NULL;

   DENTER(TOP_LAYER, "sge_signaler_main");

   cl_thread_func_startup(thread_config);

   sge_monitor_init(&monitor, thread_config->thread_name, NONE_EXT, ST_WARNING, ST_ERROR);
   sge_qmaster_thread_init(&ctx, QMASTER, SIGNALER_THREAD, true);

   sigemptyset(&sig_set);
   sigaddset(&sig_set, SIGINT);
   sigaddset(&sig_set, SIGTERM);


   /* register at profiling module */
   set_thread_name(pthread_self(), "Signal Thread");
   conf_update_thread_profiling("Signal Thread");

   while (is_continue) {
      /*
       * Wait for signals
       */
      MONITOR_IDLE_TIME(sigwait(&sig_set, &sig_num), (&monitor), mconf_get_monitor_time(),
                        mconf_is_monitor_message());

      thread_start_stop_profiling();

      DPRINTF(("%s: got signal %d\n", thread_config->thread_name, sig_num));

      switch (sig_num) {
         case SIGINT:
         case SIGTERM:
            /*
             * Notify the main thread. It is waiting and will shutdown all other
             * threads if it gets signalled
             */
            sge_thread_notify_all_waiting();

            /*
             * Now this thread can exit. The main thread does the remaining
             * shutdown activities
             */
            is_continue = false;
            break;
         default:
            ERROR((SGE_EVENT, MSG_QMASTER_UNEXPECTED_SIGNAL_I, sig_num));
      }

      sge_monitor_output(&monitor);

      thread_output_profiling("signal thread profiling summary:\n",
                              &next_prof_output);

   }

   sge_monitor_free(&monitor);

   DRETURN(NULL);
} 
