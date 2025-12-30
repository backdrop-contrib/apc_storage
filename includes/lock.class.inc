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
   * The prefix used for all APCu lock keys.
   */
  protected const string LOCK_KEY_PREFIX = 'apc_storage_lock::';

  /**
   * Default timeout value in seconds for lock operations.
   */
  protected const float DEFAULT_TIMEOUT = 30.0;

  /**
   * The locks acquired in the current request.
   *
   * @var self[]
   */
  protected static array $locks = [];

  /**
   * The unique ID used to generate the APCu keys for locks.
   *
   * @var string
   */
  protected static string $uniqueId = '';

  /**
   * The APCu key used for the lock.
   *
   * @var string
   */
  protected(set) string $keyName;

  /**
   * Generates a unique identifier for the lock system.
   *
   * @return string
   *   A 32-byte unique identifier.
   */
  protected static function generateUniqueId(): string {
    try {
      return random_bytes(32);
    }
    catch (RandomException) {
      // Use a fallback in case random_bytes() fails.
      try {
        $engine = new Xoshiro256StarStar(hash('sha256', 'apc_storage_lock_' . hrtime(TRUE), TRUE));
        $randomizer = new Randomizer($engine);

        return $randomizer->getBytes(32);
      }
      catch (\ValueError) {
        // This exception should never be thrown because the seed passed to
        // Random\Engine\Xoshiro256StarStar::__construct() is surely 32 bytes
        // and those are not all zeros.
        return '';
      }
    }
  }

  /**
   * Checks if the APCu extension is available and enabled.
   *
   * @return bool
   *   TRUE if APCu is available, FALSE otherwise.
   */
  protected static function isApcuAvailable(): bool {
    return extension_loaded('apcu') && apcu_enabled();
  }

  /**
   * Constructs a new \apc_storage\lock\Lock instance.
   *
   * The constructor is protected to ensure that locks can only be created
   * through the static get() method.
   *
   * @param string $name
   *   The name of the lock.
   */
  protected function __construct(protected(set) string $name) {
    $this->keyName = $this->generateLockKey();
  }

  /**
   * Generates the APCu key for the current lock.
   *
   * @return string
   *   The generated APCu key for the lock.
   */
  protected function generateLockKey(): string {
    return self::LOCK_KEY_PREFIX .
      backdrop_base64_encode(self::$uniqueId) . '::' .
      backdrop_base64_encode($this->name);
  }

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
      self::$uniqueId = self::generateUniqueId();
    }

    if (isset(self::$locks[$name])) {
      return self::$locks[$name];
    }

    $lock = new self($name);
    self::$locks[$name] = $lock;

    return $lock;
  }

  /**
   * Releases all previously acquired locks.
   */
  public static function releaseAll(): void {
    if (self::$uniqueId && self::isApcuAvailable()) {
      $key = preg_quote(self::LOCK_KEY_PREFIX . backdrop_base64_encode(self::$uniqueId) . '::', '/');

      apcu_delete(new \APCUIterator("/^" . $key . '[a-zA-Z0-9-_]+$/'));
    }
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
  public function acquire(float $timeout = self::DEFAULT_TIMEOUT): bool {
    if (!self::isApcuAvailable()) {
      // APCu is not available, so the lock cannot be acquired.
      return FALSE;
    }

    $expire = microtime(TRUE) + $timeout;

    if (apcu_exists($this->keyName)) {
      // Try to extend the expiration of an already acquired lock.
      if (!apcu_store($this->keyName, ['expire' => $expire])) {
        // The lock was broken.
        apcu_delete($this->keyName);

        // Return failure to acquire the lock, since we do not know what its
        // state is.
        return FALSE;
      }

      return TRUE;
    }

    // Optimistically try to acquire the lock, then retry once if it fails.
    // The first time through the loop cannot be a retry.
    $retry = FALSE;

    // We always want to do this code at least once.
    do {
      $success = apcu_store($this->keyName, ['expire' => $expire]);

      if (!$success) {
        // If this is our first pass through the loop, then $retry is FALSE.
        // In this case, the insert must have failed, meaning some other request
        // acquired the lock but did not release it.
        // We decide whether to retry by checking Lock::maybeAvailable() since
        // this will break the lock in case it is expired.
        $retry = !$retry && $this->maybeAvailable();
      }
    }
    while ($retry);

    return $success;
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
    if (!self::isApcuAvailable()) {
      // APCu is not available, so the lock cannot be acquired.
      return FALSE;
    }

    if (!apcu_exists($this->keyName)) {
      return TRUE;
    }

    $data = apcu_fetch($this->keyName, $success);

    return ($success && (empty($data['expire']) || microtime(TRUE) >= (float) $data['expire']));
  }

  /**
   * Releases a lock previously acquired by Lock::acquire().
   *
   * This will release the named lock if it is still held by the current
   * request.
   */
  public function release(): void {
    if (!self::isApcuAvailable()) {
      // APCu is not available, so the lock cannot be released.
      return;
    }

    // The lock is unconditionally removed, since the caller assumes the lock is
    // released.
    apcu_delete($this->keyName);
  }

  /**
   * Waits for a lock to be available.
   *
   * @param float $delay
   *   The maximum number of seconds to wait, as an integer.
   *
   * @return bool
   *   FALSE if the lock is available, TRUE if ::wait() still needs to be
   *   called.
   */
  public function wait(float $delay = self::DEFAULT_TIMEOUT): bool {
    if (!self::isApcuAvailable()) {
      // APCu is not available; return TRUE to mean that ::wait() should be
      // called again.
      return TRUE;
    }

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

}
