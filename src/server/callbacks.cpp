#include "callbacks.h"

#include <stdint.h>

#include <cstddef>
#include <memory>
#include <mutex>
#include <type_traits>

#include "../common/u_generic_callbacks.hpp"

struct ems_callbacks {
    std::mutex mutex;
    GenericCallbacks<ems_callbacks_func_t, enum ems_callbacks_event> callbacks_collection;
};

struct ems_callbacks *ems_callbacks_create() {
    return new ems_callbacks;
}

void ems_callbacks_destroy(struct ems_callbacks **ptr_callbacks) {
    if (!ptr_callbacks) {
        return;
    }
    std::unique_ptr<ems_callbacks> callbacks(*ptr_callbacks);
    // Take the lock to wait for anybody else who might be in the lock.
    std::unique_lock<std::mutex> lock(callbacks->mutex);
    lock.unlock();

    *ptr_callbacks = nullptr;
    callbacks.reset();
}

void ems_callbacks_add(struct ems_callbacks *callbacks,
                       uint32_t event_mask,
                       ems_callbacks_func_t func,
                       void *userdata) {
    std::unique_lock<std::mutex> lock(callbacks->mutex);
    callbacks->callbacks_collection.addCallback(func, event_mask, userdata);
}

void ems_callbacks_reset(struct ems_callbacks *callbacks) {
    std::unique_lock<std::mutex> lock(callbacks->mutex);
    callbacks->callbacks_collection = {};
}

void ems_callbacks_call(struct ems_callbacks *callbacks,
                        enum ems_callbacks_event event,
                        const ProtoUpMessage *message) {
    std::unique_lock<std::mutex> lock(callbacks->mutex);
    auto invoker = [=](enum ems_callbacks_event ev, ems_callbacks_func_t callback, void *userdata) {
        callback(ev, message, userdata);
        return false; // do not remove
    };
    callbacks->callbacks_collection.invokeCallbacks(event, invoker);
}
