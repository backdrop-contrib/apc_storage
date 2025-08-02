<?php

/**
 * @file
 * Contains \apc_storage\queue\Queue.
 */

declare(strict_types=1);

namespace apc_storage\queue;

use Random\Engine\Xoshiro256StarStar;
use Random\RandomException;
use Random\Randomizer;

/**
 * An APCu-based queue implementation.
 */
class Queue implements \BackdropQueueInterface {

  /**
   * The queue key prefix used in APCu storage.
   */
  protected const string QUEUE_PREFIX = 'apc_storage_queue::';

  /**
   * The randomizer instance used for generating random IDs.
   *
   * @var \Random\Randomizer
   */
  protected static Randomizer $randomizer;

  /**
   * Whether the APCu extension is available and enabled.
   *
   * @var bool
   */
  protected bool $apcuAvailable {
    get => extension_loaded('apcu') && apcu_enabled();
  }

  /**
   * The name of the queue this instance is working with.
   *
   * @var string
   */
  protected string $name;

  /**
   * The APCu iterator to find all the queue keys stored in the APCu store.
   *
   * @val \APCUIterator
   */
  protected \APCUIterator $queueIterator {
    get {
      $prefixed_name = self::QUEUE_PREFIX . backdrop_base64_encode($this->name) . '::';
      $pattern = '/^' . preg_quote($prefixed_name, '/') . '[a-zA-Z0-9-_]+$/';
      return new \APCUIterator($pattern);
    }
  }

  /**
   * The unique ID used for the APCu keys used to store queue data.
   *
   * @var string
   */
  protected string $uniqueId {
    get {
      if (!isset(self::$randomizer)) {
        try {
          $engine = new Xoshiro256StarStar(hash('sha256', hrtime(TRUE) . 'apc_storage_queue_', TRUE));
          self::$randomizer = new Randomizer($engine);
        }
        catch (\ValueError) {
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
        catch (\ValueError) {
          // This exception should never be thrown because the seed passed to
          // Random\Engine\Xoshiro256StarStar::__construct() is surely 32 bytes
          // and those are not all zeros.
        }
      }

    }
  }

  public function __construct(string $name) {
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
    return self::QUEUE_PREFIX .
      backdrop_base64_encode($this->name) . '::' .
      backdrop_base64_encode($item->item_id);
  }

  /**
   * {@inheritdoc}
   */
  public function createItem(mixed $data): bool {
    if (!$this->apcuAvailable) {
      return FALSE;
    }

    $item = (object) [
      'data' => $data,
      'item_id' => $this->uniqueId,
      'expire' => 0,
    ];

    return apcu_store($this->keyName($item), $item);
  }

  /**
   * {@inheritdoc}
   */
  public function numberOfItems(): int {
    if (!$this->apcuAvailable) {
      return 0;
    }

    return $this->queueIterator->getTotalCount();
  }

  /**
   * {@inheritdoc}
   */
  public function claimItem($lease_time = 30): ?object {
    if (!$this->apcuAvailable) {
      return NULL;
    }

    foreach ($this->queueIterator as $item) {
      $item['value']->expire ??= 0;

      // If the previously set lease time has expired, reset the expiration time.
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

    return NULL;
  }

  /**
   * {@inheritdoc}
   */
  public function deleteItem($item): void {
    if ($this->apcuAvailable) {
      apcu_delete($this->keyName($item));
    }
  }

  /**
   * {@inheritdoc}
   */
  public function releaseItem($item): bool {
    $item->expire = 0;

    if ($this->apcuAvailable) {
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
    if ($this->apcuAvailable) {
      apcu_delete($this->queueIterator);
    }
  }

}
