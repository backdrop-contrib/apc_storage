<?php

/**
 * @file
 * Contains apc_storage\lock\Lock.
 */

declare(strict_types=1);

namespace apc_storage\lock;

use Random\Engine\Xoshiro256StarStar;
use Random\RandomException;
use Random\Randomizer;

/**
 * Class for the APCu-based locking mechanism implementation.
 *
 * @internal This class should only be used by the APC Storage module.
 */
class Lock {

  /**
   * The locks acquired in the current request.
   *
   * @var self[]
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
   * Retrieves the lock with a given name.
   *
   * @param string $name
   *   The name of the lock.
   *
   * @return $this
   */
  public static function get(string $name): self {
    if (empty(self::$uniqueId)) {
      try {
        self::$uniqueId = random_bytes(32);
      }
      catch (RandomException) {
        // Use a fallback in case random_bytes() fails.
        try {
          $engine = new Xoshiro256StarStar(hash('sha256', hrtime(TRUE) . 'apc_storage_lock_', TRUE));
          $randomizer = new Randomizer($engine);
          self::$uniqueId = $randomizer->getBytes(32);
        }
        catch (\ValueError) {
          // This exception should never be thrown because the seed passed to
          // Random\Engine\Xoshiro256StarStar::__construct() is surely 32 bytes
          // and those are not all zeros.
        }
      }
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
      backdrop_base64_encode($this->name);
  }

  /**
   * Gets the key name for the lock in APCu.
   *
   * @return string
   *   The key name for the lock.
   */
  public function keyName(): string {
    return $this->key;
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
    $expire = microtime(TRUE) + $timeout;

    if (apcu_exists($this->key)) {
      // Try to extend the expiration of an already acquired lock.
      if (!apcu_store($this->key, array('expire' => $expire))) {
        // The lock was broken.
        apcu_delete($this->key);

        // Return failure to acquire the lock, since we do not know what its
        // state is.
        return FALSE;
      }

      return TRUE;
    }
    else {
      // Optimistically try to acquire the lock, then retry once if it fails.
      // The first time through the loop cannot be a retry.
      $retry = FALSE;

      // We always want to do this code at least once.
      do {
        $success = apcu_store($this->key, array('expire' => $expire));

        if (!$success) {
          // If this is our first pass through the loop, then $retry is FALSE.
          // In this case, the insert must have failed meaning some other
          // request acquired the lock but did not release it.
          // We decide whether to retry by checking
          // Lock::maybeAvailable(), since this will break the lock in
          // case it is expired.
          $retry = !$retry && $this->maybeAvailable();
        }
      } while ($retry);

      return apcu_exists($this->key);
    }
  }

  /**
   * Checks if a lock acquired by a different process is available.
   *
   * If an existing lock has expired, it is removed.
   *
   * @return bool
   *   TRUE if the lock can be acquired, FALSE otherwise.
   */
  public function maybeAvailable(): bool {
    if (!apcu_exists($this->key)) {
      return TRUE;
    }

    $data = apcu_fetch($this->key, $success);

    if (!$success || empty($data['expire']) || microtime(TRUE) > (float) $data['expire']) {
      return apcu_delete($this->key);
    }

    return FALSE;
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
  public function wait(float $delay = 30.0): bool {
    $delay = (int) round($delay * 1000000);
    $sleep = 25000;

    while ($delay > 0) {
      usleep($sleep);

      $delay -= $sleep;
      $sleep = min(500000, $sleep + 25000, $delay);

      if ($this->maybeAvailable()) {
        return FALSE;
      }
    }

    return TRUE;
  }

  /**
   * Releases a lock previously acquired by Lock::acquire().
   *
   * This will release the named lock if it is still held by the current
   * request.
   */
  public function release(): void {
    // The lock is unconditionally removed, since the caller assumes the lock is
    // released.
    apcu_delete($this->key);
  }

  /**
   * Releases all previously acquired locks.
   */
  public static function releaseAll(): void {
    if (self::$uniqueId && extension_loaded('apcu') && apcu_enabled()) {
      $key = preg_quote('apc_storage_lock::' . backdrop_base64_encode(self::$uniqueId) . '::', '/');

      apcu_delete(new \APCUIterator("/^" . $key . '[a-zA-Z0-9-_]+$/'));
    }
  }

}
