#include "../ui/capture_buffer.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static double time_now() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

static double old_buffer(json_object *event, unsigned count) {
    auto *rows = json_object_new_array();
    size_t bytes = 0;
    double start = time_now();
    for (unsigned i = 0; i < count; i++) {
        json_object_object_add(event, "index", json_object_new_int64(i));
        size_t n = std::strlen(json_object_to_json_string_ext(event, JSON_C_TO_STRING_PLAIN));
        while (json_object_array_length(rows) > 0 &&
               (json_object_array_length(rows) >= 4096 || bytes + n > 4 * 1024 * 1024)) {
            bytes -= std::strlen(json_object_to_json_string_ext(json_object_array_get_idx(rows, 0),
                                                                JSON_C_TO_STRING_PLAIN));
            json_object_array_del_idx(rows, 0, 1);
        }
        bytes += n;
        json_object_array_add(rows, bc_clone(event));
    }
    double result = time_now() - start;
    assert(json_object_array_length(rows) == 4096);
    assert(bc_num(json_object_array_get_idx(rows, 0), "index") == count - 4096);
    json_object_put(rows);
    return result;
}

static double new_buffer(json_object *event, unsigned count) {
    CaptureBuffer buffer;
    double start = time_now();
    for (unsigned i = 0; i < count; i++) {
        json_object_object_add(event, "index", json_object_new_int64(i));
        assert(buffer.append(event));
    }
    double result = time_now() - start;
    assert(buffer.size() == 4096 && buffer.dropped() == count - 4096);
    auto *rows = buffer.snapshot();
    assert(rows && bc_num(json_object_array_get_idx(rows, 0), "index") == count - 4096);
    assert(bc_num(json_object_array_get_idx(rows, 4095), "index") == count - 1);
    json_object_put(rows);
    return result;
}

int main(int argc, char **argv) {
    auto *event = json_tokener_parse(
        "{\"type\":\"communication_terminal_data\",\"direction\":\"rx\",\"hex\":\"4F 4B 0D "
        "0A\",\"ascii\":\"OK\\r\\n\",\"complete\":true,\"elapsed\":1,\"session\":\"fixture\"}");
    assert(event);
    std::string payload(400, 'x');
    json_object_object_add(event, "payload", json_object_new_string(payload.c_str()));
    {
        CaptureBuffer small(2, 4096);
        assert(small.append(event));
        json_object_object_add(event, "index", json_object_new_int(1));
        assert(small.append(event));
        json_object_object_add(event, "index", json_object_new_int(2));
        assert(small.append(event));
        assert(small.size() == 2 && small.dropped() == 1);
        auto *r = small.snapshot();
        assert(bc_num(json_object_array_get_idx(r, 0), "index") == 1);
        json_object_put(r);
        small.clear();
        assert(!small.size() && !small.bytes() && !small.dropped());
    }
    {
        CaptureBuffer bytes(4096, 4);
        assert(!bytes.append(event));
        assert(bytes.size() == 0 && bytes.dropped() == 1);
    }
    if (argc == 2 && !std::strcmp(argv[1], "--benchmark")) {
        std::vector<double> old_times, new_times;
        for (unsigned repeat = 0; repeat < 7; repeat++) {
            old_times.push_back(old_buffer(event, 50000));
            new_times.push_back(new_buffer(event, 50000));
        }
        std::sort(old_times.begin(), old_times.end());
        std::sort(new_times.begin(), new_times.end());
        std::printf("{\"events_per_sample\":50000,\"samples\":7,\"old_median_seconds\":%.9f,\"new_"
                    "median_seconds\":%.9f,\"speedup\":%.6f}\n",
                    old_times[3], new_times[3], old_times[3] / new_times[3]);
    } else {
        new_buffer(event, 10000);
        std::puts("PASS: captura limitada, ordem, descarte contado, snapshot independente e "
                  "limpeza explícita.");
    }
    json_object_put(event);
    return 0;
}
