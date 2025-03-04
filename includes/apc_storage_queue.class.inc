<?php

/**
 * @file
 * Contains ApcStorageQueue.
 */

declare(strict_types=1);

use Random\Engine\Xoshiro256StarStar;
use Random\RandomException;
use Random\Randomizer;

/**
 * An APCu-based queue implementation.
 */
class ApcStorageQueue implements BackdropQueueInterface {

  /**
   * The randomizer instance used for generating random IDs.
   *
   * @var \Random\Randomizer
   */
  protected static Randomizer $randomizer;

  /**
   * The name of the queue this instance is working with.
   *
   * @var string
   */
  protected string $name;

  /**
   * Generates a unique identifier.
   *
   * This method attempts to generate a 32-byte random identifier using
   * random_bytes(). If random_bytes() fails, the Xoshiro256StarStar engine is
   * used.
   *
   * @param int $length
   *   The length of the string to generate. If not provided, the default value
   *   of 32 bytes is used. If the length is negative, the absolute value is
   *   used; if the length is 0, an empty string is returned.
   *
   * @return string
   *   A unique $length-byte string.
   */
  protected static function uniqueId(int $length = 32): string {
    if ($length < 0) {
      $length = abs($length);
    }
    else if ($length === 0) {
      return '';
    }

    if (!isset(self::$randomizer)) {
      try {
        $engine = new Xoshiro256StarStar(hash('sha256', hrtime(TRUE) . 'apc_storage_queue_', TRUE));
        self::$randomizer = new Randomizer($engine);
      }
      catch (ValueError) {
        // This exception should never be thrown because the seed passed to
        // Random\Engine\Xoshiro256StarStar::__construct() is surely 32 bytes
        // and those are not all zeros.
      }
    }

    try {
      return random_bytes($length);
    }
    catch (RandomException) {
      try {
        // Use a fallback in case random_bytes() fails.
        return self::$randomizer->getBytes($length);
      }
      catch (ValueError) {
        // This exception should never be thrown because the seed passed to
        // Random\Engine\Xoshiro256StarStar::__construct() is surely 32 bytes
        // and those are not all zeros.
      }
    }
  }

  public function __construct($name) {
    $this->name = $name;
  }

  /**
   * Generates the key name for storing an item in the APCu queue.
   *
   * @param object $item
   *   The item for which the queue key name is to be generated. The item
   *   object must contain an 'item_id' property.
   *
   * @return string
   *   A string representing the key name used to store the item in the
   *   APCu queue.
   */
  protected function keyName(object $item): string {
    return 'apc_storage_queue::' .
      backdrop_base64_encode($this->name) . '::' .
      backdrop_base64_encode($item->item_id);
  }

  /**
   * {@inheritdoc}
   */
  public function createItem(mixed $data): bool {
    if (extension_loaded('apcu') && apcu_enabled()) {
      $item = new stdClass();
      $item->data = $data;
      $item->item_id = $this->uniqueId();
      $item->expire = 0;

      return apcu_store($this->keyName($item), $item);
    }

    return FALSE;
  }

  /**
   * {@inheritdoc}
   */
  public function numberOfItems(): int {
    if (extension_loaded('apcu') && apcu_enabled()) {
      $name = preg_quote(backdrop_base64_encode($this->name), '/');
      $iterator = new APCUIterator("/^apc_storage_queue::$name::[a-zA-Z0-9-_]+$/");

      return $iterator->getTotalCount();
    }

    return 0;
  }

  /**
   * {@inheritdoc}
   */
  public function claimItem($lease_time = 30): ?object {
    if (extension_loaded('apcu') && apcu_enabled()) {
      $name = preg_quote(backdrop_base64_encode($this->name), '/');
      $iterator = new APCUIterator("/^apc_storage_queue::$name::[a-zA-Z0-9-_]+$/");

      foreach ($iterator as $item) {
        if (!isset($item['value']->expire)) {
          $item['value']->expire = 0;
        }

        // If the previously set lease time has expired, reset the expiration
        // time.
        if (!empty($item['value']->expire) && $item['value']->expire < time()) {
          $item['value']->expire = 0;
        }

        // Find the first item without an expiration time.
        if ($item['value']->expire == 0) {
          $item['value']->expire = time() + $lease_time;

          if (apcu_store($item['key'], $item['value'])) {
            return $item['value'];
          }
        }
      }
    }

    return NULL;
  }

  /**
   * {@inheritdoc}
   */
  public function deleteItem($item): void {
    if (extension_loaded('apcu') && apcu_enabled()) {
      apcu_delete($this->keyName($item));
    }
  }

  /**
   * {@inheritdoc}
   */
  public function releaseItem($item): bool {
    $item->expire = 0;

    if (extension_loaded('apcu') && apcu_enabled()) {
      return apcu_store($this->keyName($item), $item);
    }

    return FALSE;
  }

  /**
   * {@inheritdoc}
   */
  public function createQueue(): void {
    // Nothing needed here.
  }

  /**
   * {@inheritdoc}
   */
  public function deleteQueue(): void {
    if (extension_loaded('apcu') && apcu_enabled()) {
      $name = preg_quote(backdrop_base64_encode($this->name), '/');
      $iterator = new APCUIterator("/^apc_storage_queue::$name::[a-zA-Z0-9-_]+$/");

      apcu_delete($iterator);
    }
  }

}
