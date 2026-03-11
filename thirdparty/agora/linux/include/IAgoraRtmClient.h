
// Copyright (c) 2022 Agora.io. All rights reserved

// This program is confidential and proprietary to Agora.io.
// And may not be copied, reproduced, modified, disclosed to others, published
// or used, in whole or in part, without the express prior written permission
// of Agora.io.

#pragma once  // NOLINT(build/header_guard)

#include "IAgoraRtmEventHandler.h"

#ifndef OPTIONAL_ENUM_CLASS
#if __cplusplus >= 201103L || (defined(_MSC_VER) && _MSC_VER >= 1800)
#define OPTIONAL_ENUM_CLASS enum class
#else
#define OPTIONAL_ENUM_CLASS enum
#endif
#endif

namespace agora {
namespace rtm {

/**
 * The IRtmClient class.
 *
 * This class provides the main methods that can be invoked by your app.
 *
 * IRtmClient is the basic interface class of the Agora RTM SDK.
 * Creating an IRtmClient object and then calling the methods of
 * this object enables you to use Agora RTM SDK's functionality.
 */
class IRtmClient {
 public:
  /**
   * Release the rtm client instance.
   *
   * @return
   * - 0: Success.
   * - < 0: Failure.
   */
  virtual int release() = 0;

  /**
   * Login the Agora RTM service. The operation result will be notified by \ref agora::rtm::IRtmEventHandler::onLoginResult callback.
   *
   * @param [in] token Token used to login RTM service.
   * @return
   * - 0: Success.
   * - < 0: Failure.
   */
  virtual void login(const char* token, uint64_t& requestId) = 0;

  /**
   * Logout the Agora RTM service. Be noticed that this method will break the rtm service including storage/lock/presence.
   *
   * @return
   * - 0: Success.
   * - < 0: Failure.
   */
  virtual void logout(uint64_t& requestId) = 0;

  /**
   * Get the storage instance.
   *
   * @return
   * - return NULL if error occurred
   */
  virtual IRtmStorage* getStorage() = 0;

  /**
   * Get the lock instance.
   *
   * @return
   * - return NULL if error occurred
   */
  virtual IRtmLock* getLock() = 0;

  /**
   * Get the presence instance.
   *
   * @return
   * - return NULL if error occurred
   */
  virtual IRtmPresence* getPresence() = 0;

  /**
   * Get the history instance.
   *
   * @return
   * - return NULL if error occurred
   */
  virtual IRtmHistory* getHistory() = 0;

  /**
   * Renews the token. Once a token is enabled and used, it expires after a certain period of time.
   * You should generate a new token on your server, call this method to renew it.
   *
   * @param [in] token Token used renew.
   * - 0: Success.
   * - < 0: Failure.
   */
  virtual void renewToken(const char* token, uint64_t& requestId) = 0;

  /**
   * Publish a message in the channel.
   *
   * @param [in] channelName The name of the channel.
   * @param [in] message The content of the message.
   * @param [in] length The length of the message.
   * @param [in] option The option of the message.
   * @param [out] requestId The related request id of this operation.
   * @return
   * - 0: Success.
   * - < 0: Failure.
   */
  virtual void publish(const char* channelName, const char* message, const size_t length, const PublishOptions& option, uint64_t& requestId) = 0;

  /**
   * Subscribe a channel.
   *
   * @param [in] channelName The name of the channel.
   * @param [in] options The options of subscribe the channel.
   * @return
   * - 0: Success.
   * - < 0: Failure.
   */
  virtual void subscribe(const char* channelName, const SubscribeOptions& options, uint64_t& requestId) = 0;

  /**
   * Unsubscribe a channel.
   *
   * @param [in] channelName The name of the channel.
   * @return
   * - 0: Success.
   * - < 0: Failure.
   */
  virtual void unsubscribe(const char* channelName, uint64_t& requestId) = 0;

  /**
   * Create a stream channel instance.
   *
   * @param [in] channelName The Name of the channel.
   * @param [out] errorCode The error code.
   * @return
   * - return NULL if error occurred
   */
  virtual IStreamChannel* createStreamChannel(const char* channelName, int& errorCode) = 0;

  /**
   * Set parameters of the sdk or engine
   *
   * @param [in] parameters The parameters in json format
   * @return
   * - 0: Success.
   * - < 0: Failure.
   */
  virtual int setParameters(const char* parameters) = 0;

 protected:
  virtual ~IRtmClient() {}
};

/**
 * Creates the rtm client object and returns the pointer.
 * 
 * @param [in] config The configuration of the rtm client.
 * @param [out] errorCode The error code.
 * @return Pointer of the rtm client object.
 */
AGORA_API IRtmClient* AGORA_CALL createAgoraRtmClient(const RtmConfig& config, int& errorCode);

/**
 * Convert error code to error string
 *
 * @param [in] errorCode Received error code
 * @return The error reason
 */
AGORA_API const char* AGORA_CALL getErrorReason(int errorCode);

/**
 * Get the version info of the Agora RTM SDK.
 *
 * @return The version info of the Agora RTM SDK.
 */
AGORA_API const char* AGORA_CALL getVersion();
}  // namespace rtm
}  // namespace agora
