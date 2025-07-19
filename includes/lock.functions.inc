<?php

/**
 * @file
 * Lock implementation functions for the APC Storage module.
 */

declare(strict_types=1);

use apc_storage\lock\Lock;

/**
 * Initializes the locking system.
 */
function lock_initialize(): void {
  if (!class_exists(Lock::class)) {
    require_once __DIR__ . '/lock.class.inc';
  }

  // Register a shutdown function to release all locks.
  backdrop_register_shutdown_function('lock_release_all');
}

/**
 * Acquires a lock. It does not wait for the lock to be available.
 *
 * @param string $name
 *   The lock name.
 * @param float $timeout
 *   The number of seconds before the lock expires. The minimum timeout is 1.
 *
 * @return bool
 *   TRUE if the lock was acquired, FALSE otherwise.
 *
 * @see Lock::acquire()
 */
function lock_acquire(string $name, float $timeout = 30.0): bool {
  return Lock::get($name)->acquire($timeout);
}

/**
 * Checks if a lock acquired by a different process is available.
 *
 * If an existing lock has expired, it is removed.
 *
 * @param string $name
 *   The name of the lock.
 *
 * @return bool
 *   TRUE if there is no lock, or it was removed, FALSE otherwise.
 *
 * @see Lock::maybeAvailable()
 */
function lock_may_be_available(string $name): bool {
  return Lock::get($name)->maybeAvailable();
}

/**
 * Waits for a lock to be available.
 *
 * This function may be called in a request that fails to acquire a desired
 * lock. This will block further execution until the lock is available or the
 * specified delay in seconds is reached. This should not be used with locks
 * that are acquired very frequently, since the lock is likely to be acquired
 * again by a different request while waiting.
 *
 * @param string $name
 *   The name of the lock.
 * @param float $delay
 *   The maximum number of seconds to wait, as an integer.
 *
 * @return bool
 *   FALSE if the lock is available, TRUE otherwise.
 *
 * @see Lock::wait()
 */
function lock_wait(string $name, float $delay = 30.0): bool {
  return Lock::get($name)->wait($delay);
}

/**
 * Releases a lock previously acquired by lock_acquire().
 *
 * This will release the named lock if it is still held by the current request.
 *
 * @param string $name
 *   The name of the lock.
 *
 * @see Lock::release()
 */
function lock_release(string $name): void {
  Lock::get($name)->release();
}
