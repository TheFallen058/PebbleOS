/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "clar.h"

#include "pbl/services/comm_session/session.h"
#include "pbl/services/process_management/app_order_storage.h"
#include "pbl/services/process_management/launcher_folder_storage.h"
#include "pbl/util/uuid.h"

#include "stubs_logging.h"

extern void app_order_protocol_msg_callback(CommSession *session, const uint8_t *data,
                                            size_t length);

enum {
  AppOrderCommandOrder = 0x01,
  AppOrderCommandFolders = 0x02,
  AppOrderResponseSuccess = 0x01,
  AppOrderResponseFailure = 0x02,
  AppOrderResponseInvalid = 0x03,
  AppOrderEndpointId = 0xabcd,
};

static uint8_t s_response;
static uint8_t s_response_count;
static uint8_t s_order_write_count;
static uint8_t s_order_uuid_count;
static uint8_t s_folder_write_count;
static size_t s_folder_size;
static bool s_folder_valid;
static bool s_folder_write_success;

// Fakes
////////////////////////////////////

bool comm_session_send_data(CommSession *session, uint16_t endpoint_id, const uint8_t *data,
                            size_t length, uint32_t timeout_ms) {
  cl_assert_equal_i(endpoint_id, AppOrderEndpointId);
  cl_assert_equal_i(length, 1);
  s_response = data[0];
  s_response_count++;
  return true;
}

void write_uuid_list_to_file(const Uuid *uuid_list, uint8_t count) {
  s_order_write_count++;
  s_order_uuid_count = count;
}

bool launcher_folder_config_is_valid(const void *data, size_t size) {
  s_folder_size = size;
  return s_folder_valid;
}

bool launcher_folder_storage_write(const void *data, size_t size) {
  s_folder_write_count++;
  cl_assert_equal_i(size, s_folder_size);
  return s_folder_write_success;
}

void test_app_order_endpoint__initialize(void) {
  s_response = 0;
  s_response_count = 0;
  s_order_write_count = 0;
  s_order_uuid_count = 0;
  s_folder_write_count = 0;
  s_folder_size = 0;
  s_folder_valid = true;
  s_folder_write_success = true;
}

void test_app_order_endpoint__cleanup(void) {}

// Legacy flat order, unchanged
////////////////////////////////////

void test_app_order_endpoint__writes_legacy_order(void) {
  uint8_t message[2 + (2 * UUID_SIZE)] = {AppOrderCommandOrder, 2};

  app_order_protocol_msg_callback(NULL, message, sizeof(message));

  cl_assert_equal_i(s_order_write_count, 1);
  cl_assert_equal_i(s_order_uuid_count, 2);
  cl_assert_equal_i(s_response_count, 1);
  cl_assert_equal_i(s_response, AppOrderResponseSuccess);
}

void test_app_order_endpoint__writes_empty_legacy_order(void) {
  const uint8_t message[] = {AppOrderCommandOrder, 0};

  app_order_protocol_msg_callback(NULL, message, sizeof(message));

  cl_assert_equal_i(s_order_write_count, 1);
  cl_assert_equal_i(s_order_uuid_count, 0);
  cl_assert_equal_i(s_response, AppOrderResponseSuccess);
}

void test_app_order_endpoint__rejects_legacy_count_mismatch(void) {
  uint8_t message[2 + UUID_SIZE] = {AppOrderCommandOrder, 2};

  app_order_protocol_msg_callback(NULL, message, sizeof(message));

  cl_assert_equal_i(s_order_write_count, 0);
  cl_assert_equal_i(s_response_count, 1);
  cl_assert_equal_i(s_response, AppOrderResponseInvalid);
}

void test_app_order_endpoint__rejects_legacy_without_count(void) {
  const uint8_t message[] = {AppOrderCommandOrder};

  app_order_protocol_msg_callback(NULL, message, sizeof(message));

  cl_assert_equal_i(s_order_write_count, 0);
  cl_assert_equal_i(s_response, AppOrderResponseInvalid);
}

// Folder configuration
////////////////////////////////////

void test_app_order_endpoint__writes_folder_config(void) {
  const uint8_t message[] = {AppOrderCommandFolders, LAUNCHER_FOLDER_STORAGE_VERSION, 0, 0, 0};

  app_order_protocol_msg_callback(NULL, message, sizeof(message));

  cl_assert_equal_i(s_folder_size, sizeof(message) - 1);
  cl_assert_equal_i(s_folder_write_count, 1);
  cl_assert_equal_i(s_response, AppOrderResponseSuccess);
}

void test_app_order_endpoint__rejects_invalid_folder_config(void) {
  const uint8_t message[] = {AppOrderCommandFolders, 0xff};
  s_folder_valid = false;

  app_order_protocol_msg_callback(NULL, message, sizeof(message));

  cl_assert_equal_i(s_folder_write_count, 0);
  cl_assert_equal_i(s_response, AppOrderResponseInvalid);
}

void test_app_order_endpoint__reports_folder_write_failure(void) {
  const uint8_t message[] = {AppOrderCommandFolders, LAUNCHER_FOLDER_STORAGE_VERSION, 0, 0, 0};
  s_folder_write_success = false;

  app_order_protocol_msg_callback(NULL, message, sizeof(message));

  cl_assert_equal_i(s_folder_write_count, 1);
  cl_assert_equal_i(s_response, AppOrderResponseFailure);
}

void test_app_order_endpoint__folder_message_does_not_touch_the_flat_order(void) {
  const uint8_t message[] = {AppOrderCommandFolders, LAUNCHER_FOLDER_STORAGE_VERSION, 0, 0, 0};

  app_order_protocol_msg_callback(NULL, message, sizeof(message));

  cl_assert_equal_i(s_order_write_count, 0);
}

// Malformed input
////////////////////////////////////

void test_app_order_endpoint__rejects_empty_message(void) {
  app_order_protocol_msg_callback(NULL, NULL, 0);

  cl_assert_equal_i(s_response_count, 1);
  cl_assert_equal_i(s_response, AppOrderResponseInvalid);
}

void test_app_order_endpoint__rejects_unknown_command(void) {
  const uint8_t message[] = {0xff};

  app_order_protocol_msg_callback(NULL, message, sizeof(message));

  cl_assert_equal_i(s_response_count, 1);
  cl_assert_equal_i(s_response, AppOrderResponseFailure);
  cl_assert_equal_i(s_order_write_count, 0);
  cl_assert_equal_i(s_folder_write_count, 0);
}
