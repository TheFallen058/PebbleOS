/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "pbl/services/process_management/launcher_folder_storage.h"

#include "applib/graphics/utf8.h"
#include "kernel/pbl_malloc.h"
#include "pbl/kernel/mutex.h"
#include "pbl/services/filesystem/pfs.h"
#include "system/passert.h"
#include <pbl/logging/logging.h>

#include <string.h>

PBL_LOG_MODULE_DEFINE(service_launcher_folders, CONFIG_SERVICE_PROCESS_MANAGEMENT_LOG_LEVEL);

#define FOLDER_FILE "lnc_fld"

#define LAUNCHER_FOLDER_CONFIG_MAX_SIZE                                                  \
  (sizeof(LauncherFolderConfig) + (LAUNCHER_FOLDER_MAX_COUNT * sizeof(LauncherFolderRecord)) + \
   (LAUNCHER_FOLDER_MAX_MEMBERS_TOTAL * sizeof(Uuid)))

typedef struct {
  struct pbl_mutex mutex;
  bool file_known_missing;
} LauncherFolderData;

static LauncherFolderData s_data;

void launcher_folder_storage_init(void) {
  pbl_mutex_init(&s_data.mutex);
}

#if UNITTEST
void launcher_folder_storage_reset_for_tests(void) {
  s_data.file_known_missing = false;
}
#endif

/////////////////////
// Record walking

static size_t prv_record_size(const LauncherFolderRecord *record) {
  return sizeof(*record) + (record->member_count * sizeof(Uuid));
}

static const Uuid *prv_record_member(const LauncherFolderRecord *record, uint8_t index) {
  // members[] is unaligned inside the packed record, but Uuid is a byte array so this is fine.
  return (const Uuid *)((const uint8_t *)record + sizeof(*record) + (index * sizeof(Uuid)));
}

//! Collects every record, rejecting anything that does not fit exactly inside the blob.
//! @return the number of records, or -1 if the layout is malformed.
static int prv_collect_records(const LauncherFolderConfig *config,
                               const LauncherFolderRecord *records[LAUNCHER_FOLDER_MAX_COUNT]) {
  const uint8_t *cursor = config->data;
  const uint8_t *const end = cursor + config->data_size;
  uint16_t total_members = 0;

  for (uint8_t i = 0; i < config->folder_count; i++) {
    if ((size_t)(end - cursor) < sizeof(LauncherFolderRecord)) {
      return -1;
    }

    const LauncherFolderRecord *record = (const LauncherFolderRecord *)cursor;
    const size_t record_size = prv_record_size(record);
    if (record_size > (size_t)(end - cursor)) {
      return -1;
    }

    total_members += record->member_count;
    if (total_members > LAUNCHER_FOLDER_MAX_MEMBERS_TOTAL) {
      return -1;
    }

    records[i] = record;
    cursor += record_size;
  }

  // Trailing bytes mean the blob does not describe what it claims to.
  return (cursor == end) ? config->folder_count : -1;
}

/////////////////////
// Validation

static bool prv_is_name_valid(const LauncherFolderRecord *record) {
  return (record->name[0] != '\0') && memchr(record->name, '\0', sizeof(record->name)) &&
         utf8_is_valid_string(record->name);
}

//! An app may appear in at most one folder, and at most once in that folder. Rather than
//! silently dropping the extra reference we reject the whole configuration, so that a buggy
//! phone can never leave the watch showing an app in two places.
static bool prv_are_members_unique(const LauncherFolderRecord *const *records,
                                   uint8_t folder_count) {
  for (uint8_t folder = 0; folder < folder_count; folder++) {
    const LauncherFolderRecord *record = records[folder];
    for (uint8_t member = 0; member < record->member_count; member++) {
      const Uuid *uuid = prv_record_member(record, member);
      if (uuid_is_system(uuid) || uuid_is_invalid(uuid)) {
        return false;
      }

      for (uint8_t other_folder = 0; other_folder <= folder; other_folder++) {
        const LauncherFolderRecord *other = records[other_folder];
        const uint8_t limit = (other_folder == folder) ? member : other->member_count;
        for (uint8_t other_member = 0; other_member < limit; other_member++) {
          if (uuid_equal(uuid, prv_record_member(other, other_member))) {
            return false;
          }
        }
      }
    }
  }
  return true;
}

bool launcher_folder_config_is_valid(const void *data, size_t size) {
  const LauncherFolderConfig *config = data;
  if (!config || (size < sizeof(*config)) || (size > LAUNCHER_FOLDER_CONFIG_MAX_SIZE) ||
      (config->version != LAUNCHER_FOLDER_STORAGE_VERSION) ||
      (config->folder_count > LAUNCHER_FOLDER_MAX_COUNT) ||
      (config->data_size != (size - sizeof(*config)))) {
    return false;
  }

  const LauncherFolderRecord *records[LAUNCHER_FOLDER_MAX_COUNT];
  if (prv_collect_records(config, records) < 0) {
    return false;
  }

  for (uint8_t i = 0; i < config->folder_count; i++) {
    if ((records[i]->folder_id == LAUNCHER_FOLDER_ID_ROOT) || !prv_is_name_valid(records[i])) {
      return false;
    }
    for (uint8_t j = 0; j < i; j++) {
      if (records[i]->folder_id == records[j]->folder_id) {
        return false;
      }
    }
  }

  return prv_are_members_unique(records, config->folder_count);
}

/////////////////////
// Persistence

LauncherFolderConfig *launcher_folder_storage_read(void) {
  PBL_ASSERT_TASK(PebbleTask_App);

  LauncherFolderConfig *config = NULL;
  bool delete_file = false;
  pbl_mutex_lock(&s_data.mutex, PBL_FOREVER);

  if (s_data.file_known_missing) {
    goto unlock;
  }

  const int fd = pfs_open(FOLDER_FILE, OP_FLAG_READ, 0, 0);
  if (fd < 0) {
    PBL_LOG_DBG("Launcher folder file does not exist");
    s_data.file_known_missing = true;
    goto unlock;
  }

  const size_t size = pfs_get_file_size(fd);
  if ((size < sizeof(LauncherFolderConfig)) || (size > LAUNCHER_FOLDER_CONFIG_MAX_SIZE)) {
    delete_file = true;
    goto close;
  }

  config = app_malloc(size);
  if (!config) {
    PBL_LOG_ERR("Failed to malloc launcher folder config");
    goto close;
  }

  if ((pfs_read(fd, config, size) != (int)size) || !launcher_folder_config_is_valid(config, size)) {
    app_free(config);
    config = NULL;
    delete_file = true;
  }

close:
  pfs_close(fd);
  if (delete_file) {
    // A launcher that cannot be shown is worse than one without folders, so drop the bad file
    // and fall back to the flat list until the phone sends a good configuration.
    PBL_LOG_ERR("Discarding corrupt launcher folder config");
    pfs_remove(FOLDER_FILE);
    s_data.file_known_missing = true;
  }

unlock:
  pbl_mutex_unlock(&s_data.mutex);
  return config;
}

static bool prv_write_locked(const void *data, size_t size) {
  int fd = pfs_open(FOLDER_FILE, OP_FLAG_OVERWRITE, FILE_TYPE_STATIC, size);
  if (fd == E_DOES_NOT_EXIST) {
    fd = pfs_open(FOLDER_FILE, OP_FLAG_WRITE, FILE_TYPE_STATIC, size);
  }
  if (fd < 0) {
    return false;
  }

  if (pfs_write(fd, data, size) != (int)size) {
    pfs_close_and_remove(fd);
    return false;
  }

  return (pfs_close(fd) == S_SUCCESS);
}

bool launcher_folder_storage_write(const void *data, size_t size) {
  PBL_ASSERT_TASK(PebbleTask_KernelBackground);

  if (!launcher_folder_config_is_valid(data, size)) {
    return false;
  }

  const LauncherFolderConfig *config = data;
  pbl_mutex_lock(&s_data.mutex, PBL_FOREVER);

  bool success;
  if (config->folder_count == 0) {
    pfs_remove(FOLDER_FILE);
    s_data.file_known_missing = true;
    success = true;
  } else {
    success = prv_write_locked(data, size);
    if (success) {
      s_data.file_known_missing = false;
    } else {
      PBL_LOG_ERR("Could not write launcher folder config");
    }
  }

  pbl_mutex_unlock(&s_data.mutex);
  return success;
}

/////////////////////
// Lookups

const LauncherFolderRecord *launcher_folder_config_find(const LauncherFolderConfig *config,
                                                        LauncherFolderId folder_id) {
  if (!config || (folder_id == LAUNCHER_FOLDER_ID_ROOT)) {
    return NULL;
  }

  const uint8_t *cursor = config->data;
  for (uint8_t i = 0; i < config->folder_count; i++) {
    const LauncherFolderRecord *record = (const LauncherFolderRecord *)cursor;
    if (record->folder_id == folder_id) {
      return record;
    }
    cursor += prv_record_size(record);
  }
  return NULL;
}

LauncherFolderId launcher_folder_config_get_folder_for_app(const LauncherFolderConfig *config,
                                                           const Uuid *uuid) {
  if (!config || !uuid) {
    return LAUNCHER_FOLDER_ID_ROOT;
  }

  const uint8_t *cursor = config->data;
  for (uint8_t i = 0; i < config->folder_count; i++) {
    const LauncherFolderRecord *record = (const LauncherFolderRecord *)cursor;
    for (uint8_t member = 0; member < record->member_count; member++) {
      if (uuid_equal(uuid, prv_record_member(record, member))) {
        return record->folder_id;
      }
    }
    cursor += prv_record_size(record);
  }
  return LAUNCHER_FOLDER_ID_ROOT;
}
