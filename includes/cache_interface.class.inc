<?php

declare(strict_types=1);

namespace apc_storage\cache;

interface CacheInterface extends \BackdropCacheInterface {

  /**
   * Normalizes a cache ID.
   *
   * @param string $cid
   *   The cache ID.
   *
   * @return string
   *   The normalized cache ID.
   */
  public function normalizeCid(string $cid): string;

  /**
   * Prepares a cached item.
   *
   * Checks that items are either permanent or did not expire, and unserializes
   * data as appropriate.
   *
   * @param object $cache
   *   An item loaded from self::get() or self::getMultiple().
   *
   * @return mixed|false
   *   The item with data unserialized as appropriate, or FALSE if there is no
   *   valid item to load.
   */
  public function prepareItem(object $cache): object|false;

}
