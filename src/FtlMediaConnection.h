/**
 * @file FtlMediaConnection.h
 * @author Hayden McAfee (hayden@outlook.com)
 * @version 0.1
 * @date 2020-08-11
 * 
 * @copyright Copyright (c) 2020 Hayden McAfee
 * 
 */
#pragma once

#include "Utilities/FtlTypes.h"
#include "Utilities/Result.h"

#include <chrono>
#include <functional>
#include <list>
#include <map>
#include <set>
#include <shared_mutex>
#include <unordered_map>
#include <set>
#include <thread>
#include <vector>

class ConnectionTransport;
class FtlControlConnection;

/**
 * @brief Manages the FTL media stream, accepting incoming RTP packets.
 */
class FtlMediaConnection
{
public:
    /* Public types */
    using ClosedCallback = std::function<void(FtlMediaConnection&)>;
    using RtpPacketCallback = std::function<void(const std::vector<std::byte>&)>;

    /* Constructor/Destructor */
    FtlMediaConnection(
        std::unique_ptr<ConnectionTransport> transport,
        const MediaMetadata mediaMetadata,
        const ftl_channel_id_t channelId,
        const ftl_stream_id_t streamId,
        const ClosedCallback onClosed,
        const RtpPacketCallback onRtpPacket,
        const bool nackLostPackets = true);

    /* Public methods */
    void RequestStop();

    /* Getters/Setters */
    FtlStreamStats GetStats();
    Result<FtlKeyframe> GetKeyframe();

private:
    /* Private types */
    struct SsrcData
    {
        uint32_t PacketsReceived = 0;
        uint32_t PacketsNacked = 0;
        uint32_t PacketsLost = 0;
        size_t PacketsSinceLastMissedSequence = 0;
        std::list<std::vector<std::byte>> CircularPacketBuffer;
        std::map<std::chrono::time_point<std::chrono::steady_clock>, uint16_t> 
            RollingBytesReceivedByTime;
        std::set<rtp_sequence_num_t> NackQueue;
        std::set<rtp_sequence_num_t> NackedSequences;
        std::list<std::vector<std::byte>> CurrentKeyframePackets;
        std::list<std::vector<std::byte>> PendingKeyframePackets;
    };

    /* Constants */
    static constexpr uint64_t            MICROSECONDS_PER_SECOND        = 1000000;
    static constexpr float               MICROSECONDS_PER_MILLISECOND   = 1000.0f;
    static constexpr rtp_payload_type_t  FTL_PAYLOAD_TYPE_SENDER_REPORT = 200;
    static constexpr rtp_payload_type_t  FTL_PAYLOAD_TYPE_PING          = 250;
    static constexpr size_t              PACKET_BUFFER_SIZE             = 128;
    static constexpr size_t              MAX_PACKETS_BEFORE_NACK        = 16;
    static constexpr size_t              NACK_TIMEOUT_SEQUENCE_DELTA    = 128;
    static constexpr uint32_t            ROLLING_SIZE_AVERAGE_MS        = 2000;
    static constexpr std::chrono::milliseconds READ_TIMEOUT{200};

    /* Private members */
    const std::unique_ptr<ConnectionTransport> transport;
    const MediaMetadata mediaMetadata;
    const ftl_channel_id_t channelId;
    const ftl_stream_id_t streamId;
    const ClosedCallback onClosed;
    const RtpPacketCallback onRtpPacket;
    // Stream data
    std::shared_mutex dataMutex;
    time_t startTime { 0 };
    std::chrono::time_point<std::chrono::steady_clock> steadyStartTime;
    std::unordered_map<rtp_ssrc_t, SsrcData> ssrcData;
    // Thread to read and process packets from the connection, must be initialized last
    std::jthread thread;

    /* Private methods */
    void threadBody(std::stop_token stopToken);
    void onBytesReceived(const std::vector<std::byte>& bytes);
    // Packet processing
    std::set<rtp_sequence_num_t> insertPacketInSequenceOrder(
        std::list<std::vector<std::byte>>& packetList, const std::vector<std::byte>& packet);
    void processRtpPacket(const std::vector<std::byte>& rtpPacket);
    void processRtpPacketSequencing(const std::vector<std::byte>& rtpPacket,
        const std::unique_lock<std::shared_mutex>& dataLock);
    void processRtpPacketKeyframe(const std::vector<std::byte>& rtpPacket,
        const std::unique_lock<std::shared_mutex>& dataLock);
    void processRtpH264PacketKeyframe(const std::vector<std::byte>& rtpPacket,
        const std::unique_lock<std::shared_mutex>& dataLock);
    bool isSequenceNewer(rtp_sequence_num_t newSeq, rtp_sequence_num_t oldSeq,
        size_t margin = PACKET_BUFFER_SIZE);
    void processNacks(const rtp_ssrc_t ssrc, const std::unique_lock<std::shared_mutex>& dataLock);
    void sendNack(const rtp_ssrc_t ssrc, const rtp_sequence_num_t packetId,
        const uint16_t followingLostPacketsBitmask,
        const std::unique_lock<std::shared_mutex>& dataLock);
    void processAudioVideoRtpPacket(const std::vector<std::byte>& rtpPacket,
        std::unique_lock<std::shared_mutex>& dataLock);
    void handlePing(const std::vector<std::byte>& rtpPacket);
    void handleSenderReport(const std::vector<std::byte>& rtpPacket);
};