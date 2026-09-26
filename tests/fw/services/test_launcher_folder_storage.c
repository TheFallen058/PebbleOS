/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "clar.h"

#include "pbl/services/filesystem/pfs.h"
#include "pbl/services/process_management/launcher_folder_storage.h"
#include "pbl/util/size.h"

#include <string.h>

// Stubs
////////////////////////////////////
#include "stubs_analytics.h"
#include "stubs_hexdump.h"
#include "stubs_logging.h"
#include "stubs_mutex.h"
#include "stubs_passert.h"
#include "stubs_pbl_malloc.h"
#include "stubs_pebble_tasks.h"
#include "stubs_prompt.h"
#include "stubs_rand_ptr.h"
#include "stubs_serial.h"
#include "stubs_sleep.h"
#include "stubs_task_wdt.h"

// Fakes
////////////////////////////////////
#include "fake_spi_flash.h"

#define FOLDER_FILE "lnc_fld"

static const Uuid s_uuid_a = {0x1e, 0xb1, 0xd3, 0x9b, 0x56, 0x98, 0x48, 0x44,
                              0xb3, 0x94, 0x1f, 0x87, 0xb6, 0xbe, 0xae, 0x67};
static const Uuid s_uuid_b = {0xb8, 0x26, 0x2e, 0x08, 0x57, 0xe9, 0x4e, 0x58,
                              0x88, 0x02, 0x45, 0xfd, 0xfe, 0xe0, 0xac, 0x77};
static const Uuid s_uuid_c = {0xaf, 0xcc, 0x68, 0x76, 0x8f, 0x84, 0x44, 0xe0,
                              0xbb, 0x8b, 0x02, 0x3f, 0xfb, 0x2d, 0x7c, 0x2c};

// Builder
////////////////////////////////////

typedef struct ConfigBuilder {
  //! Larger than the biggest valid configuration so the over-limit cases fit too.
  uint8_t bytes[2048];
  size_t size;
} ConfigBuilder;

static ConfigBuilder s_builder;

static LauncherFolderConfig *prv_config(void) {
  return (LauncherFolderConfig *)s_builder.bytes;
}

static void prv_builder_reset(void) {
  memset(&s_builder, 0, sizeof(s_builder));
  prv_config()->version = LAUNCHER_FOLDER_STORAGE_VERSION;
  s_builder.size = sizeof(LauncherFolderConfig);
}

static LauncherFolderRecord *prv_builder_add(LauncherFolderId folder_id, const char *name,
                                             const Uuid *members, uint8_t member_count) {
  LauncherFolderRecord *record = (LauncherFolderRecord *)&s_builder.bytes[s_builder.size];
  record->folder_id = folder_id;
  record->member_count = member_count;
  strncpy(record->name, name, sizeof(record->name) - 1);
  if (member_count) {
    memcpy((uint8_t *)record + sizeof(*record), members, member_count * sizeof(Uuid));
  }
  s_builder.size += sizeof(*record) + (member_count * sizeof(Uuid));
  prv_config()->folder_count++;
  prv_config()->data_size = s_builder.size - sizeof(LauncherFolderConfig);
  return record;
}

static void prv_write_file(const void *data, size_t size) {
  const int fd = pfs_open(FOLDER_FILE, OP_FLAG_WRITE, FILE_TYPE_STATIC, size);
  cl_assert(fd >= 0);
  cl_assert_equal_i(pfs_write(fd, data, size), (int)size);
  cl_assert_equal_i(pfs_close(fd), S_SUCCESS);
  launcher_folder_storage_reset_for_tests();
}

// Setup
////////////////////////////////////

void test_launcher_folder_storage__initialize(void) {
  fake_spi_flash_init(0, 0x1000000);
  pfs_init(false);
  pfs_format(false);
  launcher_folder_storage_init();
  launcher_folder_storage_reset_for_tests();
  prv_builder_reset();
}

void test_launcher_folder_storage__cleanup(void) {}

// Layout
////////////////////////////////////

void test_launcher_folder_storage__no_file_means_no_config(void) {
  cl_assert(launcher_folder_storage_read() == NULL);
}

void test_launcher_folder_storage__round_trip(void) {
  const Uuid games[] = {s_uuid_a, s_uuid_b};
  prv_builder_add(3, "Games", games, ARRAY_LENGTH(games));
  prv_builder_add(7, "Navigation", &s_uuid_c, 1);

  cl_assert(launcher_folder_storage_write(s_builder.bytes, s_builder.size));

  LauncherFolderConfig *config = launcher_folder_storage_read();
  cl_assert(config != NULL);
  cl_assert_equal_i(config->version, LAUNCHER_FOLDER_STORAGE_VERSION);
  cl_assert_equal_i(config->folder_count, 2);

  const LauncherFolderRecord *games_record = launcher_folder_config_find(config, 3);
  cl_assert(games_record != NULL);
  cl_assert_equal_s(games_record->name, "Games");
  cl_assert_equal_i(games_record->member_count, 2);

  const LauncherFolderRecord *nav_record = launcher_folder_config_find(config, 7);
  cl_assert(nav_record != NULL);
  cl_assert_equal_s(nav_record->name, "Navigation");

  cl_assert(launcher_folder_config_find(config, 4) == NULL);
  cl_assert(launcher_folder_config_find(config, LAUNCHER_FOLDER_ID_ROOT) == NULL);

  app_free(config);
}

void test_launcher_folder_storage__membership_lookup(void) {
  const Uuid games[] = {s_uuid_a, s_uuid_b};
  prv_builder_add(3, "Games", games, ARRAY_LENGTH(games));
  cl_assert(launcher_folder_storage_write(s_builder.bytes, s_builder.size));

  LauncherFolderConfig *config = launcher_folder_storage_read();
  cl_assert(config != NULL);
  cl_assert_equal_i(launcher_folder_config_get_folder_for_app(config, &s_uuid_a), 3);
  cl_assert_equal_i(launcher_folder_config_get_folder_for_app(config, &s_uuid_b), 3);
  // An app that is in no folder stays in the launcher root.
  cl_assert_equal_i(launcher_folder_config_get_folder_for_app(config, &s_uuid_c),
                    LAUNCHER_FOLDER_ID_ROOT);
  cl_assert_equal_i(launcher_folder_config_get_folder_for_app(NULL, &s_uuid_a),
                    LAUNCHER_FOLDER_ID_ROOT);

  app_free(config);
}

void test_launcher_folder_storage__empty_config_removes_stored_one(void) {
  prv_builder_add(3, "Games", &s_uuid_a, 1);
  cl_assert(launcher_folder_storage_write(s_builder.bytes, s_builder.size));
  LauncherFolderConfig *config = launcher_folder_storage_read();
  cl_assert(config != NULL);
  app_free(config);

  prv_builder_reset();
  cl_assert(launcher_folder_storage_write(s_builder.bytes, s_builder.size));
  cl_assert(launcher_folder_storage_read() == NULL);
}

void test_launcher_folder_storage__folder_with_no_members_is_kept(void) {
  // The phone keeps empty folders so a temporarily uninstalled app does not lose its grouping;
  // the launcher is what hides them.
  prv_builder_add(3, "Games", NULL, 0);
  cl_assert(launcher_folder_storage_write(s_builder.bytes, s_builder.size));

  LauncherFolderConfig *config = launcher_folder_storage_read();
  cl_assert(config != NULL);
  cl_assert_equal_i(config->folder_count, 1);
  cl_assert_equal_i(launcher_folder_config_find(config, 3)->member_count, 0);
  app_free(config);
}

// Validation
////////////////////////////////////

void test_launcher_folder_storage__rejects_short_blob(void) {
  cl_assert(!launcher_folder_config_is_valid(NULL, 0));
  cl_assert(!launcher_folder_config_is_valid(s_builder.bytes, 1));
  cl_assert(!launcher_folder_config_is_valid(s_builder.bytes, sizeof(LauncherFolderConfig) - 1));
}

void test_launcher_folder_storage__rejects_unsupported_version(void) {
  prv_builder_add(3, "Games", &s_uuid_a, 1);
  prv_config()->version = LAUNCHER_FOLDER_STORAGE_VERSION + 1;
  cl_assert(!launcher_folder_config_is_valid(s_builder.bytes, s_builder.size));
  cl_assert(!launcher_folder_storage_write(s_builder.bytes, s_builder.size));
}

void test_launcher_folder_storage__rejects_truncated_payload(void) {
  prv_builder_add(3, "Games", &s_uuid_a, 1);
  cl_assert(!launcher_folder_config_is_valid(s_builder.bytes, s_builder.size - 1));
  cl_assert(!launcher_folder_config_is_valid(s_builder.bytes, sizeof(LauncherFolderConfig)));
}

void test_launcher_folder_storage__rejects_data_size_mismatch(void) {
  prv_builder_add(3, "Games", &s_uuid_a, 1);
  prv_config()->data_size += 1;
  cl_assert(!launcher_folder_config_is_valid(s_builder.bytes, s_builder.size));
}

void test_launcher_folder_storage__rejects_trailing_bytes(void) {
  prv_builder_add(3, "Games", &s_uuid_a, 1);
  prv_config()->data_size += 4;
  cl_assert(!launcher_folder_config_is_valid(s_builder.bytes, s_builder.size + 4));
}

void test_launcher_folder_storage__rejects_member_count_past_end(void) {
  LauncherFolderRecord *record = prv_builder_add(3, "Games", &s_uuid_a, 1);
  record->member_count = 5;
  cl_assert(!launcher_folder_config_is_valid(s_builder.bytes, s_builder.size));
}

void test_launcher_folder_storage__rejects_root_folder_id(void) {
  prv_builder_add(LAUNCHER_FOLDER_ID_ROOT, "Games", &s_uuid_a, 1);
  cl_assert(!launcher_folder_config_is_valid(s_builder.bytes, s_builder.size));
}

void test_launcher_folder_storage__rejects_duplicate_folder_id(void) {
  prv_builder_add(3, "Games", &s_uuid_a, 1);
  prv_builder_add(3, "Navigation", &s_uuid_b, 1);
  cl_assert(!launcher_folder_config_is_valid(s_builder.bytes, s_builder.size));
}

void test_launcher_folder_storage__rejects_empty_name(void) {
  prv_builder_add(3, "", &s_uuid_a, 1);
  cl_assert(!launcher_folder_config_is_valid(s_builder.bytes, s_builder.size));
}

void test_launcher_folder_storage__rejects_unterminated_name(void) {
  LauncherFolderRecord *record = prv_builder_add(3, "Games", &s_uuid_a, 1);
  memset(record->name, 'x', sizeof(record->name));
  cl_assert(!launcher_folder_config_is_valid(s_builder.bytes, s_builder.size));
}

void test_launcher_folder_storage__accepts_longest_name(void) {
  char name[LAUNCHER_FOLDER_NAME_BUFFER_SIZE];
  memset(name, 'x', sizeof(name));
  name[LAUNCHER_FOLDER_NAME_MAX_LENGTH] = '\0';
  prv_builder_add(3, name, &s_uuid_a, 1);
  cl_assert(launcher_folder_config_is_valid(s_builder.bytes, s_builder.size));
}

void test_launcher_folder_storage__rejects_invalid_utf8_name(void) {
  LauncherFolderRecord *record = prv_builder_add(3, "Games", &s_uuid_a, 1);
  record->name[0] = (char)0xc3;
  record->name[1] = (char)0x28;
  cl_assert(!launcher_folder_config_is_valid(s_builder.bytes, s_builder.size));
}

void test_launcher_folder_storage__accepts_multibyte_name(void) {
  prv_builder_add(3, "Jeux \xf0\x9f\x8e\xae", &s_uuid_a, 1);
  cl_assert(launcher_folder_config_is_valid(s_builder.bytes, s_builder.size));
}

void test_launcher_folder_storage__rejects_app_twice_in_one_folder(void) {
  const Uuid members[] = {s_uuid_a, s_uuid_a};
  prv_builder_add(3, "Games", members, ARRAY_LENGTH(members));
  cl_assert(!launcher_folder_config_is_valid(s_builder.bytes, s_builder.size));
}

void test_launcher_folder_storage__rejects_app_in_two_folders(void) {
  prv_builder_add(3, "Games", &s_uuid_a, 1);
  prv_builder_add(4, "Navigation", &s_uuid_a, 1);
  cl_assert(!launcher_folder_config_is_valid(s_builder.bytes, s_builder.size));
}

void test_launcher_folder_storage__rejects_invalid_member_uuid(void) {
  const Uuid system_uuid = UUID_SYSTEM;
  prv_builder_add(3, "Games", &system_uuid, 1);
  cl_assert(!launcher_folder_config_is_valid(s_builder.bytes, s_builder.size));
}

void test_launcher_folder_storage__rejects_too_many_folders(void) {
  char name[8];
  for (int i = 0; i < LAUNCHER_FOLDER_MAX_COUNT + 1; i++) {
    snprintf(name, sizeof(name), "F%d", i);
    prv_builder_add(i + 1, name, NULL, 0);
  }
  cl_assert(!launcher_folder_config_is_valid(s_builder.bytes, s_builder.size));
}

void test_launcher_folder_storage__rejects_too_many_members(void) {
  Uuid members[LAUNCHER_FOLDER_MAX_MEMBERS_TOTAL + 1];
  for (unsigned int i = 0; i < ARRAY_LENGTH(members); i++) {
    members[i] = s_uuid_a;
    members[i].byte0 = (uint8_t)i;
    members[i].byte1 = (uint8_t)(i >> 8);
  }
  prv_builder_add(3, "Games", members, ARRAY_LENGTH(members));
  cl_assert(!launcher_folder_config_is_valid(s_builder.bytes, s_builder.size));
}

// Corrupt storage
////////////////////////////////////

void test_launcher_folder_storage__corrupt_file_is_discarded(void) {
  prv_builder_add(3, "Games", &s_uuid_a, 1);
  prv_config()->version = LAUNCHER_FOLDER_STORAGE_VERSION + 1;
  prv_write_file(s_builder.bytes, s_builder.size);

  cl_assert(launcher_folder_storage_read() == NULL);
  // The bad file is dropped so the launcher does not re-read it on every open.
  launcher_folder_storage_reset_for_tests();
  cl_assert(launcher_folder_storage_read() == NULL);
}

void test_launcher_folder_storage__truncated_file_is_discarded(void) {
  prv_builder_add(3, "Games", &s_uuid_a, 1);
  prv_write_file(s_builder.bytes, s_builder.size - sizeof(Uuid));

  cl_assert(launcher_folder_storage_read() == NULL);
}

void test_launcher_folder_storage__garbage_file_is_discarded(void) {
  uint8_t garbage[64];
  memset(garbage, 0xa5, sizeof(garbage));
  prv_write_file(garbage, sizeof(garbage));

  cl_assert(launcher_folder_storage_read() == NULL);
}
