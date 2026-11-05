// FLTK on Windows accepts sockets, not anonymous pipes. Timers exist only while
// a job is active; idle windows never poll. Linux retains pipe notifications.
#ifdef _WIN32
struct JobWatch {
    bc_job *job;
    void (*callback)(int, void *);
    void *data;
};

std::map<bc_job *, std::unique_ptr<JobWatch>> job_watches;

void tick_job(void *data) {
    auto *watch = static_cast<JobWatch *>(data);
    bc_job *key = watch->job;
    watch->callback(-1, watch->data);
    auto it = job_watches.find(key);
    if (it != job_watches.end())
        Fl::repeat_timeout(0.02, tick_job, it->second.get());
}

void watch_job(bc_job *job, void (*callback)(int, void *), void *data) {
    auto watch = std::make_unique<JobWatch>();
    watch->job = job;
    watch->callback = callback;
    watch->data = data;
    Fl::add_timeout(0.02, tick_job, watch.get());
    job_watches[job] = std::move(watch);
}

void unwatch_job(bc_job *job) {
    auto it = job_watches.find(job);
    if (it != job_watches.end()) {
        Fl::remove_timeout(tick_job, it->second.get());
        job_watches.erase(it);
    }
}
#else
void watch_job(bc_job *job, void (*callback)(int, void *), void *data) {
    Fl::add_fd(bc_job_notification_fd(job), FL_READ, callback, data);
}

void unwatch_job(bc_job *job) {
    Fl::remove_fd(bc_job_notification_fd(job));
}
#endif
