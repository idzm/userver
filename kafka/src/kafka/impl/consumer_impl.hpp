#pragma once

#include <optional>
#include <vector>

#include <librdkafka/rdkafka.h>

#include <userver/engine/deadline.hpp>
#include <userver/kafka/message.hpp>

#include <kafka/impl/holders.hpp>
#include <kafka/impl/stats.hpp>

USERVER_NAMESPACE_BEGIN

namespace kafka::impl {

class Configuration;
struct TopicStats;

/// @brief Consumer implementation based on `librdkafka`.
/// @warning All methods calls the `librdkafka` functions that very often uses
/// pthread mutexes. Hence, all methods must not be called in main task
/// processor
class ConsumerImpl final {
  using MessageBatch = std::vector<Message>;

 public:
  explicit ConsumerImpl(Configuration&& configuration);

  ~ConsumerImpl();

  /// @brief Schedules the `topics` subscription.
  void Subscribe(const std::vector<std::string>& topics);

  /// @brief Revokes all subscribed topics partitions and leaves the consumer
  /// group.
  /// @note Blocks until consumer successfully closed
  /// @warning Blocks forever if polled messages are not destroyed
  void LeaveGroup();

  /// @brief Closes the consumer and subscribes for the `topics`.
  void Resubscribe(const std::vector<std::string>& topics);

  /// @brief Synchronously commits the current assignment offsets.
  void Commit();

  /// @brief Schedules the commitment task.
  void AsyncCommit();

  /// @brief Polls the message until `deadline` is reached.
  /// If no message polled, returns `std::nullopt`
  /// @note Must be called periodically to maintain consumer group membership
  std::optional<Message> PollMessage(engine::Deadline deadline);

  /// @brief Effectively calls `PollMessage` until `deadline` is reached
  /// and no more than `max_batch_size` messages polled.
  MessageBatch PollBatch(std::size_t max_batch_size, engine::Deadline deadline);

  const Stats& GetStats() const;

  void AccountMessageProcessingSucceeded(const Message& message);
  void AccountMessageBatchProcessingSucceeded(const MessageBatch& batch);
  void AccountMessageProcessingFailed(const Message& message);
  void AccountMessageBatchProcessingFailed(const MessageBatch& batch);

  void ErrorCallback(int error_code, const char* reason);

  /// @brief Callback that is called on each group join/leave and topic
  /// partition update. Used as a dispatcher of rebalance events.
  void RebalanceCallback(rd_kafka_resp_err_t err,
                         rd_kafka_topic_partition_list_s* partitions);

  /// @brief Assigns (subscribes) the `partitions` list to the current
  /// consumer.
  void AssignPartitions(const rd_kafka_topic_partition_list_s* partitions);

  /// @brief Revokes `partitions` from the current consumer.
  void RevokePartitions(const rd_kafka_topic_partition_list_s* partitions);

  /// @brief Callback which is called after succeeded/failed commit.
  /// Currently, used for logging purposes.
  void OffsetCommitCallbackProxy(
      rd_kafka_resp_err_t err,
      rd_kafka_topic_partition_list_s* committed_offsets);

 private:
  std::shared_ptr<TopicStats> GetTopicStats(const std::string& topic);

  void AccountPolledMessageStat(const Message& polled_message);

 private:
  const std::string component_name_;
  Stats stats_;

  ConfHolder conf_;
  std::optional<ConsumerHolder> consumer_;
};

}  // namespace kafka::impl

USERVER_NAMESPACE_END
