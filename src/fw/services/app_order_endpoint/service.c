/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "pbl/services/process_management/app_order_storage.h"
#include "pbl/services/process_management/launcher_folder_storage.h"
#include "pbl/services/comm_session/session.h"
#include <pbl/logging/logging.h>
#include "pbl/util/uuid.h"

PBL_LOG_MODULE_DEFINE(service_app_order_endpoint, CONFIG_SERVICE_APP_ORDER_ENDPOINT_LOG_LEVEL);

//! @file
//! App Order Endpoint
//!
//! Flat app order. This is the order old firmware understands and its meaning has never
//! changed, so the phone keeps sending it even when it also manages folders.

//! \code{.c}
//! 0x01 <uint8_t num_uuids>
//! <16-byte UUID_1>
//! ...
//! <16-byte UUID_N>
//! \endcode

//! Launcher folder configuration. Carried separately from the order above because folders are
//! launcher metadata: a watch that does not understand this message still shows every app, just
//! ungrouped, in the order sent by 0x01.

//! \code{.c}
//! 0x02 <uint8_t version> <uint8_t folder_count> <uint16_t data_size>
//! Repeated folder_count times:
//!   <uint8_t folder_id> <uint8_t member_count> <25-byte NUL-terminated UTF-8 name>
//!   <16-byte member UUID_1> ... <16-byte member UUID_N>
//! \endcode
//!
//! Multi-byte fields are little-endian. See \ref launcher_folder_storage.h for the limits and
//! the validation rules applied before anything is persisted.

//! AppOrder Endpoint ID
static const uint16_t APP_ORDER_ENDPOINT_ID = 0xabcd;

typedef enum {
  APP_ORDER_CMD = 0x01,
  LAUNCHER_FOLDERS_CMD = 0x02,
} AppOrderCommand;

typedef enum {
  APP_ORDER_RES_SUCCESS = 0x01,
  APP_ORDER_RES_FAILURE = 0x02,
  APP_ORDER_RES_INVALID = 0x03,
  APP_ORDER_RES_RETRY_LATER = 0x04,
} AppOrderResponse;

typedef struct {
  CommSession *session;
  uint8_t result;
} ResponseInfo;

static void prv_send_result(CommSession *session, uint8_t result) {
  PBL_LOG_DBG("Sending result of %d", result);
  comm_session_send_data(session, APP_ORDER_ENDPOINT_ID, (uint8_t *)&result, sizeof(result),
                         COMM_SESSION_DEFAULT_TIMEOUT);
}

static void prv_handle_app_order_msg(CommSession *session, const uint8_t *data, size_t length) {
  if (length < sizeof(uint8_t)) {
    prv_send_result(session, APP_ORDER_RES_INVALID);
    return;
  }

  const uint8_t num_uuids = data[0];
  if (length != (sizeof(num_uuids) + ((size_t)num_uuids * UUID_SIZE))) {
    PBL_LOG_DBG("invalid length, num_uuids does not match with the length of message");
    prv_send_result(session, APP_ORDER_RES_INVALID);
    return;
  }

  write_uuid_list_to_file((const Uuid *)&data[1], num_uuids);
  prv_send_result(session, APP_ORDER_RES_SUCCESS);
}

static void prv_handle_launcher_folders_msg(CommSession *session, const uint8_t *data,
                                            size_t length) {
  if (!launcher_folder_config_is_valid(data, length)) {
    PBL_LOG_DBG("Rejecting malformed launcher folder configuration");
    prv_send_result(session, APP_ORDER_RES_INVALID);
    return;
  }

  prv_send_result(session, launcher_folder_storage_write(data, length) ? APP_ORDER_RES_SUCCESS
                                                                       : APP_ORDER_RES_FAILURE);
}

void app_order_protocol_msg_callback(CommSession *session, const uint8_t *data, size_t length) {
  if (!data || (length < sizeof(uint8_t))) {
    prv_send_result(session, APP_ORDER_RES_INVALID);
    return;
  }

  switch (data[0]) {
    case APP_ORDER_CMD:
      PBL_LOG_DBG("Got APP_ORDER message");
      prv_handle_app_order_msg(session, &data[1], length - 1);
      break;
    case LAUNCHER_FOLDERS_CMD:
      PBL_LOG_DBG("Got LAUNCHER_FOLDERS message");
      prv_handle_launcher_folders_msg(session, &data[1], length - 1);
      break;
    default:
      PBL_LOG_ERR("Invalid message received, first byte is %u", data[0]);
      prv_send_result(session, APP_ORDER_RES_FAILURE);
      break;
  }
}
