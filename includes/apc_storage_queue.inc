<?php

/**
 * @file
 * Contains ApcStorageQueue.
 */

declare(strict_types=1);

use Random\RandomException;

/**
 * An APCu-based queue implementation.
 */
class ApcStorageQueue implements DrupalQueueInterface {

  /**
   * The name of the queue this instance is working with.
   *
   * @var string
   */
  protected string $name;

  public function __construct($name) {
    $this->name = $name;
  }

  /**
   * Converts a binary string to a hexadecimal representation.
   *
   * @param string $data
   *   The binary string to convert.
   *
   * @return string
   *   The hexadecimal representation of the binary string.
   */
  protected function binaryToHex(string $data): string {
    return unpack('H*', $data)[1];
  }

  /**
   * {@inheritdoc}
   */
  public function createItem(mixed $data): bool {
    try {
      $item = new stdClass();
      $item->item_id = random_bytes(32);
      $item->data = $data;
      $item->expire = 0;
    }
    catch (RandomException) {
      return FALSE;
    }

    return apcu_store('apc_storage_queue::' . $this->name . '::' . unpack('H*', $item->item_id)[1], $item);
  }

  /**
   * {@inheritdoc}
   */
  public function numberOfItems(): int {
    if (extension_loaded('apcu') && apcu_enabled()) {
      $iterator = new APCUIterator('/^apc_storage_queue::' . $this->name . '::[0-9a-f]{64}/');

      return $iterator->getTotalCount();
    }

    return 0;
  }

  /**
   * {@inheritdoc}
   */
  public function claimItem($lease_time = 30): ?object {
    if (extension_loaded('apcu') && apcu_enabled()) {
      $iterator = new APCUIterator('/^apc_storage_queue::' . $this->name . '::[0-9a-f]{64}/');

      foreach ($iterator as $item) {
        if (!isset($item['value']->expire)) {
          $item['value']->expire = 0;
        }

        // If the previously set lease time has expired, reset the expiration
        // time.
        elseif (!empty($item['value']->expire) && $item['value']->expire < time()) {
          $item['value']->expire = 0;
        }

        // Find the first item without a set expire.
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
      apcu_delete('apc_storage_queue::' . $this->name . '::' . $this->binaryToHex($item->item_id));
    }
  }

  /**
   * {@inheritdoc}
   */
  public function releaseItem($item): bool {
    $item->expire = 0;

    if (extension_loaded('apcu') && apcu_enabled()) {
      return apcu_store('apc_storage_queue::' . $this->name . '::' . $this->binaryToHex($item->item_id), $item);
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
      $iterator = new APCUIterator('/^apc_storage_queue::' . $this->name . '::[0-9a-f]{64}/');

      apcu_delete($iterator);
    }
  }

}
