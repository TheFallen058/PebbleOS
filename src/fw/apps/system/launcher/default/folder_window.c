/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "folder_window.h"

#include "menu_layer.h"

#include "applib/ui/app_window_stack.h"
#include "applib/ui/window.h"
#include "kernel/pbl_malloc.h"

typedef struct LauncherFolderWindowData {
  Window window;
  LauncherMenuLayer launcher_menu_layer;
  LauncherLayout *layout;
  LauncherFolderId folder_id;
} LauncherFolderWindowData;

//! Folders do not nest, so at most one folder window is ever on the stack.
static LauncherFolderWindowData *s_folder_window;

static void prv_window_load(Window *window) {
  LauncherFolderWindowData *data = window_get_user_data(window);

  LauncherMenuLayer *launcher_menu_layer = &data->launcher_menu_layer;
  launcher_menu_layer_init(launcher_menu_layer, data->layout, data->folder_id);
  launcher_menu_layer_set_click_config_onto_window(launcher_menu_layer, window);
  launcher_menu_layer_set_selection_animations_enabled(launcher_menu_layer, true);
  layer_add_child(window_get_root_layer(window),
                  launcher_menu_layer_get_layer(launcher_menu_layer));
}

static void prv_window_unload(Window *window) {
  LauncherFolderWindowData *data = window_get_user_data(window);

  launcher_menu_layer_deinit(&data->launcher_menu_layer);
  if (s_folder_window == data) {
    s_folder_window = NULL;
  }
  app_free(data);
}

void launcher_folder_window_push(LauncherLayout *layout, LauncherFolderId folder_id) {
  if (!layout || (folder_id == LAUNCHER_FOLDER_ID_ROOT) ||
      (launcher_layout_get_row_count(layout, folder_id) == 0)) {
    return;
  }

  LauncherFolderWindowData *data = app_zalloc_check(sizeof(*data));
  data->layout = layout;
  data->folder_id = folder_id;
  s_folder_window = data;

  Window *window = &data->window;
  window_init(window, WINDOW_NAME("Launcher Folder"));
  window_set_user_data(window, data);
  window_set_window_handlers(window, &(WindowHandlers){
                                       .load = prv_window_load,
                                       .unload = prv_window_unload,
                                     });

  const bool animated = true;
  app_window_stack_push(window, animated);
}

void launcher_folder_window_handle_apps_changed(void) {
  LauncherFolderWindowData *data = s_folder_window;
  if (!data) {
    return;
  }

  if (launcher_layout_get_row_count(data->layout, data->folder_id) == 0) {
    // The last app in the folder was uninstalled; showing an empty list would be a dead end.
    app_window_stack_remove(&data->window, true /* animated */);
    return;
  }

  launcher_menu_layer_reload_data(&data->launcher_menu_layer);
}
