<?php

/**
 * @file
 * Contains ApcStorageQueue.
 */

declare(strict_types=1);

/**
 * An APCu-based queue implementation.
 */
class ApcStorageQueue implements BackdropQueueInterface {

  /**
   * The name of the queue this instance is working with.
   *
   * @var string
   */
  protected string $name;

  public function __construct($name) {
    if (!class_exists(ApcStorageHelper::class)) {
      require_once __DIR__ . '/apc_storage_helper.class.inc';
    }

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
  protected function queueKeyName(object $item): string {
    return 'apc_storage_queue::' . $this->name . '::' . ApcStorageHelper::binaryToHex($item->item_id);
  }

  /**
   * {@inheritdoc}
   */
  public function createItem(mixed $data): bool {
    if (extension_loaded('apcu') && apcu_enabled()) {
      $item = new stdClass();
      $item->data = $data;
      $item->item_id = ApcStorageHelper::uniqueId();
      $item->expire = 0;

      return apcu_store($this->queueKeyName($item), $item);
    }

    return FALSE;
  }

  /**
   * {@inheritdoc}
   */
  public function numberOfItems(): int {
    if (extension_loaded('apcu') && apcu_enabled()) {
      $name = preg_quote($this->name, '/');
      $iterator = new APCUIterator("/^apc_storage_queue::$name::[0-9a-f]{64}/");

      return $iterator->getTotalCount();
    }

    return 0;
  }

  /**
   * {@inheritdoc}
   */
  public function claimItem($lease_time = 30): ?object {
    if (extension_loaded('apcu') && apcu_enabled()) {
      $name = preg_quote($this->name, '/');
      $iterator = new APCUIterator("/^apc_storage_queue::$name::[0-9a-f]{64}/");

      foreach ($iterator as $item) {
        if (!isset($item['value']->expire)) {
          $item['value']->expire = 0;
        }

        // If the previously set lease time has expired, reset the expiration
        // time.
        elseif (!empty($item['value']->expire) && $item['value']->expire < time()) {
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
      apcu_delete($this->queueKeyName($item));
    }
  }

  /**
   * {@inheritdoc}
   */
  public function releaseItem($item): bool {
    $item->expire = 0;

    if (extension_loaded('apcu') && apcu_enabled()) {
      return apcu_store($this->queueKeyName($item), $item);
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
      $name = preg_quote($this->name, '/');
      $iterator = new APCUIterator("/^apc_storage_queue::$name::[0-9a-f]{64}/");

      apcu_delete($iterator);
    }
  }

}
