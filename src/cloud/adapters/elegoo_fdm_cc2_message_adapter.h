#pragma once
#include <lan/adapters/elegoo_cc2_adapters.h>
#include <set>
namespace elink
{
    /**
     * Cloud Elegoo FDM CC2 Message Adapter
     * Extends ElegooFdmCC2MessageAdapter with timestamp-based data synchronization
     * for HTTP full status and MQTT incremental updates in cloud mode.
     *
     * Features:
     * - Timestamp-based conflict resolution
     * - Prevents data rollback from delayed HTTP responses
     * - Field-level precision control
     * - Backward compatible (falls back to parent behavior if no timestamps)
     */
    class CloudElegooFdmCC2MessageAdapter : public ElegooFdmCC2MessageAdapter
    {
    public:
        explicit CloudElegooFdmCC2MessageAdapter(const PrinterInfo &printerInfo);
        virtual ~CloudElegooFdmCC2MessageAdapter() = default;

        /**
         * Override convertToEvent to extract and handle timestamps
         * Supports both MQTT incremental data (meta_data.id) and HTTP full data (field_timestamps)
         */
        PrinterBizEvent convertToEvent(const std::string &printerMessage) override;

        /**
         * Override mergeStatusUpdateJson to apply timestamp checking on MQTT delta updates
         * This ensures MQTT delta timestamps are recorded when merging
         */
        nlohmann::json mergeStatusUpdateJson(const nlohmann::json &deltaStatusResult) override;

        /**
         * Clear status cache and timestamps on disconnect
         */
        void clearStatusCache() override;

    private:
        /**
         * Extract delta timestamp from MQTT message
         * @param statusJson Status JSON containing meta_data.id
         * @return Timestamp in milliseconds, or 0 if not found
         */
        int64_t extractDeltaTimestamp(const nlohmann::json &statusJson) const;

        /**
         * Extract field timestamps from HTTP full status response
         * @param fullStatusResult Full status result containing field_timestamps
         * @return Map of field paths to timestamps
         */
        std::map<std::string, int64_t> extractFullStatusTimestamps(
            const nlohmann::json &fullStatusResult) const;

        /**
         * Filter full status update based on timestamps
         * Only accept fields with newer timestamps than cached values
         * @param fullStatusResult Full status result from HTTP
         * @param newTimestamps Map of field paths to timestamps
         * @param acceptedFields Output set of field paths that were accepted
         * @return Filtered status JSON
         */
        nlohmann::json filterFullStatusByTimestamps(
            const nlohmann::json &fullStatusResult,
            const std::map<std::string, int64_t> &newTimestamps,
            std::set<std::string> &acceptedFields);

        /**
         * Recursively merge JSON with timestamp checking
         * @param target Target JSON (cached full status)
         * @param source Source JSON (delta update)
         * @param currentPath Current field path for tracking
         * @param timestamp Timestamp of the delta update
         */
        void mergeJsonWithTimestampCheck(
            nlohmann::json &target,
            const nlohmann::json &source,
            const std::string &currentPath,
            int64_t timestamp);

        /**
         * Update timestamps for all fields in a delta update
         * Used when no full status cache exists yet
         * @param deltaResult Delta status JSON
         * @param timestamp Timestamp to apply to all fields
         */
        void updateTimestampsFromDelta(
            const nlohmann::json &deltaResult,
            int64_t timestamp);

        /**
         * Check if a field should be updated based on timestamp
         * @param fieldPath Full path of the field (e.g., "extruder.temperature")
         * @param newTimestamp New timestamp to compare
         * @return true if field should be updated (new timestamp is newer)
         */
        bool shouldUpdateField(const std::string &fieldPath, int64_t newTimestamp) const;

        /**
         * Update cached timestamp for a field
         * @param fieldPath Full path of the field
         * @param timestamp New timestamp value
         */
        void updateFieldTimestamp(const std::string &fieldPath, int64_t timestamp);

        /**
         * Get cached timestamp for a field
         * @param fieldPath Full path of the field
         * @return Cached timestamp, or 0 if not found
         */
        int64_t getFieldTimestamp(const std::string &fieldPath) const;

        /**
         * Clear all cached timestamps
         */
        void clearTimestamps();

        // Timestamp management (private to this class)
        mutable std::mutex timestampMutex_;
        std::map<std::string, int64_t> fieldTimestamps_; // field path -> timestamp (milliseconds)

        // Partial status cache for MQTT deltas received before HTTP full status
        nlohmann::json partialStatusCache_;  // Cache MQTT delta data when no full status exists
        bool hasPartialStatusCache_ = false; // Whether we have cached MQTT delta
    };

} // namespace elink
