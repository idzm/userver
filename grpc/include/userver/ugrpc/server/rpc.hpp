#pragma once

/// @file userver/ugrpc/server/rpc.hpp
/// @brief Classes representing an incoming RPC

#include <google/protobuf/message.h>
#include <grpcpp/impl/codegen/proto_utils.h>
#include <grpcpp/server_context.h>

#include <userver/utils/assert.hpp>

#include <userver/ugrpc/impl/deadline_timepoint.hpp>
#include <userver/ugrpc/impl/internal_tag_fwd.hpp>
#include <userver/ugrpc/impl/span.hpp>
#include <userver/ugrpc/impl/statistics_scope.hpp>
#include <userver/ugrpc/server/exceptions.hpp>
#include <userver/ugrpc/server/impl/async_methods.hpp>
#include <userver/ugrpc/server/impl/call_params.hpp>
#include <userver/ugrpc/server/middlewares/fwd.hpp>

USERVER_NAMESPACE_BEGIN

namespace ugrpc::server {

namespace impl {

std::string FormatLogMessage(
    const std::multimap<grpc::string_ref, grpc::string_ref>& metadata,
    std::string_view peer, std::chrono::system_clock::time_point start_time,
    std::string_view call_name, grpc::StatusCode code);

}

/// @brief RPCs kinds
enum class CallKind {
  kUnaryCall,
  kRequestStream,
  kResponseStream,
  kBidirectionalStream,
};

/// @brief A non-typed base class for any gRPC call
class CallAnyBase {
 public:
  /// @brief Complete the RPC with an error
  ///
  /// `Finish` must not be called multiple times for the same RPC.
  ///
  /// @param status error details
  /// @throws ugrpc::server::RpcError on an RPC error
  /// @see @ref IsFinished
  virtual void FinishWithError(const grpc::Status& status) = 0;

  /// @returns the `ServerContext` used for this RPC
  /// @note Initial server metadata is not currently supported
  /// @note Trailing metadata, if any, must be set before the `Finish` call
  grpc::ServerContext& GetContext() { return params_.context; }

  /// @brief Name of the RPC in the format `full.path.ServiceName/MethodName`
  std::string_view GetCallName() const { return params_.call_name; }

  /// @brief Get name of gRPC service
  std::string_view GetServiceName() const;

  /// @brief Get name of called gRPC method
  std::string_view GetMethodName() const;

  /// @brief Get the span of the current RPC. Span's lifetime covers the
  /// `Handle` call of the outermost @ref MiddlewareBase "middleware".
  tracing::Span& GetSpan() { return params_.call_span; }

  /// @brief Get RPCs kind of method
  CallKind GetCallKind() const { return call_kind_; }

  /// @brief Returns call context for storing per-call custom data
  ///
  /// The context can be used to pass data from server middleware to client
  /// handler or from one middleware to another one.
  ///
  /// ## Example usage:
  ///
  /// In authentication middleware:
  ///
  /// @code
  /// if (password_is_correct) {
  ///   // Username is authenticated, set it in per-call storage context
  ///   ctx.GetCall().GetStorageContext().Emplace(kAuthUsername, username);
  /// }
  /// @endcode
  ///
  /// In client handler:
  ///
  /// @code
  /// const auto& username = rpc.GetStorageContext().Get(kAuthUsername);
  /// auto msg = fmt::format("Hello, {}!", username);
  /// @endcode
  utils::AnyStorage<StorageContext>& GetStorageContext() {
    return params_.storage_context;
  }

  /// @brief Useful for generic error reporting via @ref FinishWithError
  virtual bool IsFinished() const = 0;

  /// @brief Set a custom call name for metric labels
  void SetMetricsCallName(std::string_view call_name);

  /// @cond
  // For internal use only
  CallAnyBase(utils::impl::InternalTag, impl::CallParams&& params,
              CallKind call_kind)
      : params_(std::move(params)), call_kind_(call_kind) {}

  // For internal use only
  ugrpc::impl::RpcStatisticsScope& GetStatistics(ugrpc::impl::InternalTag);

  // For internal use only
  void RunMiddlewarePipeline(utils::impl::InternalTag,
                             MiddlewareCallContext& md_call_context);
  /// @endcond

 protected:
  ugrpc::impl::RpcStatisticsScope& GetStatistics() {
    return params_.statistics;
  }

  logging::LoggerRef AccessTskvLogger() { return params_.access_tskv_logger; }

  void LogFinish(grpc::Status status) const;

  void ApplyRequestHook(google::protobuf::Message* request);

  void ApplyResponseHook(google::protobuf::Message* response);

 private:
  impl::CallParams params_;
  CallKind call_kind_;
  MiddlewareCallContext* middleware_call_context_{nullptr};
};

/// @brief Controls a single request -> single response RPC
///
/// The RPC is cancelled on destruction unless `Finish` has been called.
template <typename Response>
class UnaryCall final : public CallAnyBase {
 public:
  /// @brief Complete the RPC successfully
  ///
  /// `Finish` must not be called multiple times for the same RPC.
  ///
  /// @param response the single Response to send to the client
  /// @throws ugrpc::server::RpcError on an RPC error
  void Finish(Response& response);

  /// @brief Complete the RPC successfully
  ///
  /// `Finish` must not be called multiple times for the same RPC.
  ///
  /// @param response the single Response to send to the client
  /// @throws ugrpc::server::RpcError on an RPC error
  void Finish(Response&& response);

  /// @brief Complete the RPC with an error
  ///
  /// `Finish` must not be called multiple times for the same RPC.
  ///
  /// @param status error details
  /// @throws ugrpc::server::RpcError on an RPC error
  void FinishWithError(const grpc::Status& status) override;

  /// For internal use only
  UnaryCall(impl::CallParams&& call_params,
            impl::RawResponseWriter<Response>& stream);

  UnaryCall(UnaryCall&&) = delete;
  UnaryCall& operator=(UnaryCall&&) = delete;
  ~UnaryCall();

  bool IsFinished() const override;

 private:
  impl::RawResponseWriter<Response>& stream_;
  bool is_finished_{false};
};

/// @brief Controls a request stream -> single response RPC
///
/// This class is not thread-safe except for `GetContext`.
///
/// The RPC is cancelled on destruction unless the stream has been finished.
///
/// If any method throws, further methods must not be called on the same stream,
/// except for `GetContext`.
template <typename Request, typename Response>
class InputStream final : public CallAnyBase {
 public:
  /// @brief Await and read the next incoming message
  /// @param request where to put the request on success
  /// @returns `true` on success, `false` on end-of-input
  [[nodiscard]] bool Read(Request& request);

  /// @brief Complete the RPC successfully
  ///
  /// `Finish` must not be called multiple times for the same RPC.
  ///
  /// @param response the single Response to send to the client
  /// @throws ugrpc::server::RpcError on an RPC error
  void Finish(Response& response);

  /// @brief Complete the RPC successfully
  ///
  /// `Finish` must not be called multiple times for the same RPC.
  ///
  /// @param response the single Response to send to the client
  /// @throws ugrpc::server::RpcError on an RPC error
  void Finish(Response&& response);

  /// @brief Complete the RPC with an error
  ///
  /// `Finish` must not be called multiple times for the same RPC.
  ///
  /// @param status error details
  /// @throws ugrpc::server::RpcError on an RPC error
  void FinishWithError(const grpc::Status& status) override;

  /// For internal use only
  InputStream(impl::CallParams&& call_params,
              impl::RawReader<Request, Response>& stream);

  InputStream(InputStream&&) = delete;
  InputStream& operator=(InputStream&&) = delete;
  ~InputStream();

  bool IsFinished() const override;

 private:
  enum class State { kOpen, kReadsDone, kFinished };

  impl::RawReader<Request, Response>& stream_;
  State state_{State::kOpen};
};

/// @brief Controls a single request -> response stream RPC
///
/// This class is not thread-safe except for `GetContext`.
///
/// The RPC is cancelled on destruction unless the stream has been finished.
///
/// If any method throws, further methods must not be called on the same stream,
/// except for `GetContext`.
template <typename Response>
class OutputStream final : public CallAnyBase {
 public:
  /// @brief Write the next outgoing message
  /// @param response the next message to write
  /// @throws ugrpc::server::RpcError on an RPC error
  void Write(Response& response);

  /// @brief Write the next outgoing message
  /// @param response the next message to write
  /// @throws ugrpc::server::RpcError on an RPC error
  void Write(Response&& response);

  /// @brief Complete the RPC successfully
  ///
  /// `Finish` must not be called multiple times.
  ///
  /// @throws ugrpc::server::RpcError on an RPC error
  void Finish();

  /// @brief Complete the RPC with an error
  ///
  /// `Finish` must not be called multiple times.
  ///
  /// @param status error details
  /// @throws ugrpc::server::RpcError on an RPC error
  void FinishWithError(const grpc::Status& status) override;

  /// @brief Equivalent to `Write + Finish`
  ///
  /// This call saves one round-trip, compared to separate `Write` and `Finish`.
  ///
  /// `Finish` must not be called multiple times.
  ///
  /// @param response the final response message
  /// @throws ugrpc::server::RpcError on an RPC error
  void WriteAndFinish(Response& response);

  /// @brief Equivalent to `Write + Finish`
  ///
  /// This call saves one round-trip, compared to separate `Write` and `Finish`.
  ///
  /// `Finish` must not be called multiple times.
  ///
  /// @param response the final response message
  /// @throws ugrpc::server::RpcError on an RPC error
  void WriteAndFinish(Response&& response);

  /// For internal use only
  OutputStream(impl::CallParams&& call_params,
               impl::RawWriter<Response>& stream);

  OutputStream(OutputStream&&) = delete;
  OutputStream& operator=(OutputStream&&) = delete;
  ~OutputStream();

  bool IsFinished() const override;

 private:
  enum class State { kNew, kOpen, kFinished };

  impl::RawWriter<Response>& stream_;
  State state_{State::kNew};
};

/// @brief Controls a request stream -> response stream RPC
///
/// This class allows the following concurrent calls:
///
///   - `GetContext`
///   - `Read`;
///   - one of (`Write`, `Finish`, `FinishWithError`, `WriteAndFinish`).
///
/// The RPC is cancelled on destruction unless the stream has been finished.
///
/// If any method throws, further methods must not be called on the same stream,
/// except for `GetContext`.
template <typename Request, typename Response>
class BidirectionalStream : public CallAnyBase {
 public:
  /// @brief Await and read the next incoming message
  /// @param request where to put the request on success
  /// @returns `true` on success, `false` on end-of-input
  /// @throws ugrpc::server::RpcError on an RPC error
  [[nodiscard]] bool Read(Request& request);

  /// @brief Write the next outgoing message
  /// @param response the next message to write
  /// @throws ugrpc::server::RpcError on an RPC error
  void Write(Response& response);

  /// @brief Write the next outgoing message
  /// @param response the next message to write
  /// @throws ugrpc::server::RpcError on an RPC error
  void Write(Response&& response);

  /// @brief Complete the RPC successfully
  ///
  /// `Finish` must not be called multiple times.
  ///
  /// @throws ugrpc::server::RpcError on an RPC error
  void Finish();

  /// @brief Complete the RPC with an error
  ///
  /// `Finish` must not be called multiple times.
  ///
  /// @param status error details
  /// @throws ugrpc::server::RpcError on an RPC error
  void FinishWithError(const grpc::Status& status) override;

  /// @brief Equivalent to `Write + Finish`
  ///
  /// This call saves one round-trip, compared to separate `Write` and `Finish`.
  ///
  /// `Finish` must not be called multiple times.
  ///
  /// @param response the final response message
  /// @throws ugrpc::server::RpcError on an RPC error
  void WriteAndFinish(Response& response);

  /// @brief Equivalent to `Write + Finish`
  ///
  /// This call saves one round-trip, compared to separate `Write` and `Finish`.
  ///
  /// `Finish` must not be called multiple times.
  ///
  /// @param response the final response message
  /// @throws ugrpc::server::RpcError on an RPC error
  void WriteAndFinish(Response&& response);

  /// For internal use only
  BidirectionalStream(impl::CallParams&& call_params,
                      impl::RawReaderWriter<Request, Response>& stream);

  BidirectionalStream(const BidirectionalStream&) = delete;
  BidirectionalStream(BidirectionalStream&&) = delete;
  ~BidirectionalStream();

  bool IsFinished() const override;

 private:
  impl::RawReaderWriter<Request, Response>& stream_;
  bool are_reads_done_{false};
  bool is_finished_{false};
};

// ========================== Implementation follows ==========================

template <typename Response>
UnaryCall<Response>::UnaryCall(impl::CallParams&& call_params,
                               impl::RawResponseWriter<Response>& stream)
    : CallAnyBase(utils::impl::InternalTag{}, std::move(call_params),
                  CallKind::kUnaryCall),
      stream_(stream) {}

template <typename Response>
UnaryCall<Response>::~UnaryCall() {
  if (!is_finished_) {
    impl::CancelWithError(stream_, GetCallName());
    LogFinish(impl::kUnknownErrorStatus);
  }
}

template <typename Response>
void UnaryCall<Response>::Finish(Response&& response) {
  Finish(response);
}

template <typename Response>
void UnaryCall<Response>::Finish(Response& response) {
  UINVARIANT(!is_finished_, "'Finish' called on a finished call");
  is_finished_ = true;

  ApplyResponseHook(&response);

  LogFinish(grpc::Status::OK);
  impl::Finish(stream_, response, grpc::Status::OK, GetCallName());
  GetStatistics().OnExplicitFinish(grpc::StatusCode::OK);
  ugrpc::impl::UpdateSpanWithStatus(GetSpan(), grpc::Status::OK);
}

template <typename Response>
void UnaryCall<Response>::FinishWithError(const grpc::Status& status) {
  if (IsFinished()) return;
  is_finished_ = true;
  LogFinish(status);
  impl::FinishWithError(stream_, status, GetCallName());
  GetStatistics().OnExplicitFinish(status.error_code());
  ugrpc::impl::UpdateSpanWithStatus(GetSpan(), status);
}

template <typename Response>
bool UnaryCall<Response>::IsFinished() const {
  return is_finished_;
}

template <typename Request, typename Response>
InputStream<Request, Response>::InputStream(
    impl::CallParams&& call_params, impl::RawReader<Request, Response>& stream)
    : CallAnyBase(utils::impl::InternalTag{}, std::move(call_params),
                  CallKind::kRequestStream),
      stream_(stream) {}

template <typename Request, typename Response>
InputStream<Request, Response>::~InputStream() {
  if (state_ != State::kFinished) {
    impl::CancelWithError(stream_, GetCallName());
    LogFinish(impl::kUnknownErrorStatus);
  }
}

template <typename Request, typename Response>
bool InputStream<Request, Response>::Read(Request& request) {
  UINVARIANT(state_ == State::kOpen,
             "'Read' called while the stream is half-closed for reads");
  if (impl::Read(stream_, request)) {
    ApplyRequestHook(&request);
    return true;
  } else {
    state_ = State::kReadsDone;
    return false;
  }
}

template <typename Request, typename Response>
void InputStream<Request, Response>::Finish(Response&& response) {
  Finish(response);
}

template <typename Request, typename Response>
void InputStream<Request, Response>::Finish(Response& response) {
  UINVARIANT(state_ != State::kFinished,
             "'Finish' called on a finished stream");
  state_ = State::kFinished;

  const auto& status = grpc::Status::OK;
  LogFinish(status);

  ApplyResponseHook(&response);

  impl::Finish(stream_, response, status, GetCallName());
  GetStatistics().OnExplicitFinish(status.error_code());
  ugrpc::impl::UpdateSpanWithStatus(GetSpan(), status);
}

template <typename Request, typename Response>
void InputStream<Request, Response>::FinishWithError(
    const grpc::Status& status) {
  UASSERT(!status.ok());
  if (IsFinished()) return;
  state_ = State::kFinished;
  LogFinish(status);
  impl::FinishWithError(stream_, status, GetCallName());
  GetStatistics().OnExplicitFinish(status.error_code());
  ugrpc::impl::UpdateSpanWithStatus(GetSpan(), status);
}

template <typename Request, typename Response>
bool InputStream<Request, Response>::IsFinished() const {
  return state_ == State::kFinished;
}

template <typename Response>
OutputStream<Response>::OutputStream(impl::CallParams&& call_params,
                                     impl::RawWriter<Response>& stream)
    : CallAnyBase(utils::impl::InternalTag{}, std::move(call_params),
                  CallKind::kResponseStream),
      stream_(stream) {}

template <typename Response>
OutputStream<Response>::~OutputStream() {
  if (state_ != State::kFinished) {
    impl::Cancel(stream_, GetCallName());
    LogFinish(impl::kUnknownErrorStatus);
  }
}

template <typename Response>
void OutputStream<Response>::Write(Response&& response) {
  Write(response);
}

template <typename Response>
void OutputStream<Response>::Write(Response& response) {
  UINVARIANT(state_ != State::kFinished, "'Write' called on a finished stream");

  // For some reason, gRPC requires explicit 'SendInitialMetadata' in output
  // streams
  impl::SendInitialMetadataIfNew(stream_, GetCallName(), state_);

  // Don't buffer writes, otherwise in an event subscription scenario, events
  // may never actually be delivered
  grpc::WriteOptions write_options{};

  ApplyResponseHook(&response);

  impl::Write(stream_, response, write_options, GetCallName());
}

template <typename Response>
void OutputStream<Response>::Finish() {
  UINVARIANT(state_ != State::kFinished,
             "'Finish' called on a finished stream");
  state_ = State::kFinished;

  const auto& status = grpc::Status::OK;
  LogFinish(status);
  impl::Finish(stream_, status, GetCallName());
  GetStatistics().OnExplicitFinish(grpc::StatusCode::OK);
  ugrpc::impl::UpdateSpanWithStatus(GetSpan(), status);
}

template <typename Response>
void OutputStream<Response>::FinishWithError(const grpc::Status& status) {
  UASSERT(!status.ok());
  if (IsFinished()) return;
  state_ = State::kFinished;
  LogFinish(status);
  impl::Finish(stream_, status, GetCallName());
  GetStatistics().OnExplicitFinish(status.error_code());
  ugrpc::impl::UpdateSpanWithStatus(GetSpan(), status);
}

template <typename Response>
void OutputStream<Response>::WriteAndFinish(Response&& response) {
  WriteAndFinish(response);
}

template <typename Response>
void OutputStream<Response>::WriteAndFinish(Response& response) {
  UINVARIANT(state_ != State::kFinished,
             "'WriteAndFinish' called on a finished stream");
  state_ = State::kFinished;

  // Don't buffer writes, otherwise in an event subscription scenario, events
  // may never actually be delivered
  grpc::WriteOptions write_options{};

  const auto& status = grpc::Status::OK;
  LogFinish(status);

  ApplyResponseHook(&response);

  impl::WriteAndFinish(stream_, response, write_options, status, GetCallName());
  GetStatistics().OnExplicitFinish(grpc::StatusCode::OK);
  ugrpc::impl::UpdateSpanWithStatus(GetSpan(), status);
}

template <typename Response>
bool OutputStream<Response>::IsFinished() const {
  return state_ == State::kFinished;
}

template <typename Request, typename Response>
BidirectionalStream<Request, Response>::BidirectionalStream(
    impl::CallParams&& call_params,
    impl::RawReaderWriter<Request, Response>& stream)
    : CallAnyBase(utils::impl::InternalTag{}, std::move(call_params),
                  CallKind::kBidirectionalStream),
      stream_(stream) {}

template <typename Request, typename Response>
BidirectionalStream<Request, Response>::~BidirectionalStream() {
  if (!is_finished_) {
    impl::Cancel(stream_, GetCallName());
    LogFinish(impl::kUnknownErrorStatus);
  }
}

template <typename Request, typename Response>
bool BidirectionalStream<Request, Response>::Read(Request& request) {
  UINVARIANT(!are_reads_done_,
             "'Read' called while the stream is half-closed for reads");
  if (impl::Read(stream_, request)) {
    if constexpr (std::is_base_of_v<google::protobuf::Message, Request>) {
      ApplyRequestHook(&request);
    }
    return true;
  } else {
    are_reads_done_ = true;
    return false;
  }
}

template <typename Request, typename Response>
void BidirectionalStream<Request, Response>::Write(Response&& response) {
  Write(response);
}

template <typename Request, typename Response>
void BidirectionalStream<Request, Response>::Write(Response& response) {
  UINVARIANT(!is_finished_, "'Write' called on a finished stream");

  // Don't buffer writes, optimize for ping-pong-style interaction
  grpc::WriteOptions write_options{};

  if constexpr (std::is_base_of_v<google::protobuf::Message, Response>) {
    ApplyResponseHook(&response);
  }

  try {
    impl::Write(stream_, response, write_options, GetCallName());
  } catch (const RpcInterruptedError&) {
    is_finished_ = true;
    throw;
  }
}

template <typename Request, typename Response>
void BidirectionalStream<Request, Response>::Finish() {
  UINVARIANT(!is_finished_, "'Finish' called on a finished stream");
  is_finished_ = true;

  const auto& status = grpc::Status::OK;
  LogFinish(status);
  impl::Finish(stream_, status, GetCallName());
  GetStatistics().OnExplicitFinish(grpc::StatusCode::OK);
  ugrpc::impl::UpdateSpanWithStatus(GetSpan(), status);
}

template <typename Request, typename Response>
void BidirectionalStream<Request, Response>::FinishWithError(
    const grpc::Status& status) {
  UASSERT(!status.ok());
  if (IsFinished()) return;
  is_finished_ = true;
  LogFinish(status);
  impl::Finish(stream_, status, GetCallName());
  GetStatistics().OnExplicitFinish(status.error_code());
  ugrpc::impl::UpdateSpanWithStatus(GetSpan(), status);
}

template <typename Request, typename Response>
void BidirectionalStream<Request, Response>::WriteAndFinish(
    Response&& response) {
  WriteAndFinish(response);
}

template <typename Request, typename Response>
void BidirectionalStream<Request, Response>::WriteAndFinish(
    Response& response) {
  UINVARIANT(!is_finished_, "'WriteAndFinish' called on a finished stream");
  is_finished_ = true;

  // Don't buffer writes, optimize for ping-pong-style interaction
  grpc::WriteOptions write_options{};

  const auto& status = grpc::Status::OK;
  LogFinish(status);

  if constexpr (std::is_base_of_v<google::protobuf::Message, Response>) {
    ApplyResponseHook(&response);
  }

  impl::WriteAndFinish(stream_, response, write_options, status, GetCallName());
  GetStatistics().OnExplicitFinish(status.error_code());
  ugrpc::impl::UpdateSpanWithStatus(GetSpan(), status);
}

template <typename Request, typename Response>
bool BidirectionalStream<Request, Response>::IsFinished() const {
  return is_finished_;
}

}  // namespace ugrpc::server

USERVER_NAMESPACE_END
