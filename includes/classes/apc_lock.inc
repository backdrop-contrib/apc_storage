<?php

/**
 * @file
 * An APCU-based implementation of a locking mechanism.
 *
 * @see includes/lock.inc
 */

declare(strict_types=1);

/**
 * Class for the APCu-based locking mechanism implementation.
 *
 * @internal
 */
class ApcLock {

  /**
   * The locks acquired in the current request.
   *
   * @var array
   */
  protected static $locks = array();

  /**
   * The unique ID used to generate the APCu keys for locks.
   *
   * @var string
   */
  protected static $uniqueId = '';

  /**
   * Initializes the locking system.
   */
  public static function initialize() {
    self::$uniqueId = drupal_random_bytes(32);
  }

  /**
   * Acquires (or renews) a lock, but it does not block if it fails.
   *
   * @param string $name
   *   The name of the lock.
   * @param float $timeout
   *   The number of seconds before the lock expires. The minimum timeout is 1.
   *
   * @return bool
   *   TRUE if the lock was acquired, FALSE otherwise.
   */
  public static function acquire($name, $timeout = 30.0) {
    // Ensure that the timeout is at least one second. APCu works with integer
    // TTLs where 0 means the value is persistent.
    $timeout = (int) round(max($timeout, 1.0));
    $key = self::getLockKey($name);
    $expire = microtime(TRUE) + $timeout;

    if (isset(self::$locks[$name])) {
      // Try to extend the expiration of a lock we already acquired.
      $success = apcu_store($key, array('expire' => $expire), $timeout);

      if (!$success) {
        // The lock was broken.
        unset(self::$locks[$name]);
        apcu_delete($key);
      }

      return $success;
    }
    else {
      // Try to acquire the lock.
      $retry = FALSE;

      do {
        $success = apcu_store($key, array('expire' => $expire), $timeout);

        if ($success) {
          self::$locks[$name] = TRUE;
        }
        else {
          $retry = !$retry && self::maybeAvailable($name);
        }
      } while ($retry);
    }

    return isset(self::$locks[$name]);
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
   */
  public static function maybeAvailable($name) {
    $key = self::getLockKey($name);

    if (!apcu_exists($key)) {
      return TRUE;
    }

    $lock = apcu_fetch($key, $success);

    if (!$success) {
      return TRUE;
    }

    $expire = (float) $lock['expire'];
    $now = microtime(TRUE);

    if ($now >= $expire) {
      apcu_delete($key);

      return TRUE;
    }

    return FALSE;
  }

  /**
   * Waits for a lock to be available.
   *
   * @param string $name
   *   The name of the lock.
   * @param float $delay
   *   The maximum number of seconds to wait, as an integer.
   *
   * @return bool
   *   FALSE if the lock is available, TRUE otherwise.
   */
  public static function waitForLock($name, $delay = 30.0) {
    // Pause the process for short periods between calling
    // lock_may_be_available(). However, if the wait period is too long, there
    // is the potential for a large number of processes to be blocked waiting
    // for a lock, especially if the item being rebuilt is commonly requested.
    // To address both of these concerns, begin waiting for 25ms, then add 25
    // ms to the wait period each time until it reaches 500 ms. After this
    // point, polling will continue every 500 ms until $delay is reached.
    // $delay is passed in seconds, but we will be using usleep(), which takes
    // microseconds as a parameter. Multiply it by 1 million so that all
    // further numbers are equivalent.
    $delay = (int) round($delay * 1000000);

    // Begin sleeping at 25ms.
    $sleep = 25000;
    while ($delay > 0) {
      // This function should only be called by a request that failed to get a
      // lock, so we sleep first to give the parallel request a chance to finish
      // and release the lock.
      usleep($sleep);
      // After each sleep, increase the value of $sleep until it reaches 500 ms,
      // to reduce the potential for a lock stampede.
      $delay = $delay - $sleep;
      $sleep = min(500000, $sleep + 25000, $delay);

      if (!apcu_exists(self::getLockKey($name))) {
        // No longer need to wait.
        return FALSE;
      }
    }

    // The caller must still wait longer to get the lock.
    return TRUE;
  }

  /**
   * Releases a lock previously acquired by ApcLock::acquire().
   *
   * This will release the named lock if it is still held by the current
   * request.
   *
   * @param string $name
   *   The name of the lock.
   */
  public static function release($name) {
    // The lock is unconditionally removed, since the caller assumes the lock is
    // anyway released.
    unset(self::$locks[$name]);
    apcu_delete(self::getLockKey($name));
  }

  /**
   * Gets the name of the APCu key used to store a lock.
   *
   * @param string $name
   *   The name of the lock.
   *
   * @return string
   *   The APCu key name.
   */
  public static function getLockKey($name) {
    // self::$uniqueId is already initialized from ApcLock::initialize(). The
    // following lines are added as safeguard, just in case
    // ApcLock::initialize() is not called as expected. This should never happen
    // because Drupal core calls lock_initialize() in
    // _drupal_bootstrap_variables() and lock_initialize() implemented in this
    // file is a wrapper for ApcLock::initialize().
    if (empty(self::$uniqueId)) {
      self::$uniqueId = drupal_random_bytes(32);
    }

    $hash = hash('sha256', self::$uniqueId . $name, TRUE);

    return 'apc_lock::' . drupal_base64_encode($hash);
  }

}

/**
 * Initializes the locking system.
 *
 * @see ApcLock::initialize()
 */
function lock_initialize() {
  ApcLock::initialize();
}

/**
 * Acquires (or renews) a lock, but it does not block if it fails.
 *
 * @param string $name
 *   The name of the lock.
 * @param float $timeout
 *   The number of seconds before the lock expires. The minimum timeout is 1.
 *
 * @return bool
 *   TRUE if the lock was acquired, FALSE otherwise.
 *
 * @see ApcLock::acquire()
 */
function lock_acquire($name, $timeout = 30.0) {
  return ApcLock::acquire($name, $timeout);
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
 * @see ApcLock::maybeAvailable()
 */
function lock_may_be_available($name) {
  return ApcLock::maybeAvailable($name);
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
 * @see ApcLock::waitForLock()
 */
function lock_wait($name, $delay = 30.0) {
  return ApcLock::waitForLock($name, $delay);
}

/**
 * Releases a lock previously acquired by lock_acquire().
 *
 * This will release the named lock if it is still held by the current request.
 *
 * @param string $name
 *   The name of the lock.
 *
 * @see ApcLock::release()
 */
function lock_release($name) {
  ApcLock::release($name);
}
