/******************************************************************************
This source code is licensed under the MIT license found in the
LICENSE file in the root directory of this source tree.
*******************************************************************************/

#include "network_frontend/analytical/congestion_aware/NetworkApi.hh"

using namespace AstraSim;
using namespace AstraSimAnalyticalCongestionAware;
using namespace NetworkAnalytical;
using namespace NetworkAnalyticalCongestionAware;

std::shared_ptr<EventQueue> NetworkApi::event_queue;

std::shared_ptr<Topology> NetworkApi::topology;

ChunkIdGenerator NetworkApi::chunk_id_generator = {};

EventHandlerTracker NetworkApi::event_handler_tracker = {};

EventHandlerTracker& NetworkApi::get_event_handler_tracker() noexcept {
  return event_handler_tracker;
}

void NetworkApi::process_chunk_arrival(void* args) noexcept {
  assert(args != nullptr);

  auto* const data =
      static_cast<std::tuple<int, int, int, uint64_t, int>*>(args);
  const auto [tag, src, dest, count, chunk_id] = *data;

  auto& tracker = NetworkApi::get_event_handler_tracker();
  const auto entry = tracker.search_entry(tag, src, dest, count, chunk_id);
  assert(entry.has_value()); // entry must exist

  if (entry.value()->both_callbacks_registered()) {
    // call both send and recv callback
    entry.value()->invoke_send_handler();
    entry.value()->invoke_recv_handler();

    // remove entry
    tracker.pop_entry(tag, src, dest, count, chunk_id);
  } else {
    // run only send callback, as recv is not ready yet.
    entry.value()->invoke_send_handler();

    // mark the transmission as finished
    entry.value()->set_transmission_finished();
  }
}

void NetworkApi::set_event_queue(
    std::shared_ptr<EventQueue> event_queue_ptr) noexcept {
  assert(event_queue_ptr != nullptr);

  NetworkApi::event_queue = std::move(event_queue_ptr);
}

void NetworkApi::set_topology(std::shared_ptr<Topology> topology_ptr) noexcept {
  assert(topology_ptr != nullptr);

  NetworkApi::topology = std::move(topology_ptr);
}

NetworkApi::NetworkApi(const int rank) noexcept : AstraNetworkAPI(rank) {
  assert(rank >= 0);
}

timespec_t NetworkApi::sim_get_time() {
  const auto current_time = event_queue->get_current_time();
  const auto astra_sim_time = static_cast<double>(current_time);

  return {NS, astra_sim_time};
}

void NetworkApi::sim_schedule(
    const timespec_t delta,
    void (*fun_ptr)(void*),
    void* const fun_arg) {
  assert(delta.time_res == NS);
  assert(fun_ptr != nullptr);

  // calculate event_time
  const auto current_time = sim_get_time();
  const auto event_time = current_time.time_val + delta.time_val;
  const auto event_time_ns = static_cast<EventTime>(event_time);

  // schedule event
  event_queue->schedule_event(event_time_ns, fun_ptr, fun_arg);
}

void NetworkApi::schedule(
    const timespec_t time,
    void (*fun_ptr)(void*),
    void* const fun_arg) {
  assert(time.time_res == NS);
  assert(fun_ptr != nullptr);

  // get event time
  const auto event_time = time.time_val;
  const auto event_time_ns = static_cast<EventTime>(event_time);
  assert(event_time_ns >= event_queue->get_current_time());

  // schedule event
  event_queue->schedule_event(event_time_ns, fun_ptr, fun_arg);
}

int NetworkApi::sim_send(
    void* const buffer,
    const uint64_t count,
    const int type,
    const int dst,
    const int tag,
    sim_request* const request,
    void (*msg_handler)(void*),
    void* const fun_arg) {
  // query chunk id
  const auto src = sim_comm_get_rank();
  const auto chunk_id =
      NetworkApi::chunk_id_generator.get_send_id(tag, src, dst, count);

  // search tracker
  auto entry =
      event_handler_tracker.search_entry(tag, src, dst, count, chunk_id);
  if (entry.has_value()) {
    entry.value()->register_send_callback(msg_handler, fun_arg);
  } else {
    // create new entry and insert callback
    auto* const new_entry =
        event_handler_tracker.create_new_entry(tag, src, dst, count, chunk_id);
    new_entry->register_send_callback(msg_handler, fun_arg);
  }

  // initiate transmission from src -> dst.
  auto chunk_arrival_arg = std::tuple(tag, src, dst, count, chunk_id);
  auto arg = std::make_unique<decltype(chunk_arrival_arg)>(chunk_arrival_arg);
  const auto arg_ptr = static_cast<void*>(arg.release());
  const auto route = topology->route(src, dst);

  auto chunk = std::make_unique<Chunk>(
      count, route, NetworkApi::process_chunk_arrival, arg_ptr);
  topology->send(std::move(chunk));

  return 0;
}

int NetworkApi::sim_recv(
    void* const buffer,
    const uint64_t count,
    const int type,
    const int src,
    const int tag,
    sim_request* const request,
    void (*msg_handler)(void*),
    void* const fun_arg) {
  // query chunk id
  const auto dst = sim_comm_get_rank();
  const auto chunk_id =
      NetworkApi::chunk_id_generator.get_recv_id(tag, src, dst, count);

  // search tracker
  auto entry =
      event_handler_tracker.search_entry(tag, src, dst, count, chunk_id);
  if (entry.has_value()) {
    // send() already invoked
    // behavior is decided whether the transmission is already finished or not
    if (entry.value()->is_transmission_finished()) {
      // pop entry
      event_handler_tracker.pop_entry(tag, src, dst, count, chunk_id);

      // run recv callback immediately
      const auto delta = timespec_t{NS, 0};
      // sim_schedule(delta, msg_handler, fun_arg);
      //  Divya: Changes to port congestion backend to Chakra
      schedule(delta, msg_handler, fun_arg);
    } else {
      // register recv callback
      entry.value()->register_recv_callback(msg_handler, fun_arg);
    }
  } else {
    // send() not yet called
    // create new entry and insert callback
    auto* const new_entry =
        event_handler_tracker.create_new_entry(tag, src, dst, count, chunk_id);
    new_entry->register_recv_callback(msg_handler, fun_arg);
  }

  return 0;
}
