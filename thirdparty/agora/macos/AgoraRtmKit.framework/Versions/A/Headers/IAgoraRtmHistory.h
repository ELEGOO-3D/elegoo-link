// Copyright (c) 2024 Agora.io. All rights reserved

// This program is confidential and proprietary to Agora.io.
// And may not be copied, reproduced, modified, disclosed to others, published
// or used, in whole or in part, without the express prior written permission
// of Agora.io.

#pragma once  // NOLINT(build/header_guard)

#include "AgoraRtmBase.h"

namespace agora {
namespace rtm {
/**
 * The IRtmHistory class.
 *
 * This class provides the rtm history methods that can be invoked by your app.
 */
class IRtmHistory {
 public:
  /**
   * gets history messages in the channel.
   *
   * @param [in] channelName The name of the channel.
   * @param [in] channelType The type of the channel.
   * @param [in] options The query options.
   * @param [out] requestId The related request id of this operation.
   * @return
   * - 0: Success.
   * - < 0: Failure.
   */
  virtual void getMessages(const char* channelName, RTM_CHANNEL_TYPE channelType, const GetHistoryMessagesOptions& options, uint64_t& requestId) = 0;

 protected:
  virtual ~IRtmHistory() {}
};

}  // namespace rtm
}  // namespace agora