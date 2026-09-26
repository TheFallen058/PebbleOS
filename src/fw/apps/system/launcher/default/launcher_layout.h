/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include "pbl/services/process_management/launcher_folder_storage.h"
#include "process_management/app_menu_data_source.h"

//! @file
//! Folder-aware view over an \ref AppMenuDataSource.
//!
//! Folders only exist for the launcher, so the grouping lives here instead of in
//! AppMenuDataSource, which is shared with Quick Launch, the watchface selector and the activity
//! tracker settings. Those keep seeing the plain flat list of installed apps.
//!
//! A folder has no order of its own: it occupies the slot of its first member in the app order
//! that is already synchronised from the phone. That keeps the legacy flat order the single
//! source of truth for ordering and means old firmware, which only understands that order, shows
//! the same apps in the same sequence.
//!
//! Only folders with at least one installed member produce a row, so uninstalling the last app in
//! a folder hides it without touching the stored configuration.

typedef enum LauncherLayoutEntryType {
  LauncherLayoutEntryTypeApp,
  LauncherLayoutEntryTypeFolder,
} LauncherLayoutEntryType;

typedef struct LauncherLayoutEntry {
  LauncherLayoutEntryType type;
  //! Set when type is LauncherLayoutEntryTypeApp.
  AppMenuNode *app;
  //! Set when type is LauncherLayoutEntryTypeFolder.
  const LauncherFolderRecord *folder;
} LauncherLayoutEntry;

typedef struct PBL_PACKED LauncherLayoutRootRow {
  //! Data source row, meaningful when folder_id is LAUNCHER_FOLDER_ID_ROOT.
  uint16_t app_index;
  LauncherFolderId folder_id;
} LauncherLayoutRootRow;

typedef struct LauncherLayout {
  AppMenuDataSource *data_source;
  //! NULL when no folders are configured, in which case the layout is a pass-through and
  //! allocates nothing.
  LauncherFolderConfig *config;
  //! Folder of each installed app, indexed by data source row.
  LauncherFolderId *app_folders;
  LauncherLayoutRootRow *root_rows;
  uint16_t app_count;
  uint16_t root_count;
  bool needs_rebuild;
} LauncherLayout;

//! Reads the stored folder configuration and attaches the layout to \a data_source.
void launcher_layout_init(LauncherLayout *layout, AppMenuDataSource *data_source);

void launcher_layout_deinit(LauncherLayout *layout);

//! Marks the grouping stale after the set of installed apps changed. The configuration itself is
//! only read once, when the launcher opens.
void launcher_layout_reload(LauncherLayout *layout);

//! @return true if at least one configured folder currently has an installed member.
bool launcher_layout_has_folders(LauncherLayout *layout);

//! @param folder_id LAUNCHER_FOLDER_ID_ROOT for the root list, otherwise a folder's contents.
uint16_t launcher_layout_get_row_count(LauncherLayout *layout, LauncherFolderId folder_id);

bool launcher_layout_get_row(LauncherLayout *layout, LauncherFolderId folder_id, uint16_t row,
                             LauncherLayoutEntry *entry_out);

//! @return the root row showing \a folder_id, or 0 if it is no longer shown.
uint16_t launcher_layout_get_root_row_for_folder(LauncherLayout *layout,
                                                 LauncherFolderId folder_id);
