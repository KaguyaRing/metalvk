// SPDX-License-Identifier: MIT
// Test-only platform adapters; the core never includes OS thread headers.
#ifndef MTVK_TEST_THREAD_SUPPORT_H
#define MTVK_TEST_THREAD_SUPPORT_H
#if defined(NDEBUG)
#error Contract tests require assertions enabled
#endif
#include <assert.h>
#if defined(_WIN32)
#include <windows.h>
typedef CRITICAL_SECTION test_mutex;
typedef HANDLE test_event;
static inline void mutex_init(test_mutex *m) { InitializeCriticalSection(m); }
static inline void mutex_destroy(test_mutex *m) { DeleteCriticalSection(m); }
static inline void mutex_lock(test_mutex *m) { EnterCriticalSection(m); }
static inline void mutex_unlock(test_mutex *m) { LeaveCriticalSection(m); }
static inline void event_init(test_event *e) { *e = CreateEventW(NULL, TRUE, FALSE, NULL); assert(*e); }
static inline void event_destroy(test_event *e) { assert(CloseHandle(*e)); }
static inline void event_signal(test_event *e) { assert(SetEvent(*e)); }
static inline void event_wait(test_event *e) { assert(WaitForSingleObject(*e, 5000) == WAIT_OBJECT_0); }
#else
#include <pthread.h>
#include <time.h>
typedef pthread_mutex_t test_mutex;
typedef struct test_event { pthread_mutex_t mutex; pthread_cond_t condition; int set; } test_event;
static inline void mutex_init(test_mutex *m) { assert(pthread_mutex_init(m, NULL) == 0); }
static inline void mutex_destroy(test_mutex *m) { assert(pthread_mutex_destroy(m) == 0); }
static inline void mutex_lock(test_mutex *m) { assert(pthread_mutex_lock(m) == 0); }
static inline void mutex_unlock(test_mutex *m) { assert(pthread_mutex_unlock(m) == 0); }
static inline void event_init(test_event *e) {
    e->set = 0; assert(pthread_mutex_init(&e->mutex, NULL) == 0); assert(pthread_cond_init(&e->condition, NULL) == 0);
}
static inline void event_destroy(test_event *e) {
    assert(pthread_cond_destroy(&e->condition) == 0); assert(pthread_mutex_destroy(&e->mutex) == 0);
}
static inline void event_signal(test_event *e) {
    assert(pthread_mutex_lock(&e->mutex) == 0); e->set = 1; assert(pthread_cond_broadcast(&e->condition) == 0);
    assert(pthread_mutex_unlock(&e->mutex) == 0);
}
static inline void event_wait(test_event *e) {
    struct timespec deadline; assert(clock_gettime(CLOCK_REALTIME, &deadline) == 0); deadline.tv_sec += 5;
    assert(pthread_mutex_lock(&e->mutex) == 0);
    while (!e->set) assert(pthread_cond_timedwait(&e->condition, &e->mutex, &deadline) == 0);
    assert(pthread_mutex_unlock(&e->mutex) == 0);
}
#endif
typedef struct test_thread {
    void (*work)(void *);
    void *argument;
#if defined(_WIN32)
    HANDLE thread;
#else
    pthread_t thread;
#endif
} test_thread;
#if defined(_WIN32)
static DWORD WINAPI thread_entry(void *argument) {
    test_thread *t = argument; t->work(t->argument); return 0;
}
#else
static void *thread_entry(void *argument) {
    test_thread *t = argument; t->work(t->argument); return NULL;
}
#endif
static inline void thread_start(test_thread *t, void (*work)(void *), void *argument) {
    t->work = work; t->argument = argument;
#if defined(_WIN32)
    t->thread = CreateThread(NULL, 0, thread_entry, t, 0, NULL); assert(t->thread);
#else
    assert(pthread_create(&t->thread, NULL, thread_entry, t) == 0);
#endif
}
static inline void thread_join(test_thread *t) {
#if defined(_WIN32)
    assert(WaitForSingleObject(t->thread, 5000) == WAIT_OBJECT_0); assert(CloseHandle(t->thread));
#else
    assert(pthread_join(t->thread, NULL) == 0);
#endif
}
#endif
