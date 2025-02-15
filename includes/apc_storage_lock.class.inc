<?php

/**
 * @file
 * Contains ApcStorageLock.
 */

declare(strict_types=1);

/**
 * Class for the APCu-based locking mechanism implementation.
 *
 * @internal This class should only be used from third-party modules.
 */
class ApcStorageLock {

  /**
   * The locks acquired in the current request.
   *
   * @var ApcStorageLock[]
   */
  protected static array $locks = array();

  /**
   * The unique ID used to generate the APCu keys for locks.
   *
   * @var string
   */
  protected static string $uniqueId = '';

  /**
   * The name of the lock.
   *
   * @var string
   */
  protected string $name;

  /**
   * The APCu key used for the lock.
   *
   * @var string
   */
  protected string $key;

  /**
   * The expiration time of the lock.
   *
   * @var float
   */
  protected float $expire;

  /**
   * Retrieves the lock with a given name.
   *
   * @param string $name
   *   The name of the lock.
   *
   * @return $this
   */
  public static function get(string $name): self {
    if (!class_exists(ApcStorageHelper::class)) {
      require_once __DIR__ . '/apc_storage_helper.class.inc';
    }

    if (!isset(self::$uniqueId)) {
      self::$uniqueId = ApcStorageHelper::uniqueId();
    }

    if (isset(self::$locks[$name])) {
      return self::$locks[$name];
    }

    $lock = new self($name);
    self::$locks[$name] = $lock;

    return $lock;
  }

  private function __construct(string $name) {
    $this->name = $name;
    $this->key = 'apc_storage_lock::' .
      backdrop_base64_encode(self::$uniqueId) . '::' .
      ApcStorageHelper::binaryToHex($this->name);
  }

  /**
   * Acquires (or renews) a lock. It does not block for already acquired locks.
   *
   * @param float $timeout
   *   The number of seconds before the lock expires. The minimum timeout is 1.
   *
   * @return bool
   *   TRUE if the lock was acquired, FALSE otherwise.
   */
  public function acquire(float $timeout = 30.0): bool {
    // Ensure the timeout is at least one second. The APCu extension consider a
    // key with a zero TTL a permanent key.
    $this->expire = microtime(TRUE) + max($timeout, 1.0);

    // Try to extend the expiration of an already acquired lock.
    $success = apcu_store($this->key, array('expire' => $this->expire));

    if (!$success) {
      // The lock was broken.
      apcu_delete($this->key);

      $this->expire = 0;
    }
    else {
      // Try to acquire the lock.
      $retry = FALSE;

      do {
        $success = apcu_store($this->key, array('expire' => $this->expire));

        if (!$success) {
          $retry = !$retry && $this->maybeAvailable();
        }
      } while ($retry);
    }

    return TRUE;
  }

  /**
   * Checks if a lock acquired by a different process is available.
   *
   * If an existing lock has expired, it is removed.
   *
   * @return bool
   *   TRUE if there is no lock, or it was removed, FALSE otherwise.
   */
  public function maybeAvailable(): bool {
    if (!apcu_exists($this->key)) {
      return TRUE;
    }

    $lock = apcu_fetch($this->key, $success);

    if (!$success) {
      return TRUE;
    }

    $this->expire = (float) $lock['expire'];
    $now = microtime(TRUE);

    if ($now < $this->expire) {
      return FALSE;
    }

    $this->expire = 0;
    apcu_delete($this->key);
    return TRUE;
  }

  /**
   * Waits for a lock to be available.
   *
   * @param float $delay
   *   The maximum number of seconds to wait, as an integer.
   *
   * @return bool
   *   FALSE if the lock is available, TRUE otherwise.
   */
  public function waitForLock(float $delay = 30.0): bool {
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

    // Start sleeping for 25ms.
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

      if (!apcu_exists($this->key)) {
        // No longer need to wait.
        return FALSE;
      }
    }

    // The caller must still wait longer to get the lock.
    return TRUE;
  }

  /**
   * Releases a lock previously acquired by ApcStorageLock::acquire().
   *
   * This will release the named lock if it is still held by the current
   * request.
   */
  public function release(): void {
    // The lock is unconditionally removed, since the caller assumes the lock is
    // released.
    $this->expire = 0;
    apcu_delete($this->key);
  }

  /**
   * Releases all previously acquired locks.
   */
  public static function releaseAll(): void {
    self::$locks = array();

    if (!empty(self::$uniqueId)) {
      if (extension_loaded('apcu') && apcu_enabled()) {
        $key = preg_quote('apc_storage_lock::' . backdrop_base64_encode(self::$uniqueId) . '::', '/');
        $iterator = new APCUIterator("/^$key/", APC_ITER_KEY);

        apcu_delete($iterator);
      }
    }
  }

  /**
   * Deletes all the lock data stored by ApcStorageLock.
   *
   * @return bool
   *   TRUE if the data was successfully deleted, FALSE otherwise.
   */
  public static function deleteAllStoredData(): bool {
    if (extension_loaded('apcu') && apcu_enabled()) {
      $iterator = new APCUIterator('/^apc_storage_lock::[a-zA-Z0-9-_]+::/', APC_ITER_KEY);
      apcu_delete($iterator);

      return TRUE;
    }

    return FALSE;
  }

}
