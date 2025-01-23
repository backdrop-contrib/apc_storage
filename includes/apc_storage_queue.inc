<?php

/**
 * @file
 * Contains ApcStorageQueue.
 */

declare(strict_types=1);

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

  public function createItem(mixed $data): bool {
    $item = new stdClass();
    $item->item_id = random_bytes(32);
    $item->data = $data;
    $item->expire = 0;

    return apcu_store('apc_storage_queue::' . $this->name . '::' . unpack('H*', $item->item_id)[1], $item, 0);
  }

  public function numberOfItems(): int {
    if (extension_loaded('apcu') && apcu_enabled()) {
      $iterator = new APCUIterator('/^apc_storage_queue::' . $this->name . '::[0-9a-f]{64}/');

      return $iterator->getTotalCount();
    }

    return 0;
  }

  public function claimItem(int $lease_time = 30): object {
    if (extension_loaded('apcu') && apcu_enabled()) {
      $iterator = new APCUIterator('/^apc_storage_queue::' . $this->name . '::[0-9a-f]{64}/');

      foreach ($iterator as $item) {
        // If the previously set lease time has expired, reset the expire time.
        if (empty($item['value']->expire) && $item['value']->expire < time()) {
          $item['value']->expire = 0;
        }

        // Find the first item without a set expire.
        if ($item['value']->expire == 0) {
          $item['value']->expire = time() + $lease_time;

          if (apcu_store($item['key'], $item['value'], 0)) {
            return $item['value'];
          }
        }
      }
    }
  }

  public function deleteItem(object $item): void {
    if (extension_loaded('apcu') && apcu_enabled()) {
      apcu_delete('apc_storage_queue::' . $this->name . '::' . unpack('H*', $item->item_id)[1]);
    }
  }

  public function releaseItem(object $item): bool {
    $item->expire = 0;

    if (extension_loaded('apcu') && apcu_enabled()) {
      return apcu_store('apc_storage_queue::' . $this->name . '::' . unpack('H*', $item->item_id)[1], $item, 0);
    }

    return FALSE;
  }

  public function createQueue(): void {
    // Nothing needed here.
  }

  public function deleteQueue(): void {
    if (extension_loaded('apcu') && apcu_enabled()) {
      $iterator = new APCUIterator('/^apc_storage_queue::' . $this->name . '::[0-9a-f]{64}/');

      apcu_delete($iterator);
    }
  }

}
