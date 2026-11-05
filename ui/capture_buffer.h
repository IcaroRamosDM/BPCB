#pragma once
#include "bancada.h"
#include <cstring>
#include <deque>
#include <utility>

/* UI-thread owner. Byte limits refer to the serialized event payload, as before.
   Eviction is O(1), with no reserialization or shifting of the remaining events. */
class CaptureBuffer {
    struct Item {
        json_object *value;
        size_t bytes;
    };

    std::deque<Item> events_;
    size_t bytes_ = 0, dropped_ = 0, maximum_events_, maximum_bytes_;

public:
    explicit CaptureBuffer(size_t events = 4096, size_t bytes = 4 * 1024 * 1024)
        : maximum_events_(events), maximum_bytes_(bytes) {
    }

    CaptureBuffer(const CaptureBuffer &) = delete;
    CaptureBuffer &operator=(const CaptureBuffer &) = delete;

    ~CaptureBuffer() {
        clear();
    }

    bool append(json_object *event) {
        if (!event) {
            dropped_++;
            return false;
        }
        size_t n = std::strlen(json_object_to_json_string_ext(event, JSON_C_TO_STRING_PLAIN));
        if (!maximum_events_ || n > maximum_bytes_) {
            dropped_++;
            return false;
        }
        json_object *copy = bc_clone(event);
        if (!copy) {
            dropped_++;
            return false;
        }
        while (!events_.empty() &&
               (events_.size() >= maximum_events_ || n > maximum_bytes_ - bytes_)) {
            bytes_ -= events_.front().bytes;
            json_object_put(events_.front().value);
            events_.pop_front();
            dropped_++;
        }
        try {
            events_.push_back({copy, n});
        } catch (...) {
            json_object_put(copy);
            dropped_++;
            return false;
        }
        bytes_ += n;
        return true;
    }

    size_t size() const {
        return events_.size();
    }

    size_t bytes() const {
        return bytes_;
    }

    size_t dropped() const {
        return dropped_;
    }

    json_object *snapshot() const {
        json_object *array = json_object_new_array();
        for (const auto &item : events_) {
            json_object *copy = bc_clone(item.value);
            if (!copy) {
                json_object_put(array);
                return nullptr;
            }
            json_object_array_add(array, copy);
        }
        return array;
    }

    void clear() {
        for (const auto &item : events_)
            json_object_put(item.value);
        events_.clear();
        bytes_ = dropped_ = 0;
    }
};
