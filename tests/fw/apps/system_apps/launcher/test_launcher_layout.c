/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "clar.h"

#include "apps/system/launcher/default/launcher_layout.h"
#include "pbl/services/filesystem/pfs.h"
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

#define MAX_TEST_APPS 8

// Fake app list, standing in for AppMenuDataSource. Its order is the flat app order the phone
// already synchronises, which is what the layout derives folder positions from.
static AppMenuNode s_apps[MAX_TEST_APPS];
static uint16_t s_app_count;

uint16_t app_menu_data_source_get_count(AppMenuDataSource *source) { return s_app_count; }

AppMenuNode *app_menu_data_source_get_node_at_index(AppMenuDataSource *source,
                                                    uint16_t row_index) {
  return (row_index < s_app_count) ? &s_apps[row_index] : NULL;
}

static AppMenuDataSource s_data_source;
static LauncherLayout s_layout;

static Uuid prv_uuid(uint8_t seed) {
  Uuid uuid = {0};
  uuid.byte0 = seed;
  uuid.byte15 = 0xee;
  return uuid;
}

static void prv_add_app(const char *name, uint8_t seed) {
  cl_assert(s_app_count < MAX_TEST_APPS);
  s_apps[s_app_count] = (AppMenuNode){
    .install_id = s_app_count + 1,
    .name = (char *)name,
    .uuid = prv_uuid(seed),
  };
  s_app_count++;
}

// Config builder
////////////////////////////////////

static uint8_t s_config_bytes[512];
static size_t s_config_size;

static void prv_config_reset(void) {
  memset(s_config_bytes, 0, sizeof(s_config_bytes));
  ((LauncherFolderConfig *)s_config_bytes)->version = LAUNCHER_FOLDER_STORAGE_VERSION;
  s_config_size = sizeof(LauncherFolderConfig);
}

static void prv_config_add_folder(LauncherFolderId folder_id, const char *name,
                                  const uint8_t *member_seeds, uint8_t member_count) {
  LauncherFolderRecord *record = (LauncherFolderRecord *)&s_config_bytes[s_config_size];
  record->folder_id = folder_id;
  record->member_count = member_count;
  strncpy(record->name, name, sizeof(record->name) - 1);
  Uuid *members = (Uuid *)((uint8_t *)record + sizeof(*record));
  for (uint8_t i = 0; i < member_count; i++) {
    members[i] = prv_uuid(member_seeds[i]);
  }
  s_config_size += sizeof(*record) + (member_count * sizeof(Uuid));

  LauncherFolderConfig *config = (LauncherFolderConfig *)s_config_bytes;
  config->folder_count++;
  config->data_size = s_config_size - sizeof(LauncherFolderConfig);
}

static void prv_store_config(void) {
  cl_assert(launcher_folder_storage_write(s_config_bytes, s_config_size));
}

// Setup
////////////////////////////////////

void test_launcher_layout__initialize(void) {
  fake_spi_flash_init(0, 0x1000000);
  pfs_init(false);
  pfs_format(false);
  launcher_folder_storage_init();
  launcher_folder_storage_reset_for_tests();

  memset(s_apps, 0, sizeof(s_apps));
  s_app_count = 0;
  prv_config_reset();

  // Music, Compass, Backtrack, Pebbletris, Calculator
  prv_add_app("Music", 1);
  prv_add_app("Compass", 2);
  prv_add_app("Backtrack", 3);
  prv_add_app("Pebbletris", 4);
  prv_add_app("Calculator", 5);
}

void test_launcher_layout__cleanup(void) { launcher_layout_deinit(&s_layout); }

static void prv_init_layout(void) { launcher_layout_init(&s_layout, &s_data_source); }

static const char *prv_root_app_name(uint16_t row) {
  LauncherLayoutEntry entry;
  cl_assert(launcher_layout_get_row(&s_layout, LAUNCHER_FOLDER_ID_ROOT, row, &entry));
  cl_assert_equal_i(entry.type, LauncherLayoutEntryTypeApp);
  return entry.app->name;
}

static const char *prv_root_folder_name(uint16_t row) {
  LauncherLayoutEntry entry;
  cl_assert(launcher_layout_get_row(&s_layout, LAUNCHER_FOLDER_ID_ROOT, row, &entry));
  cl_assert_equal_i(entry.type, LauncherLayoutEntryTypeFolder);
  return entry.folder->name;
}

static const char *prv_folder_app_name(LauncherFolderId folder_id, uint16_t row) {
  LauncherLayoutEntry entry;
  cl_assert(launcher_layout_get_row(&s_layout, folder_id, row, &entry));
  cl_assert_equal_i(entry.type, LauncherLayoutEntryTypeApp);
  return entry.app->name;
}

// No folders
////////////////////////////////////

void test_launcher_layout__no_config_is_the_flat_list(void) {
  prv_init_layout();

  cl_assert(!launcher_layout_has_folders(&s_layout));
  cl_assert_equal_i(launcher_layout_get_row_count(&s_layout, LAUNCHER_FOLDER_ID_ROOT), 5);
  cl_assert_equal_s(prv_root_app_name(0), "Music");
  cl_assert_equal_s(prv_root_app_name(4), "Calculator");

  // Nothing is allocated for the grouping when no folders are configured.
  cl_assert(s_layout.config == NULL);
  cl_assert(s_layout.root_rows == NULL);

  LauncherLayoutEntry entry;
  cl_assert(!launcher_layout_get_row(&s_layout, LAUNCHER_FOLDER_ID_ROOT, 5, &entry));
  cl_assert(!launcher_layout_get_row(&s_layout, 1, 0, &entry));
}

void test_launcher_layout__empty_config_is_the_flat_list(void) {
  prv_store_config();
  prv_init_layout();

  cl_assert(!launcher_layout_has_folders(&s_layout));
  cl_assert_equal_i(launcher_layout_get_row_count(&s_layout, LAUNCHER_FOLDER_ID_ROOT), 5);
}

// One folder
////////////////////////////////////

void test_launcher_layout__folder_takes_the_slot_of_its_first_member(void) {
  const uint8_t navigation[] = {2, 3};
  prv_config_add_folder(1, "Navigation", navigation, ARRAY_LENGTH(navigation));
  prv_store_config();
  prv_init_layout();

  cl_assert(launcher_layout_has_folders(&s_layout));
  // Music, Navigation >, Pebbletris, Calculator
  cl_assert_equal_i(launcher_layout_get_row_count(&s_layout, LAUNCHER_FOLDER_ID_ROOT), 4);
  cl_assert_equal_s(prv_root_app_name(0), "Music");
  cl_assert_equal_s(prv_root_folder_name(1), "Navigation");
  cl_assert_equal_s(prv_root_app_name(2), "Pebbletris");
  cl_assert_equal_s(prv_root_app_name(3), "Calculator");

  cl_assert_equal_i(launcher_layout_get_row_count(&s_layout, 1), 2);
  cl_assert_equal_s(prv_folder_app_name(1, 0), "Compass");
  cl_assert_equal_s(prv_folder_app_name(1, 1), "Backtrack");
  cl_assert_equal_i(launcher_layout_get_root_row_for_folder(&s_layout, 1), 1);
}

void test_launcher_layout__multiple_folders_keep_their_relative_order(void) {
  const uint8_t navigation[] = {2, 3};
  const uint8_t games[] = {4};
  prv_config_add_folder(2, "Games", games, ARRAY_LENGTH(games));
  prv_config_add_folder(1, "Navigation", navigation, ARRAY_LENGTH(navigation));
  prv_store_config();
  prv_init_layout();

  // Folder position follows the app order, not the order folders appear in the configuration.
  cl_assert_equal_i(launcher_layout_get_row_count(&s_layout, LAUNCHER_FOLDER_ID_ROOT), 4);
  cl_assert_equal_s(prv_root_app_name(0), "Music");
  cl_assert_equal_s(prv_root_folder_name(1), "Navigation");
  cl_assert_equal_s(prv_root_folder_name(2), "Games");
  cl_assert_equal_s(prv_root_app_name(3), "Calculator");

  cl_assert_equal_i(launcher_layout_get_root_row_for_folder(&s_layout, 1), 1);
  cl_assert_equal_i(launcher_layout_get_root_row_for_folder(&s_layout, 2), 2);
}

void test_launcher_layout__every_app_in_one_folder(void) {
  const uint8_t all[] = {1, 2, 3, 4, 5};
  prv_config_add_folder(1, "Everything", all, ARRAY_LENGTH(all));
  prv_store_config();
  prv_init_layout();

  cl_assert_equal_i(launcher_layout_get_row_count(&s_layout, LAUNCHER_FOLDER_ID_ROOT), 1);
  cl_assert_equal_s(prv_root_folder_name(0), "Everything");
  cl_assert_equal_i(launcher_layout_get_row_count(&s_layout, 1), 5);
}

// Empty folders and app lifecycle
////////////////////////////////////

void test_launcher_layout__folder_with_no_installed_member_is_hidden(void) {
  const uint8_t missing[] = {99};
  prv_config_add_folder(1, "Ghosts", missing, ARRAY_LENGTH(missing));
  prv_store_config();
  prv_init_layout();

  // The configuration keeps the folder so the phone can restore it, but nothing is shown.
  cl_assert(!launcher_layout_has_folders(&s_layout));
  cl_assert_equal_i(launcher_layout_get_row_count(&s_layout, LAUNCHER_FOLDER_ID_ROOT), 5);
  cl_assert_equal_i(launcher_layout_get_row_count(&s_layout, 1), 0);
  cl_assert(launcher_folder_config_find(s_layout.config, 1) != NULL);
}

void test_launcher_layout__uninstalling_a_member_keeps_the_folder(void) {
  const uint8_t games[] = {4, 5};
  prv_config_add_folder(1, "Games", games, ARRAY_LENGTH(games));
  prv_store_config();
  prv_init_layout();
  cl_assert_equal_i(launcher_layout_get_row_count(&s_layout, 1), 2);

  // Pebbletris is uninstalled.
  s_apps[3] = s_apps[4];
  s_app_count = 4;
  launcher_layout_reload(&s_layout);

  cl_assert_equal_i(launcher_layout_get_row_count(&s_layout, 1), 1);
  cl_assert_equal_s(prv_folder_app_name(1, 0), "Calculator");
  cl_assert_equal_i(launcher_layout_get_row_count(&s_layout, LAUNCHER_FOLDER_ID_ROOT), 4);
}

void test_launcher_layout__uninstalling_the_last_member_hides_the_folder(void) {
  const uint8_t games[] = {4};
  prv_config_add_folder(1, "Games", games, ARRAY_LENGTH(games));
  prv_store_config();
  prv_init_layout();
  cl_assert_equal_i(launcher_layout_get_row_count(&s_layout, 1), 1);

  s_apps[3] = s_apps[4];
  s_app_count = 4;
  launcher_layout_reload(&s_layout);

  cl_assert_equal_i(launcher_layout_get_row_count(&s_layout, 1), 0);
  cl_assert_equal_i(launcher_layout_get_row_count(&s_layout, LAUNCHER_FOLDER_ID_ROOT), 4);
  cl_assert_equal_i(launcher_layout_get_root_row_for_folder(&s_layout, 1), 0);
}

void test_launcher_layout__reinstalling_the_same_uuid_restores_membership(void) {
  const uint8_t games[] = {4};
  prv_config_add_folder(1, "Games", games, ARRAY_LENGTH(games));
  prv_store_config();
  prv_init_layout();

  const AppMenuNode pebbletris = s_apps[3];
  s_apps[3] = s_apps[4];
  s_app_count = 4;
  launcher_layout_reload(&s_layout);
  cl_assert_equal_i(launcher_layout_get_row_count(&s_layout, 1), 0);

  // Reinstalled with a new install id but the same UUID, which is what membership is keyed on.
  s_apps[4] = pebbletris;
  s_apps[4].install_id = 42;
  s_app_count = 5;
  launcher_layout_reload(&s_layout);

  cl_assert_equal_i(launcher_layout_get_row_count(&s_layout, 1), 1);
  cl_assert_equal_s(prv_folder_app_name(1, 0), "Pebbletris");
}

void test_launcher_layout__no_apps_installed(void) {
  const uint8_t games[] = {4};
  prv_config_add_folder(1, "Games", games, ARRAY_LENGTH(games));
  prv_store_config();
  s_app_count = 0;
  prv_init_layout();

  cl_assert_equal_i(launcher_layout_get_row_count(&s_layout, LAUNCHER_FOLDER_ID_ROOT), 0);
  cl_assert_equal_i(launcher_layout_get_row_count(&s_layout, 1), 0);
  LauncherLayoutEntry entry;
  cl_assert(!launcher_layout_get_row(&s_layout, LAUNCHER_FOLDER_ID_ROOT, 0, &entry));
}

// Membership moves
////////////////////////////////////

void test_launcher_layout__app_moves_between_root_and_folder(void) {
  prv_config_add_folder(1, "Games", (const uint8_t[]){4}, 1);
  prv_store_config();
  prv_init_layout();
  cl_assert_equal_i(launcher_layout_get_row_count(&s_layout, 1), 1);
  launcher_layout_deinit(&s_layout);

  // The phone moves Calculator into Games as well.
  prv_config_reset();
  prv_config_add_folder(1, "Games", (const uint8_t[]){4, 5}, 2);
  prv_store_config();
  prv_init_layout();
  cl_assert_equal_i(launcher_layout_get_row_count(&s_layout, 1), 2);
  cl_assert_equal_i(launcher_layout_get_row_count(&s_layout, LAUNCHER_FOLDER_ID_ROOT), 4);
  launcher_layout_deinit(&s_layout);

  // ...and then moves both back to the root.
  prv_config_reset();
  prv_store_config();
  prv_init_layout();
  cl_assert_equal_i(launcher_layout_get_row_count(&s_layout, LAUNCHER_FOLDER_ID_ROOT), 5);
  cl_assert_equal_s(prv_root_app_name(3), "Pebbletris");
}

void test_launcher_layout__app_moves_between_folders(void) {
  prv_config_add_folder(1, "Navigation", (const uint8_t[]){2, 3}, 2);
  prv_config_add_folder(2, "Games", (const uint8_t[]){4}, 1);
  prv_store_config();
  prv_init_layout();
  cl_assert_equal_i(launcher_layout_get_row_count(&s_layout, 1), 2);
  cl_assert_equal_i(launcher_layout_get_row_count(&s_layout, 2), 1);
  launcher_layout_deinit(&s_layout);

  // Backtrack moves from Navigation to Games.
  prv_config_reset();
  prv_config_add_folder(1, "Navigation", (const uint8_t[]){2}, 1);
  prv_config_add_folder(2, "Games", (const uint8_t[]){4, 3}, 2);
  prv_store_config();
  prv_init_layout();

  cl_assert_equal_i(launcher_layout_get_row_count(&s_layout, 1), 1);
  cl_assert_equal_s(prv_folder_app_name(1, 0), "Compass");
  cl_assert_equal_i(launcher_layout_get_row_count(&s_layout, 2), 2);
  // Inside a folder the apps keep the flat app order, not the order listed in the configuration.
  cl_assert_equal_s(prv_folder_app_name(2, 0), "Backtrack");
  cl_assert_equal_s(prv_folder_app_name(2, 1), "Pebbletris");
}

// Broken configuration
////////////////////////////////////

void test_launcher_layout__unusable_config_falls_back_to_the_flat_list(void) {
  prv_config_add_folder(1, "Games", (const uint8_t[]){4}, 1);
  ((LauncherFolderConfig *)s_config_bytes)->version = LAUNCHER_FOLDER_STORAGE_VERSION + 1;
  // Written straight to flash: the endpoint would have rejected this.
  const int fd = pfs_open("lnc_fld", OP_FLAG_WRITE, FILE_TYPE_STATIC, s_config_size);
  cl_assert(fd >= 0);
  cl_assert_equal_i(pfs_write(fd, s_config_bytes, s_config_size), (int)s_config_size);
  cl_assert_equal_i(pfs_close(fd), S_SUCCESS);
  launcher_folder_storage_reset_for_tests();

  prv_init_layout();

  cl_assert(s_layout.config == NULL);
  cl_assert(!launcher_layout_has_folders(&s_layout));
  cl_assert_equal_i(launcher_layout_get_row_count(&s_layout, LAUNCHER_FOLDER_ID_ROOT), 5);
  cl_assert_equal_s(prv_root_app_name(0), "Music");
}

void test_launcher_layout__unknown_folder_id_has_no_rows(void) {
  prv_config_add_folder(1, "Games", (const uint8_t[]){4}, 1);
  prv_store_config();
  prv_init_layout();

  cl_assert_equal_i(launcher_layout_get_row_count(&s_layout, 200), 0);
  LauncherLayoutEntry entry;
  cl_assert(!launcher_layout_get_row(&s_layout, 200, 0, &entry));
  cl_assert_equal_i(launcher_layout_get_root_row_for_folder(&s_layout, 200), 0);
}
