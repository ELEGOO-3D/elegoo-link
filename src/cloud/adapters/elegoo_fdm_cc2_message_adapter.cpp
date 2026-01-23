#include "adapters/elegoo_fdm_cc2_message_adapter.h"
#include "utils/logger.h"
#include "utils/json_utils.h"
#include "utils/utils.h"
#include <nlohmann/json.hpp>

namespace elink
{
    CloudElegooFdmCC2MessageAdapter::CloudElegooFdmCC2MessageAdapter(const PrinterInfo &printerInfo)
        : ElegooFdmCC2MessageAdapter(printerInfo)
    {
        ELEGOO_LOG_INFO("Created CloudElegooFdmCC2MessageAdapter for printer {}",
                        StringUtils::maskString(printerInfo.printerId));
    }

    PrinterBizEvent CloudElegooFdmCC2MessageAdapter::convertToEvent(const std::string &printerMessage)
    {
        try
        {
            auto printerJson = parseJson(printerMessage);
            if (printerJson.empty())
            {
                return PrinterBizEvent();
            }

            // Extract timestamps from HTTP response (field_timestamps in result)
            if (printerJson.contains("result") && printerJson["result"].is_object())
            {
                auto &result = printerJson["result"];
                if (result.contains("field_timestamps") && result["field_timestamps"].is_object())
                {
                    // Extract and store timestamps
                    auto fieldTimestamps = extractFullStatusTimestamps(result);

                    // Filter the result based on cached timestamps and get accepted fields
                    std::set<std::string> acceptedFields;
                    nlohmann::json filteredResult = filterFullStatusByTimestamps(result, fieldTimestamps, acceptedFields);

                    // Replace result with filtered version and remove field_timestamps
                    filteredResult.erase("field_timestamps");
                    printerJson["result"] = filteredResult;

                    // Only update timestamps for accepted fields
                    for (const auto &fieldPath : acceptedFields)
                    {
                        auto it = fieldTimestamps.find(fieldPath);
                        if (it != fieldTimestamps.end())
                        {
                            updateFieldTimestamp(fieldPath, it->second);
                            ELEGOO_LOG_TRACE("[TIMESTAMP] Updated timestamp for accepted field '{}' = {}", fieldPath, it->second);
                        }
                    }
                    ELEGOO_LOG_INFO("[TIMESTAMP] Updated timestamps for {} accepted HTTP fields", acceptedFields.size());
                }
            }

            // Extract timestamp from MQTT delta update (meta_data.id)
            // Note: mergeStatusUpdateJson override will handle this timestamp

            // Call parent implementation with modified JSON
            return ElegooFdmCC2MessageAdapter::convertToEvent(printerJson.dump());
        }
        catch (const std::exception &e)
        {
            ELEGOO_LOG_ERROR("Error in CloudElegooFdmCC2MessageAdapter::convertToEvent: {}", e.what());
            return PrinterBizEvent();
        }
    }

    void CloudElegooFdmCC2MessageAdapter::clearStatusCache()
    {
        // Clear partial status cache
        {
            std::lock_guard<std::mutex> lock(statusCacheMutex_);
            partialStatusCache_ = nlohmann::json();
            hasPartialStatusCache_ = false;
        }

        // Call parent to clear status cache
        ElegooFdmCC2MessageAdapter::clearStatusCache();

        // Also clear timestamps
        clearTimestamps();

        ELEGOO_LOG_DEBUG("Cleared cloud adapter cache, partial cache and timestamps");
    }

    nlohmann::json CloudElegooFdmCC2MessageAdapter::mergeStatusUpdateJson(const nlohmann::json &deltaStatusResult)
    {

        // Extract MQTT timestamp from meta_data.id
        int64_t mqttTimestamp = extractDeltaTimestamp(deltaStatusResult);
        if (mqttTimestamp > 0)
        {
            // MQTT delta with timestamp - merge with timestamp checking
            ELEGOO_LOG_DEBUG("Merging MQTT delta with timestamp {}", mqttTimestamp);

            std::lock_guard<std::mutex> lock(statusCacheMutex_);

            if (!hasFullStatusCache_)
            {
                // No full status cache yet, cache this delta for future HTTP merge
                ELEGOO_LOG_INFO("[TIMESTAMP] No full status cache, caching MQTT delta (partial status)");

                // Update timestamps for all fields in delta
                updateTimestampsFromDelta(deltaStatusResult, mqttTimestamp);

                // Merge with existing partial cache or create new
                if (hasPartialStatusCache_)
                {
                    // Already have partial cache, merge new delta into it
                    mergeJsonWithTimestampCheck(partialStatusCache_, deltaStatusResult, "", mqttTimestamp);
                    ELEGOO_LOG_DEBUG("[TIMESTAMP] Merged MQTT delta into existing partial cache");
                }
                else
                {
                    // First delta, create partial cache
                    partialStatusCache_ = deltaStatusResult;
                    hasPartialStatusCache_ = true;
                    ELEGOO_LOG_DEBUG("[TIMESTAMP] Created new partial cache from MQTT delta");
                }

                ELEGOO_LOG_INFO("[TIMESTAMP] Partial cache ready, will merge with HTTP when it arrives");

                // Return delta as-is, parent class will handle it

                return nlohmann::json(); // Indicate no full merge yet
            }

            // Have full status cache, merge delta into it
            nlohmann::json mergedResult = cachedFullStatusJson_;

            // Merge with timestamp checking (also updates cached timestamps)
            mergeJsonWithTimestampCheck(mergedResult, deltaStatusResult, "", mqttTimestamp);

            // Update cache
            cachedFullStatusJson_ = mergedResult;

            return mergedResult;
        }
        else
        {
            // No timestamp available, fall back to parent implementation
            ELEGOO_LOG_DEBUG("MQTT delta without timestamp, using parent merge");
            return ElegooFdmCC2MessageAdapter::mergeStatusUpdateJson(deltaStatusResult);
        }
    }

    int64_t CloudElegooFdmCC2MessageAdapter::extractDeltaTimestamp(const nlohmann::json &statusJson) const
    {
        // MQTT message structure: {"id":0, "method":6000, "result":{...data with meta_data...}}
        // Extract timestamp from MQTT message: result.meta_data.id

        // First check if meta_data is at top level (old path)
        if (statusJson.contains("meta_data") && statusJson["meta_data"].is_object())
        {
            int64_t ts = JsonUtils::safeGetInt64(statusJson["meta_data"], "id", 0);
            if (ts > 0)
            {
                return ts;
            }
        }

        // Check if it's in result.meta_data (reportValue parsed structure)
        // This is NOT needed here because statusJson is already the deltaStatusResult passed from mergeStatusUpdateJson
        // which is the full status cache merged result, not the raw MQTT message

        ELEGOO_LOG_WARN("[TIMESTAMP] No valid MQTT timestamp found in delta update");
        return 0;
    }

    std::map<std::string, int64_t> CloudElegooFdmCC2MessageAdapter::extractFullStatusTimestamps(
        const nlohmann::json &fullStatusResult) const
    {
        std::map<std::string, int64_t> timestamps;

        if (!fullStatusResult.contains("field_timestamps") || !fullStatusResult["field_timestamps"].is_object())
        {
            return timestamps;
        }

        const auto &fieldTs = fullStatusResult["field_timestamps"];

        // field_timestamps format:  {"extruder": {"temperature": 1768527842273}, "machine_status": {...}}
        for (auto &[key, value] : fieldTs.items())
        {
            if (value.is_object())
            {
                for (auto &[subKey, timestamp] : value.items())
                {
                    std::string fullPath = key + "." + subKey;
                    timestamps[fullPath] = JsonUtils::safeGetInt64(value, subKey, 0);
                }
            }
        }

        ELEGOO_LOG_DEBUG("Extracted {} field timestamps from HTTP response", timestamps.size());
        return timestamps;
    }

    nlohmann::json CloudElegooFdmCC2MessageAdapter::filterFullStatusByTimestamps(
        const nlohmann::json &fullStatusResult,
        const std::map<std::string, int64_t> &newTimestamps,
        std::set<std::string> &acceptedFields)
    {
        acceptedFields.clear();

        // Get current caches (outside lock)
        nlohmann::json currentCache;
        nlohmann::json partialCache;
        bool hasCache = false;
        bool hasPartial = false;
        {
            std::lock_guard<std::mutex> statusLock(statusCacheMutex_);
            hasCache = hasFullStatusCache_;
            if (hasCache)
            {
                currentCache = cachedFullStatusJson_;
            }
            hasPartial = hasPartialStatusCache_;
            if (hasPartial)
            {
                partialCache = partialStatusCache_;
                ELEGOO_LOG_INFO("[TIMESTAMP] Have partial cache from MQTT, will merge with HTTP");
            }
        }

        // Start with HTTP full status as base
        nlohmann::json result = fullStatusResult;
        result.erase("field_timestamps");

        int rejectedCount = 0;

        // Check if we have any cached timestamps (even without full status cache)
        std::lock_guard<std::mutex> tsLock(timestampMutex_);
        bool hasAnyTimestamps = !fieldTimestamps_.empty();

        // If we have cached timestamps, selectively merge with timestamp checking
        if (hasAnyTimestamps)
        {
            ELEGOO_LOG_DEBUG("[TIMESTAMP] Have {} cached timestamps, checking HTTP fields", fieldTimestamps_.size());

            // Iterate through HTTP fields
            for (const auto &[fieldPath, httpTs] : newTimestamps)
            {
                // Parse field path (e.g., "extruder.temperature")
                auto dotPos = fieldPath.find('.');
                if (dotPos == std::string::npos)
                    continue;

                std::string key = fieldPath.substr(0, dotPos);
                std::string subKey = fieldPath.substr(dotPos + 1);

                // Get cached timestamp
                auto it = fieldTimestamps_.find(fieldPath);
                int64_t cachedTs = (it != fieldTimestamps_.end()) ? it->second : 0;

                // If we have a cached timestamp and it's newer than HTTP
                if (it != fieldTimestamps_.end() && cachedTs > httpTs)
                {
                    // Try to restore from full cache first, then partial cache
                    bool restored = false;

                    if (hasCache && currentCache.contains(key) && currentCache[key].is_object() &&
                        currentCache[key].contains(subKey))
                    {
                        if (!result.contains(key) || !result[key].is_object())
                        {
                            result[key] = nlohmann::json::object();
                        }
                        result[key][subKey] = currentCache[key][subKey];
                        restored = true;
                        ELEGOO_LOG_DEBUG("[TIMESTAMP] HTTP field '{}': REJECTED httpTs={} < cachedTs={}, kept full cache value",
                                         fieldPath, httpTs, cachedTs);
                    }
                    else if (hasPartial && partialCache.contains(key) && partialCache[key].is_object() &&
                             partialCache[key].contains(subKey))
                    {
                        if (!result.contains(key) || !result[key].is_object())
                        {
                            result[key] = nlohmann::json::object();
                        }
                        result[key][subKey] = partialCache[key][subKey];
                        restored = true;
                        ELEGOO_LOG_DEBUG("[TIMESTAMP] HTTP field '{}': REJECTED httpTs={} < cachedTs={}, kept partial cache value",
                                         fieldPath, httpTs, cachedTs);
                    }

                    if (restored)
                    {
                        rejectedCount++;
                    }
                    else
                    {
                        // No cached value available, have to accept HTTP
                        acceptedFields.insert(fieldPath);
                        ELEGOO_LOG_WARN("[TIMESTAMP] HTTP field '{}': httpTs={} < cachedTs={} but no cached value, accepting HTTP",
                                        fieldPath, httpTs, cachedTs);
                    }
                }
                else
                {
                    // Accept HTTP value
                    acceptedFields.insert(fieldPath);
                    ELEGOO_LOG_TRACE("[TIMESTAMP] HTTP field '{}': ACCEPTED httpTs={} >= cachedTs={}",
                                     fieldPath, httpTs, cachedTs);
                }
            }

            ELEGOO_LOG_INFO("[TIMESTAMP] HTTP merge: {} fields accepted, {} fields rejected/kept",
                            acceptedFields.size(), rejectedCount);
        }
        else
        {
            // No cache, accept all fields
            for (const auto &[fieldPath, httpTs] : newTimestamps)
            {
                acceptedFields.insert(fieldPath);
            }
            ELEGOO_LOG_INFO("[TIMESTAMP] No cache, accepting all {} HTTP fields", acceptedFields.size());
        }

        return result;
    }

    void CloudElegooFdmCC2MessageAdapter::mergeJsonWithTimestampCheck(
        nlohmann::json &target,
        const nlohmann::json &source,
        const std::string &currentPath,
        int64_t timestamp)
    {
        for (auto &[key, value] : source.items())
        {
            // Skip meta_data field (it's not printer status data)
            if (key == "meta_data")
            {
                continue;
            }

            std::string fieldPath = currentPath.empty() ? key : currentPath + "." + key;

            if (value.is_object())
            {
                // Recursive merge for nested objects
                if (!target.contains(key) || !target[key].is_object())
                {
                    target[key] = nlohmann::json::object();
                }
                mergeJsonWithTimestampCheck(target[key], value, fieldPath, timestamp);
            }
            else
            {
                // Leaf value - check timestamp before updating
                if (shouldUpdateField(fieldPath, timestamp))
                {
                    target[key] = value;
                    updateFieldTimestamp(fieldPath, timestamp);
                    ELEGOO_LOG_TRACE("Updated field {} with timestamp {}", fieldPath, timestamp);
                }
                else
                {
                    ELEGOO_LOG_TRACE("Skipped field {} (cached_ts > delta_ts={})", fieldPath, timestamp);
                }
            }
        }
    }

    void CloudElegooFdmCC2MessageAdapter::updateTimestampsFromDelta(
        const nlohmann::json &deltaResult,
        int64_t timestamp)
    {
        // Recursively traverse delta and update all field timestamps
        std::function<void(const nlohmann::json &, const std::string &)> traverse;
        traverse = [&](const nlohmann::json &obj, const std::string &path)
        {
            for (auto &[key, value] : obj.items())
            {
                // Skip meta_data
                if (key == "meta_data")
                {
                    continue;
                }

                std::string fieldPath = path.empty() ? key : path + "." + key;

                if (value.is_object())
                {
                    traverse(value, fieldPath);
                }
                else
                {
                    // Leaf value - update timestamp
                    updateFieldTimestamp(fieldPath, timestamp);
                }
            }
        };

        traverse(deltaResult, "");
        ELEGOO_LOG_DEBUG("Updated timestamps for MQTT delta fields with ts={}", timestamp);
    }

    // ========== Private Timestamp Management ==========

    bool CloudElegooFdmCC2MessageAdapter::shouldUpdateField(
        const std::string &fieldPath, int64_t newTimestamp) const
    {
        // If timestamp is 0, always accept (backward compatibility)
        if (newTimestamp == 0)
        {
            ELEGOO_LOG_TRACE("[TIMESTAMP] Field '{}': newTs=0, accepting (backward compat)", fieldPath);
            return true;
        }

        std::lock_guard<std::mutex> lock(timestampMutex_);
        auto it = fieldTimestamps_.find(fieldPath);

        // If no cached timestamp, accept new data
        if (it == fieldTimestamps_.end())
        {
            ELEGOO_LOG_DEBUG("[TIMESTAMP] Field '{}': newTs={}, no cached ts, accepting", fieldPath, newTimestamp);
            return true;
        }

        // Only accept if new timestamp is newer
        bool shouldUpdate = newTimestamp >= it->second;
        if (!shouldUpdate)
        {
            ELEGOO_LOG_WARN("[TIMESTAMP] Field '{}': REJECTED newTs={} < cachedTs={}",
                            fieldPath, newTimestamp, it->second);
        }
        else
        {
            ELEGOO_LOG_DEBUG("[TIMESTAMP] Field '{}': ACCEPTED newTs={} >= cachedTs={}",
                             fieldPath, newTimestamp, it->second);
        }
        return shouldUpdate;
    }

    void CloudElegooFdmCC2MessageAdapter::updateFieldTimestamp(
        const std::string &fieldPath, int64_t timestamp)
    {
        std::lock_guard<std::mutex> lock(timestampMutex_);
        fieldTimestamps_[fieldPath] = timestamp;
    }

    int64_t CloudElegooFdmCC2MessageAdapter::getFieldTimestamp(
        const std::string &fieldPath) const
    {
        std::lock_guard<std::mutex> lock(timestampMutex_);
        auto it = fieldTimestamps_.find(fieldPath);
        return (it != fieldTimestamps_.end()) ? it->second : 0;
    }

    void CloudElegooFdmCC2MessageAdapter::clearTimestamps()
    {
        std::lock_guard<std::mutex> lock(timestampMutex_);
        fieldTimestamps_.clear();
        ELEGOO_LOG_DEBUG("Cleared all field timestamps");
    }

} // namespace elink
