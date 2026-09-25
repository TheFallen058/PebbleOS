/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "launcher_layout.h"

#include "kernel/pbl_malloc.h"
#include "system/passert.h"

#include <string.h>

static void prv_free_index(LauncherLayout *layout) {
  app_free(layout->app_folders);
  layout->app_folders = NULL;
  app_free(layout->root_rows);
  layout->root_rows = NULL;
  layout->app_count = 0;
  layout->root_count = 0;
}

static void prv_rebuild(LauncherLayout *layout) {
  prv_free_index(layout);

  // Without folders the launcher shows the data source as-is, so there is nothing to index and
  // nothing to allocate.
  if (!layout->config) {
    return;
  }

  const uint16_t app_count = app_menu_data_source_get_count(layout->data_source);
  if (app_count == 0) {
    return;
  }

  layout->app_folders = app_malloc(app_count * sizeof(*layout->app_folders));
  layout->root_rows = app_malloc(app_count * sizeof(*layout->root_rows));
  if (!layout->app_folders || !layout->root_rows) {
    // Without the index we cannot group; fall back to the flat list rather than failing to
    // render the launcher at all.
    prv_free_index(layout);
    return;
  }
  layout->app_count = app_count;

  bool folder_shown[LAUNCHER_FOLDER_MAX_COUNT + 1] = {0};

  for (uint16_t i = 0; i < app_count; i++) {
    AppMenuNode *node = app_menu_data_source_get_node_at_index(layout->data_source, i);
    const LauncherFolderId folder_id =
        node ? launcher_folder_config_get_folder_for_app(layout->config, &node->uuid)
             : LAUNCHER_FOLDER_ID_ROOT;
    layout->app_folders[i] = folder_id;

    if (folder_id == LAUNCHER_FOLDER_ID_ROOT) {
      layout->root_rows[layout->root_count++] = (LauncherLayoutRootRow){
        .app_index = i,
        .folder_id = LAUNCHER_FOLDER_ID_ROOT,
      };
      continue;
    }

    // A folder takes the slot of its first installed member.
    if (folder_id <= LAUNCHER_FOLDER_MAX_COUNT && folder_shown[folder_id]) {
      continue;
    }
    if (folder_id <= LAUNCHER_FOLDER_MAX_COUNT) {
      folder_shown[folder_id] = true;
    }
    layout->root_rows[layout->root_count++] = (LauncherLayoutRootRow){
      .app_index = i,
      .folder_id = folder_id,
    };
  }
}

static void prv_rebuild_if_needed(LauncherLayout *layout) {
  if (layout->needs_rebuild) {
    layout->needs_rebuild = false;
    prv_rebuild(layout);
  }
}

void launcher_layout_init(LauncherLayout *layout, AppMenuDataSource *data_source) {
  PBL_ASSERTN(layout && data_source);
  *layout = (LauncherLayout){
    .data_source = data_source,
    .config = launcher_folder_storage_read(),
    .needs_rebuild = true,
  };
}

void launcher_layout_deinit(LauncherLayout *layout) {
  if (!layout) {
    return;
  }
  prv_free_index(layout);
  app_free(layout->config);
  layout->config = NULL;
}

void launcher_layout_reload(LauncherLayout *layout) {
  if (layout) {
    layout->needs_rebuild = true;
  }
}

bool launcher_layout_has_folders(LauncherLayout *layout) {
  if (!layout || !layout->config) {
    return false;
  }
  prv_rebuild_if_needed(layout);
  return (layout->root_count != layout->app_count);
}

uint16_t launcher_layout_get_row_count(LauncherLayout *layout, LauncherFolderId folder_id) {
  if (!layout) {
    return 0;
  }
  prv_rebuild_if_needed(layout);

  if (!layout->config || !layout->root_rows) {
    return (folder_id == LAUNCHER_FOLDER_ID_ROOT)
               ? app_menu_data_source_get_count(layout->data_source)
               : 0;
  }

  if (folder_id == LAUNCHER_FOLDER_ID_ROOT) {
    return layout->root_count;
  }

  uint16_t count = 0;
  for (uint16_t i = 0; i < layout->app_count; i++) {
    if (layout->app_folders[i] == folder_id) {
      count++;
    }
  }
  return count;
}

static bool prv_set_app_entry(LauncherLayout *layout, uint16_t app_index,
                              LauncherLayoutEntry *entry_out) {
  AppMenuNode *node = app_menu_data_source_get_node_at_index(layout->data_source, app_index);
  if (!node) {
    return false;
  }
  *entry_out = (LauncherLayoutEntry){
    .type = LauncherLayoutEntryTypeApp,
    .app = node,
  };
  return true;
}

bool launcher_layout_get_row(LauncherLayout *layout, LauncherFolderId folder_id, uint16_t row,
                             LauncherLayoutEntry *entry_out) {
  if (!layout || !entry_out) {
    return false;
  }
  prv_rebuild_if_needed(layout);

  if (!layout->config || !layout->root_rows) {
    if ((folder_id != LAUNCHER_FOLDER_ID_ROOT) ||
        (row >= app_menu_data_source_get_count(layout->data_source))) {
      return false;
    }
    return prv_set_app_entry(layout, row, entry_out);
  }

  if (folder_id == LAUNCHER_FOLDER_ID_ROOT) {
    if (row >= layout->root_count) {
      return false;
    }
    const LauncherLayoutRootRow *root_row = &layout->root_rows[row];
    if (root_row->folder_id == LAUNCHER_FOLDER_ID_ROOT) {
      return prv_set_app_entry(layout, root_row->app_index, entry_out);
    }
    const LauncherFolderRecord *record =
        launcher_folder_config_find(layout->config, root_row->folder_id);
    if (!record) {
      return false;
    }
    *entry_out = (LauncherLayoutEntry){
      .type = LauncherLayoutEntryTypeFolder,
      .folder = record,
    };
    return true;
  }

  uint16_t index = 0;
  for (uint16_t i = 0; i < layout->app_count; i++) {
    if ((layout->app_folders[i] == folder_id) && (index++ == row)) {
      return prv_set_app_entry(layout, i, entry_out);
    }
  }
  return false;
}

uint16_t launcher_layout_get_root_row_for_folder(LauncherLayout *layout,
                                                 LauncherFolderId folder_id) {
  if (!layout) {
    return 0;
  }
  prv_rebuild_if_needed(layout);

  for (uint16_t row = 0; row < layout->root_count; row++) {
    if (layout->root_rows[row].folder_id == folder_id) {
      return row;
    }
  }
  return 0;
}
