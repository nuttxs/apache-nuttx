/****************************************************************************
 * sched/clock/clock_initialize.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/compiler.h>

#include <stdint.h>
#include <time.h>
#include <errno.h>
#include <debug.h>

#ifdef CONFIG_RTC
#  include <nuttx/irq.h>
#endif

#include <nuttx/arch.h>
#include <nuttx/clock.h>
#include <nuttx/trace.h>

#include <nuttx/spinlock.h>

#include "clock/clock.h"
#ifdef CONFIG_CLOCK_TIMEKEEPING
#  include "clock/clock_timekeeping.h"
#endif

/****************************************************************************
 * Public Data
 ****************************************************************************/

#if !defined(CONFIG_SCHED_TICKLESS) && \
    !defined(CONFIG_ALARM_ARCH) && !defined(CONFIG_TIMER_ARCH)
volatile clock_t g_system_ticks = INITIAL_SYSTEM_TIMER_TICKS;
#endif

#ifndef CONFIG_CLOCK_TIMEKEEPING
struct timespec   g_basetime;
spinlock_t g_basetime_lock = SP_UNLOCKED;
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: clock_basetime
 *
 * Description:
 *   Get the initial time value from the best source available.
 *
 ****************************************************************************/

#ifdef CONFIG_RTC
#if defined(CONFIG_RTC_DATETIME)
/* Initialize the system time using a broken out date/time structure */

int clock_basetime(FAR struct timespec *tp)
{
  struct tm rtctime;
  long nsecs = 0;
  int ret;

  /* Get the broken-out time from the date/time RTC. */

#ifdef CONFIG_ARCH_HAVE_RTC_SUBSECONDS
  ret = up_rtc_getdatetime_with_subseconds(&rtctime, &nsecs);
#else
  ret = up_rtc_getdatetime(&rtctime);
#endif
  if (ret >= 0)
    {
      /* And use the broken-out time to initialize the system time */

      tp->tv_sec  = timegm(&rtctime);
      tp->tv_nsec = nsecs;
    }

  return ret;
}

#elif defined(CONFIG_RTC_HIRES)

/* Initialize the system time using a high-resolution structure */

int clock_basetime(FAR struct timespec *tp)
{
  /* Get the complete time from the hi-res RTC. */

  return up_rtc_gettime(tp);
}

#else

/* Initialize the system time using seconds only */

int clock_basetime(FAR struct timespec *tp)
{
  /* Get the seconds (only) from the lo-resolution RTC */

  tp->tv_sec  = up_rtc_time();
  tp->tv_nsec = 0;
  return OK;
}

#endif /* CONFIG_RTC_HIRES */
#else /* CONFIG_RTC */

int clock_basetime(FAR struct timespec *tp)
{
  time_t jdn = 0;

  /* Get the EPOCH-relative julian date from the calendar year,
   * month, and date
   */

  jdn = clock_calendar2utc(CONFIG_START_YEAR, CONFIG_START_MONTH - 1,
                           CONFIG_START_DAY);

  /* Set the base time as seconds into this julian day. */

  tp->tv_sec  = jdn * SEC_PER_DAY;
  tp->tv_nsec = 0;
  return OK;
}

#endif /* CONFIG_RTC */

/****************************************************************************
 * Name: clock_inittime
 *
 * Description:
 *   Get the initial time value from the best source available.
 *
 ****************************************************************************/

#ifdef CONFIG_RTC
static void clock_inittime(FAR const struct timespec *tp)
{
  /* (Re-)initialize the time value to match the RTC */

#ifndef CONFIG_CLOCK_TIMEKEEPING
  struct timespec ts;
  irqstate_t flags;

  clock_systime_timespec(&ts);

  flags = spin_lock_irqsave(&g_basetime_lock);
  if (tp)
    {
      memcpy(&g_basetime, tp, sizeof(struct timespec));
    }
  else
    {
      clock_basetime(&g_basetime);
    }

  /* Adjust base time to hide initial timer ticks. */

  g_basetime.tv_sec  -= ts.tv_sec;
  g_basetime.tv_nsec -= ts.tv_nsec;
  while (g_basetime.tv_nsec < 0)
    {
      g_basetime.tv_nsec += NSEC_PER_SEC;
      g_basetime.tv_sec--;
    }

  spin_unlock_irqrestore(&g_basetime_lock, flags);
#else
  clock_inittimekeeping(tp);
#endif
}
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: clock_initialize
 *
 * Description:
 *   Perform one-time initialization of the timing facilities.
 *
 ****************************************************************************/

void clock_initialize(void)
{
  sched_trace_begin();

#if !defined(CONFIG_SUPPRESS_INTERRUPTS) && \
    !defined(CONFIG_SUPPRESS_TIMER_INTS) && \
    !defined(CONFIG_SYSTEMTICK_EXTCLK)
  /* Initialize the system timer interrupt */

  up_timer_initialize();
#endif

#if defined(CONFIG_RTC)
  /* Initialize the internal RTC hardware.  Initialization of external RTC
   * must be deferred until the system has booted.
   */

  up_rtc_initialize();

#if !defined(CONFIG_RTC_EXTERNAL)
  /* Initialize the time value to match the RTC */

  clock_inittime(NULL);
#endif

#endif

  perf_init();

#ifdef CONFIG_SCHED_CPULOAD_SYSCLK
  cpuload_init();
#endif

  sched_trace_end();
}

/****************************************************************************
 * Name: clock_synchronize
 *
 * Description:
 *   Synchronize the system timer to a hardware RTC.  This operation is
 *   normally performed automatically by the system during clock
 *   initialization.  However, the user may also need to explicitly re-
 *   synchronize the system timer to the RTC under certain conditions where
 *   the system timer is known to be in error.  For example, in certain low-
 *   power states, the system timer may be stopped but the RTC will continue
 *   keep correct time.  After recovering from such low-power state, this
 *   function should be called to restore the correct system time.
 *
 *   Calling this function could result in system time going "backward" in
 *   time, especially with certain lower resolution RTC implementations.
 *   Time going backward could have bad consequences if there are ongoing
 *   timers and delays.  So use this interface with care.
 *
 * Input Parameters:
 *   tp: rtc time should be synced, set NULL to re-get time
 *
 * Returned Value:
 *   None
 *
 * Assumptions:
 *
 ****************************************************************************/

#ifdef CONFIG_RTC
void clock_synchronize(FAR const struct timespec *tp)
{
  /* Re-initialize the time value to match the RTC */

  clock_inittime(tp);
}
#endif

/****************************************************************************
 * Name: clock_resynchronize
 *
 * Description:
 *   Resynchronize the system timer to a hardware RTC.  The user can
 *   explicitly re-synchronize the system timer to the RTC under certain
 *   conditions where the system timer is known to be in error.  For example,
 *   in certain low-power states, the system timer may be stopped but the
 *   RTC will continue keep correct time.  After recovering from such
 *   low-power state, this function should be called to restore the correct
 *   system time. Function also keeps monotonic clock at rate of RTC.
 *
 *   Calling this function will not result in system time going "backward" in
 *   time. If setting system time with RTC would result time going "backward"
 *   then resynchronization is not performed.
 *
 * Input Parameters:
 *   rtc_diff:  amount of time system-time is adjusted forward with RTC
 *
 * Returned Value:
 *   None
 *
 * Assumptions:
 *
 ****************************************************************************/

#if defined(CONFIG_RTC) && !defined(CONFIG_SCHED_TICKLESS) && \
    !defined(CONFIG_CLOCK_TIMEKEEPING) && !defined(CONFIG_ALARM_ARCH) && \
    !defined(CONFIG_TIMER_ARCH)
void clock_resynchronize(FAR struct timespec *rtc_diff)
{
  struct timespec rtc_time;
  struct timespec bias;
  struct timespec curr_ts;
  struct timespec rtc_diff_tmp;
  irqstate_t flags;
  int ret;

  if (rtc_diff == NULL)
    {
      rtc_diff = &rtc_diff_tmp;
    }

  /* Get RTC time */

  ret = clock_basetime(&rtc_time);
  if (ret < 0)
    {
      /* Error reading RTC, skip resynchronization. */

      sinfo("rtc error %d, skip resync\n", ret);

      rtc_diff->tv_sec = 0;
      rtc_diff->tv_nsec = 0;
      return;
    }

  /* Set the time value to match the RTC */

  /* Get the elapsed time since power up (in milliseconds).  This is a
   * bias value that we need to use to correct the base time.
   */

  clock_systime_timespec(&bias);

  /* Add the base time to this.  The base time is the time-of-day
   * setting.  When added to the elapsed time since the time-of-day
   * was last set, this gives us the current time.
   */

  flags = spin_lock_irqsave(&g_basetime_lock);
  clock_timespec_add(&bias, &g_basetime, &curr_ts);
  spin_unlock_irqrestore(&g_basetime_lock, flags);

  /* Check if RTC has advanced past system time. */

  if (curr_ts.tv_sec > rtc_time.tv_sec ||
      (curr_ts.tv_sec == rtc_time.tv_sec &&
       curr_ts.tv_nsec >= rtc_time.tv_nsec))
    {
      /* Setting system time with RTC now would result time going
       * backwards. Skip resynchronization.
       */

      sinfo("skip resync\n");

      rtc_diff->tv_sec = 0;
      rtc_diff->tv_nsec = 0;
    }
  else
    {
      /* Output difference between time at entry and new current time. */

      clock_timespec_subtract(&rtc_time, &curr_ts, rtc_diff);

      /* Add the sleep time to correct system timer */

      clock_t diff_ticks = SEC2TICK(rtc_diff->tv_sec) +
                           NSEC2TICK(rtc_diff->tv_nsec);

#ifdef CONFIG_SYSTEM_TIME64
      atomic64_fetch_add((FAR atomic64_t *)&g_system_ticks, diff_ticks);
#else
      atomic_fetch_add((FAR atomic_t *)&g_system_ticks, diff_ticks);
#endif
    }
}
#endif

/****************************************************************************
 * Name: clock_timer
 *
 * Description:
 *   This function must be called once every time the real time clock
 *   interrupt occurs.  The interval of this clock interrupt must be
 *   USEC_PER_TICK
 *
 ****************************************************************************/

#if !defined(CONFIG_SCHED_TICKLESS) && \
    !defined(CONFIG_ALARM_ARCH) && !defined(CONFIG_TIMER_ARCH)
void clock_timer(void)
{
  /* Increment the per-tick system counter */

#ifdef CONFIG_SYSTEM_TIME64
  atomic64_fetch_add((FAR atomic64_t *)&g_system_ticks, 1);
#else
  atomic_fetch_add((FAR atomic_t *)&g_system_ticks, 1);
#endif
}
#endif
