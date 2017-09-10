// -*- c-basic-offset: 4; c-backslash-column: 79; indent-tabs-mode: nil -*-
// vim:sw=4 ts=4 sts=4 expandtab
#ifndef REF_H_110324
#define REF_H_110324
#include <assert.h>
#include <junkie/config.h>
#include <junkie/tools/queue.h>
#include <junkie/tools/mutex.h>

/** @file
 * @brief Reference counted objects
 *
 * Some objects are ref-counted since they can be used by several other objects
 * and so their lifespan is unpredictable.
 * But as these objects are used concurrently by several threads running amok,
 * we have to take extra provisions :
 *
 * - first we inc/dec the count atomically (using a mutex in case atomic
 * operations are not readily available)
 *
 * - then we prevent a thread to delete an object which count reaches 0, since
 * this object address may be known by another thread that is about to inc the
 * count.  Refcounted object deletions are thus delayed until a safe point in
 * the program where all threads meet together without any pointer to any
 * refcounted object. As a consequence, it is not impossible for an object
 * count to be raised from 0 to 1.
 *
 * Note: ref counters gives you the assurance that a refed object won't
 * disapear, but does not prevent in any way another thread than yours to
 * modify it concurrently. */

struct ref {
    unsigned count;             ///< The count itself
    /** NOT_IN_DEATH_ROW:
     * entry.sle_next is set to NOT_IN_DEATH_ROW at creation and will be set to a proper value only
     * when the object is queued for deletion. It is then guaranteed that it will never be set to
     * NOT_IN_DEATH_ROW again, except in mono_region, when the object is rescued by doomer_thread.
     * You may be able to make some limited use of this, at your peril.
     *
     * Note: We Cannot use 0 as the magic value since sle_next will be NULL at end of list. */
#   define NOT_IN_DEATH_ROW ((void *)1)
    SLIST_ENTRY(ref) entry;     ///< If already on the (preliminary or definitive) death row, or NOT_IN_DEATH_ROW
#   ifndef __GNUC__
    struct mutex mutex;         ///< In dire circumstances when we can't use atomic operations
#   endif
    void (*del)(struct ref *);  ///< The delete function to finally get rid of the object
};

static inline void ref_ctor(struct ref *ref, void (*del)(struct ref *))
{
    ref->count = 1; // for the caller
    ref->entry.sle_next = NOT_IN_DEATH_ROW;
    ref->del = del;
#   ifndef __GNUC__
    mutex_ctor(&ref->mutex, "ref");
#   endif
}

// Only called from the doomer thread
static inline void ref_dtor(struct ref *ref)
{
    assert(ref->count == 0);
    // We do not remove it from the death_row since delete_doomed does it
#   ifndef __GNUC__
    mutex_dtor(&ref->mutex);
#   endif
}

static inline void *ref(struct ref *ref)
{
    if (! ref) return NULL;

#   ifdef __GNUC__
    (void)__sync_fetch_and_add(&ref->count, 1);
#   else
    mutex_lock(&ref->mutex);
    ref->count ++;
    mutex_unlock(&ref->mutex);
#   endif

    return ref;
}

extern SLIST_HEAD(refs, ref) death_row;
extern struct mutex death_row_mutex;

static inline void unref(struct ref *ref)
{
    if (! ref) return;

#   ifdef __GNUC__
    unsigned const c = __sync_fetch_and_sub(&ref->count, 1);
    assert(c > 0);  // or where did this ref came from ?
    bool const unreachable = c == 1;
#   else
    mutex_lock(&ref->mutex);
    assert(ref->count > 0);
    bool const unreachable = ref->count == 1;
    ref->count --;
    mutex_unlock(&ref->mutex);
#   endif

    if (unreachable) {
        // The thread that downs the count to 0 is responsible for queuing the object onto the death row.
        mutex_lock(&death_row_mutex);
        assert(ref->entry.sle_next == NOT_IN_DEATH_ROW);    // If it was already on the death row, then where did this ref come from?
        SLIST_INSERT_HEAD(&death_row, ref, entry);
        assert(ref->entry.sle_next != NOT_IN_DEATH_ROW);
        mutex_unlock(&death_row_mutex);
    }
}

/// Enter the region where multiple threads can enter
void enter_multi_region(void);

/// Enter the region where only this thread can enter
void enter_mono_region(void);

/// Leave the protected region (ie. all threads allowed)
void leave_protected_region(void);

/// Will stop the doomer_thread (must be called bedore ref_fini(), and probably before any parser_fini()
void doomer_stop(void);

/// Will run the doomer thread to kill all unreachable objects (safe for multithread)
void doomer_run(void);

void ref_init(void);
void ref_fini(void);

#endif
