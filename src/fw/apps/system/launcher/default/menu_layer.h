/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include "app_glance.h"
#include "app_glance_service.h"
#include "launcher_layout.h"

#include "applib/preferred_content_size.h"
#include "process_management/app_menu_data_source.h"

//! Fonts and cell geometry of the launcher for one content size
typedef struct LauncherMenuLayerStyle {
  const char *title_font_key;
  const char *subtitle_font_key;
  //! Vertical margin between the title and the subtitle
  int16_t title_margin_h;
#if PBL_RECT
  int16_t cell_height;
#else
  int16_t focused_cell_height;
  int16_t unfocused_cell_height;
#endif
} LauncherMenuLayerStyle;

//! Called when the user selects a folder row. The launcher app pushes the folder's window; the
//! menu layer itself stays out of window management so it can be reused for that window.
typedef void (*LauncherMenuLayerFolderSelectedHandler)(LauncherFolderId folder_id, void *context);

typedef struct LauncherMenuLayer {
  Layer container_layer;
  MenuLayer menu_layer;
#if PBL_ROUND
  Layer up_arrow_layer;
  Layer down_arrow_layer;
#endif
  PreferredContentSize content_size;
  LauncherLayout *layout;
  //! LAUNCHER_FOLDER_ID_ROOT for the launcher root, otherwise the folder being shown.
  LauncherFolderId folder_id;
  LauncherAppGlanceService glance_service;
  //! Lazily created the first time a folder row is drawn; folders are not apps and so are not
  //! cached per UUID by the glance service.
  LauncherAppGlance *folder_glance;
  LauncherMenuLayerFolderSelectedHandler folder_selected;
  void *folder_selected_context;
  bool selection_animations_enabled;
  AppInstallId app_to_launch_after_next_render;
} LauncherMenuLayer;

typedef struct LauncherMenuLayerSelectionState {
  int16_t scroll_offset_y;
  uint16_t row_index;
  //! Content size the scroll offset was captured with
  PreferredContentSize content_size;
} LauncherMenuLayerSelectionState;

//! @return The style for the user's preferred content size
const LauncherMenuLayerStyle *launcher_menu_layer_get_style(void);

//! @param folder_id LAUNCHER_FOLDER_ID_ROOT to show the launcher root, otherwise a folder.
void launcher_menu_layer_init(LauncherMenuLayer *launcher_menu_layer, LauncherLayout *layout,
                              LauncherFolderId folder_id);

void launcher_menu_layer_set_folder_selected_handler(
    LauncherMenuLayer *launcher_menu_layer, LauncherMenuLayerFolderSelectedHandler handler,
    void *context);

Layer *launcher_menu_layer_get_layer(LauncherMenuLayer *launcher_menu_layer);

void launcher_menu_layer_set_click_config_onto_window(LauncherMenuLayer *launcher_menu_layer,
                                                      Window *window);

void launcher_menu_layer_reload_data(LauncherMenuLayer *launcher_menu_layer);

//! Re-initialize the launcher menu layer if the user's preferred content size changed since it was
//! initialized, keeping the current selection.
void launcher_menu_layer_update_content_size(LauncherMenuLayer *launcher_menu_layer);

void launcher_menu_layer_set_selection_state(LauncherMenuLayer *launcher_menu_layer,
                                             const LauncherMenuLayerSelectionState *new_state);

void launcher_menu_layer_get_selection_state(const LauncherMenuLayer *launcher_menu_layer,
                                             LauncherMenuLayerSelectionState *state_out);

void launcher_menu_layer_get_selection_vertical_range(const LauncherMenuLayer *launcher_menu_layer,
                                                      GRangeVertical *vertical_range_out);

void launcher_menu_layer_set_selection_animations_enabled(LauncherMenuLayer *launcher_menu_layer,
                                                          bool enabled);

void launcher_menu_layer_deinit(LauncherMenuLayer *launcher_menu_layer);
