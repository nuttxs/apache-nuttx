/****************************************************************************
 * boards/arm/sama5/sama5d4-ek/src/sam_at25.c
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

#include <stdbool.h>
#include <stdio.h>
#include <errno.h>
#include <debug.h>

#include <nuttx/spi/spi.h>
#include <nuttx/mtd/mtd.h>
#include <nuttx/fs/fs.h>
#include <nuttx/fs/nxffs.h>
#include <nuttx/drivers/drivers.h>

#include "sam_spi.h"
#include "sama5d4-ek.h"

#ifdef HAVE_AT25

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: sam_at25_automount
 *
 * Description:
 *   Initialize and configure the AT25 serial FLASH
 *
 ****************************************************************************/

int sam_at25_automount(int minor)
{
  struct spi_dev_s *spi;
  struct mtd_dev_s *mtd;
#ifdef CONFIG_SAMA5D4EK_AT25_MTD
  char mtddev[12];
#endif
  static bool initialized = false;
  int ret;

  /* Have we already initialized? */

  if (!initialized)
    {
      /* No.. Get the SPI port driver */

      spi = sam_spibus_initialize(AT25_PORT);
      if (!spi)
        {
          ferr("ERROR: Failed to initialize SPI port %d\n", AT25_PORT);
          return -ENODEV;
        }

      /* Now bind the SPI interface to the AT25 SPI FLASH driver */

      mtd = at25_initialize(spi);
      if (!mtd)
        {
          ferr("ERROR: Failed to bind SPI port %d "
               "to the AT25 FLASH driver\n", AT25_PORT);
          return -ENODEV;
        }

#if defined(CONFIG_SAMA5D4EK_AT25_MTD)
      /* Use the minor number to create device paths */

      snprintf(mtddev, sizeof(mtddev), "/dev/mtd%d", minor);

      /* Register the MTD driver */

      ret = register_mtddriver(mtddev, mtd, 0755, NULL);
      if (ret < 0)
        {
          ferr("register_mtddriver %s failed: %d\n", mtddev, ret);
          return ret;
        }

#elif defined(CONFIG_SAMA5D4EK_AT25_NXFFS)
      /* Initialize to provide NXFFS on the MTD interface */

      ret = nxffs_initialize(mtd);
      if (ret < 0)
        {
          ferr("ERROR: NXFFS initialization failed: %d\n", ret);
          return ret;
        }

      /* Mount the file system at /mnt/at25 */

      ret = nx_mount(NULL, "/mnt/at25", "nxffs", 0, NULL);
      if (ret < 0)
        {
          ferr("ERROR: Failed to mount the NXFFS volume: %d\n", ret);
          return ret;
        }

#endif

      /* Now we are initialized */

      initialized = true;
    }

  return OK;
}

#endif /* HAVE_AT25 */
