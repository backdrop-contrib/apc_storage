<?php

/**
 * @file
 * Contains \apc_storage\cache\Cache.
 */

declare(strict_types=1);

namespace apc_storage\cache;

/**
 * The APCu cache implementation.
 */
abstract class CacheBase implements CacheInterface {

  /**
   * {@inheritdoc}
   */
  public function garbageCollection(): void {
    // The default implementation does nothing.
  }


  /**
   * {@inheritdoc}
   */
  public function normalizeCid(string $cid): string {
    // The default implementation does not normalize the cache ID.
    return $cid;
  }

  /**
   * {@inheritdoc}
   */
  public function prepareItem(object $cache): object|false {
    if (!isset($cache->data)) {
      return FALSE;
    }

    if ($cache->serialized) {
      $cache->data = unserialize($cache->data);
    }

    return $cache;
  }

}
