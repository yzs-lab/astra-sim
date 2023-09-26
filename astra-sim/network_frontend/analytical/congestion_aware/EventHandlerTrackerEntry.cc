/******************************************************************************
This source code is licensed under the MIT license found in the
LICENSE file in the root directory of this source tree.
*******************************************************************************/

#include "network_frontend/analytical/congestion_aware/EventHandlerTrackerEntry.hh"

using namespace AstraSimAnalyticalCongestionAware;

EventHandlerTrackerEntry::EventHandlerTrackerEntry() noexcept
    : send_event(std::nullopt),
      recv_event(std::nullopt),
      transmission_finished(false) {}

void EventHandlerTrackerEntry::register_send_callback(
    const Callback callback,
    CallbackArg const arg) noexcept {
  assert(!send_event.has_value());

  const auto event = Event(callback, arg);
  send_event = event;
}

void EventHandlerTrackerEntry::register_recv_callback(
    const Callback callback,
    CallbackArg const arg) noexcept {
  assert(!recv_event.has_value());

  const auto event = Event(callback, arg);
  recv_event = event;
}

bool EventHandlerTrackerEntry::is_transmission_finished() const noexcept {
  return transmission_finished;
}

void EventHandlerTrackerEntry::set_transmission_finished() noexcept {
  transmission_finished = true;
}

bool EventHandlerTrackerEntry::both_callbacks_registered() const noexcept {
  return (send_event.has_value() && recv_event.has_value());
}

void EventHandlerTrackerEntry::invoke_send_handler() noexcept {
  assert(send_event.has_value());

  send_event.value().invoke_event();
}

void EventHandlerTrackerEntry::invoke_recv_handler() noexcept {
  assert(recv_event.has_value());

  recv_event.value().invoke_event();
}
