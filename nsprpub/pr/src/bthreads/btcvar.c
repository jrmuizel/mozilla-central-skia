/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* 
 * The contents of this file are subject to the Mozilla Public
 * License Version 1.1 (the "License"); you may not use this file
 * except in compliance with the License. You may obtain a copy of
 * the License at http://www.mozilla.org/MPL/
 * 
 * Software distributed under the License is distributed on an "AS
 * IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or
 * implied. See the License for the specific language governing
 * rights and limitations under the License.
 * 
 * The Original Code is the Netscape security libraries.
 * 
 * The Initial Developer of the Original Code is Netscape
 * Communications Corporation.  Portions created by Netscape are 
 * Copyright (C) 1998-2000 Netscape Communications Corporation.  All
 * Rights Reserved.
 * 
 * Contributor(s):
 * 
 * Alternatively, the contents of this file may be used under the
 * terms of the GNU General Public License Version 2 or later (the
 * "GPL"), in which case the provisions of the GPL are applicable 
 * instead of those above.  If you wish to allow use of your 
 * version of this file only under the terms of the GPL and not to
 * allow others to use your version of this file under the MPL,
 * indicate your decision by deleting the provisions above and
 * replace them with the notice and other provisions required by
 * the GPL.  If you do not delete the provisions above, a recipient
 * may use your version of this file under either the MPL or the
 * GPL.
 */

#include <kernel/OS.h>

#include "primpl.h"

/*
** Create a new condition variable.
**
** 	"lock" is the lock used to protect the condition variable.
**
** Condition variables are synchronization objects that threads can use
** to wait for some condition to occur.
**
** This may fail if memory is tight or if some operating system resource
** is low. In such cases, a NULL will be returned.
*/
PR_IMPLEMENT(PRCondVar*)
    PR_NewCondVar (PRLock *lock)
{
    PRCondVar *cv = PR_NEW( PRCondVar );
    PR_ASSERT( NULL != lock );
    if( NULL != cv )
    {
	cv->lock = lock;
	cv->isem = create_sem( 1, "nspr_sem");
	PR_ASSERT( cv->isem >= B_NO_ERROR );
    }
    return cv;
} /* PR_NewCondVar */

/*
** Destroy a condition variable. There must be no thread
** waiting on the condvar. The caller is responsible for guaranteeing
** that the condvar is no longer in use.
**
*/
PR_IMPLEMENT(void)
    PR_DestroyCondVar (PRCondVar *cvar)
{
    status_t result;

    result = delete_sem( cvar->isem );
    PR_ASSERT( result == B_NO_ERROR );
    PR_DELETE( cvar );
}

/*
** The thread that waits on a condition is blocked in a "waiting on
** condition" state until another thread notifies the condition or a
** caller specified amount of time expires. The lock associated with
** the condition variable will be released, which must have be held
** prior to the call to wait.
**
** Logically a notified thread is moved from the "waiting on condition"
** state and made "ready." When scheduled, it will attempt to reacquire
** the lock that it held when wait was called.
**
** The timeout has two well known values, PR_INTERVAL_NO_TIMEOUT and
** PR_INTERVAL_NO_WAIT. The former value requires that a condition be
** notified (or the thread interrupted) before it will resume from the
** wait. If the timeout has a value of PR_INTERVAL_NO_WAIT, the effect
** is to release the lock, possibly causing a rescheduling within the
** runtime, then immediately attempting to reacquire the lock and resume.
**
** Any other value for timeout will cause the thread to be rescheduled
** either due to explicit notification or an expired interval. The latter
** must be determined by treating time as one part of the monitored data
** being protected by the lock and tested explicitly for an expired
** interval.
**
** Returns PR_FAILURE if the caller has not locked the lock associated
** with the condition variable or the thread was interrupted (PR_Interrupt()).
** The particular reason can be extracted with PR_GetError().
*/
PR_IMPLEMENT(PRStatus)
    PR_WaitCondVar (PRCondVar *cvar, PRIntervalTime timeout)
{
    status_t result;
    bigtime_t interval;

    PR_Unlock( cvar->lock );

    switch (timeout) {
    case PR_INTERVAL_NO_WAIT:
        /* nothing to do */
        break;

    case PR_INTERVAL_NO_TIMEOUT:
        /* wait as long as necessary */
        if( acquire_sem( cvar->isem ) != B_NO_ERROR ) return PR_FAILURE;
        break;

    default:
        interval = (bigtime_t)PR_IntervalToMicroseconds(timeout);

        /*
        ** in R5, this problem seems to have been resolved, so we
        ** won't bother with it
        */
#if !defined(B_BEOS_VERSION_5) || (B_BEOS_VERSION < B_BEOS_VERSION_5)
        /*
        ** This is an entirely stupid bug, but...  If you call
        ** acquire_sem_etc with a timeout of exactly 1,000,000 microseconds
        ** it returns immediately with B_NO_ERROR.  1,000,010 microseconds
        ** returns as expected.  Running BeOS/Intel R3.1 at this time.
        ** Forwarded to Be, Inc. for resolution, Bug ID 980624-225956
        **
        ** Update: Be couldn't reproduce it, but removing timeout++ still
        **         exhibits the problem on BeOS/Intel R4 and BeOS/PPC R4.
        */
        if (interval == 1000000)
            interval = 1000010;
#endif	/* !defined(B_BEOS_VERSION_5) || (B_BEOS_VERSION < B_BEOS_VERSION_5) */

        result = acquire_sem_etc( cvar->isem, 1, B_RELATIVE_TIMEOUT, interval);
        if( result != B_NO_ERROR && result != B_TIMED_OUT )
            return PR_FAILURE;
        break;
    }

    PR_Lock( cvar->lock );

    return PR_SUCCESS;
}

/*
** Notify ONE thread that is currently waiting on 'cvar'. Which thread is
** dependent on the implementation of the runtime. Common sense would dictate
** that all threads waiting on a single condition have identical semantics,
** therefore which one gets notified is not significant. 
**
** The calling thead must hold the lock that protects the condition, as
** well as the invariants that are tightly bound to the condition, when
** notify is called.
**
** Returns PR_FAILURE if the caller has not locked the lock associated
** with the condition variable.
*/
PR_IMPLEMENT(PRStatus)
    PR_NotifyCondVar (PRCondVar *cvar)
{
    if( release_sem( cvar->isem ) != B_NO_ERROR ) return PR_FAILURE;

    return PR_SUCCESS; 
}

/*
** Notify all of the threads waiting on the condition variable. The order
** that the threads are notified is indeterminant. The lock that protects
** the condition must be held.
**
** Returns PR_FAILURE if the caller has not locked the lock associated
** with the condition variable.
*/
PR_IMPLEMENT(PRStatus)
    PR_NotifyAllCondVar (PRCondVar *cvar)
{
    sem_info semInfo;

    if( get_sem_info( cvar->isem, &semInfo ) != B_NO_ERROR )
	return PR_FAILURE;

    if( release_sem_etc( cvar->isem, semInfo.count, 0 ) != B_NO_ERROR )
	return PR_FAILURE;

    return PR_SUCCESS;
}
