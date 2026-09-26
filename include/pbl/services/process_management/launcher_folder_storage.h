/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include "pbl/kernel/compiler.h"
#include "pbl/util/uuid.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

//! @file
//! Launcher folder configuration.
//!
//! Folders are launcher presentation metadata, not applications: they have no AppInstallId, no
//! AppDB record and no UUID of their own, so nothing outside the launcher has to know they exist.
//!
//! Membership is keyed by application UUID rather than AppInstallId because an AppInstallId only
//! identifies the current installation; an app that is uninstalled and reinstalled gets a new one
//! and would silently fall out of its folder.
//!
//! The configuration is stored separately from the legacy flat app order (\ref app_order_storage)
//! so that the meaning of the existing order file and its protocol message never changes.

typedef uint8_t LauncherFolderId;

#define LAUNCHER_FOLDER_STORAGE_VERSION 1

//! Reserved: apps that are in no folder are shown in the launcher root.
#define LAUNCHER_FOLDER_ID_ROOT ((LauncherFolderId)0)

#define LAUNCHER_FOLDER_MAX_COUNT         16
#define LAUNCHER_FOLDER_MAX_MEMBERS_TOTAL 64

//! Names are a fixed-width field so a record can be walked without a separate length table. 24
//! bytes holds more than the launcher can display on the widest supported screen; the mobile app
//! enforces the same limit so names are never silently truncated here.
#define LAUNCHER_FOLDER_NAME_MAX_LENGTH  24
#define LAUNCHER_FOLDER_NAME_BUFFER_SIZE (LAUNCHER_FOLDER_NAME_MAX_LENGTH + 1)

typedef struct PBL_PACKED LauncherFolderRecord {
  LauncherFolderId folder_id;
  uint8_t member_count;
  char name[LAUNCHER_FOLDER_NAME_BUFFER_SIZE];
  Uuid members[];
} LauncherFolderRecord;

//! Versioned container holding \ref LauncherFolderRecord entries back to back. The same byte
//! layout is used on the wire and on flash. Multi-byte fields are little-endian, matching the
//! other structures this subsystem persists.
typedef struct PBL_PACKED LauncherFolderConfig {
  uint8_t version;
  uint8_t folder_count;
  uint16_t data_size;
  uint8_t data[];
} LauncherFolderConfig;

void launcher_folder_storage_init(void);

#if UNITTEST
//! Reset cached state for testing - clears the cached "file missing" flag.
void launcher_folder_storage_reset_for_tests(void);
#endif

//! Reads the stored folder configuration.
//! @return a configuration on the app heap, or NULL when none is stored or the stored one is
//! unusable, in which case the launcher shows its traditional flat list. Freed by the caller.
//! @note Must be called from the app task.
LauncherFolderConfig *launcher_folder_storage_read(void);

//! Validates and persists a configuration received from the phone. A configuration with no
//! folders removes the stored one.
//! @note Must be called from the kernel background task.
bool launcher_folder_storage_write(const void *data, size_t size);

//! Checks that a configuration blob is structurally sound and internally consistent.
bool launcher_folder_config_is_valid(const void *data, size_t size);

//! @return the record for \a folder_id, or NULL if there is no such folder.
const LauncherFolderRecord *launcher_folder_config_find(const LauncherFolderConfig *config,
                                                        LauncherFolderId folder_id);

//! @return the folder \a uuid belongs to, or LAUNCHER_FOLDER_ID_ROOT if it belongs to none.
LauncherFolderId launcher_folder_config_get_folder_for_app(const LauncherFolderConfig *config,
                                                           const Uuid *uuid);
