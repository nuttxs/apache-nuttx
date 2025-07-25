/****************************************************************************
 * boards/arm/sama5/sama5d4-ek/src/sam_usbmsc.c
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

#include <stdio.h>
#include <syslog.h>
#include <errno.h>

#include <nuttx/board.h>

#include "sama5d4-ek.h"

#ifdef CONFIG_USBMSC

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Configuration ************************************************************/

#ifndef HAVE_AT25
#  error AT25 Serial FLASH not supported
#endif

#ifndef CONFIG_SAMA5D4EK_AT25_MTD
#  error AT25 MTD support required (CONFIG_SAMA5D4EK_AT25_MTD)
#  undef HAVE_AT25
#endif

#ifndef CONFIG_SYSTEM_USBMSC_DEVMINOR1
#  define CONFIG_SYSTEM_USBMSC_DEVMINOR1 0
#endif

#if CONFIG_SYSTEM_USBMSC_DEVMINOR1 != AT25_MINOR
#  error Confusion in the assignment of minor device numbers
#  undef HAVE_AT25
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: board_usbmsc_initialize
 *
 * Description:
 *   Perform architecture specific initialization as needed to establish
 *   the mass storage device that will be exported by the USB MSC device.
 *
 ****************************************************************************/

int board_usbmsc_initialize(int port)
{
  /* Initialize the AT25 MTD driver */

#ifdef HAVE_AT25
  int ret = sam_at25_automount(AT25_MINOR);
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: sam_at25_automount failed: %d\n", ret);
    }

  return ret;
#else
  return -ENODEV;
#endif
}

#endif /* CONFIG_USBMSC */
