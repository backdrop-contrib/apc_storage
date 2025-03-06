<?php

/**
 * @file
 * Contains ApcStorageCache.
 */

declare(strict_types=1);

/**
 * APCu cache implementation.
 */
class ApcStorageCache implements BackdropCacheInterface {

  /**
   * The list of all the operations done to the cache.
   *
   * @var array
   */
  protected static array $operations = array();

  /**
   * The cache bin.
   *
   * @var string
   */
  protected string $bin;

  /**
   * Prefix for all keys in this cache bin.
   *
   * @var string
   */
  protected string $binPrefix;

  /**
   * Prefix for all keys in the storage that belong to this site.
   *
   * @var string
   */
  protected string $sitePrefix;

  public function __construct($bin) {
    $identifier = 'apc_storage';
    $prefix = 'backdrop.' . $identifier . '.' . BACKDROP_VERSION . '.' .
      settings_get('apc_storage_deployment_identifier', '') . '.';
    $site_path = conf_path();

    if (settings_get('apc_storage_ensure_unique_prefix', TRUE)) {
      $prefix .= hash_hmac('sha256', $identifier, backdrop_get_hash_salt() . '.' . BACKDROP_ROOT . '/' . $site_path);
    }
    else {
      $prefix .= backdrop_hash_base64(BACKDROP_ROOT . '/' . $site_path);
    }

    $this->bin = $bin;
    $this->sitePrefix = $prefix;
    $this->binPrefix = $this->sitePrefix . '::' . backdrop_base64_encode($this->bin) . '::';
  }

  /**
   * Gets the key to use in APC calls.
   *
   * @param string $cid
   *   The cache ID.
   *
   * @return string
   *   The key which can be used in APC calls to avoid conflicts with other
   *   keys.
   */
  public function getApcuKey(string $cid = ''): string {
    return $this->binPrefix . $cid;
  }

  /**
   * {@inheritdoc}
   */
  public function get($cid): object|false {
    $this->addOperation(array(
      'method' => 'get()',
      'bin' => $this->bin,
      'cid' => array($cid),
    ));

    if (extension_loaded('apcu') && apcu_enabled()) {
      $data = apcu_fetch($this->getApcuKey($cid), $success);

      if (!$success) {
        return FALSE;
      }

      return $this->prepareItem($data);
    }

    return FALSE;
  }

  /**
   * {@inheritdoc}
   */
  public function getMultiple(&$cids): array {
    $cache = array();

    // Add a get to our statistics.
    $this->addOperation(array(
      'method' => 'getMultiple()',
      'bin' => $this->bin,
      'cids' => $cids,
    ));

    if (!$cids) {
      return $cache;
    }

    if (extension_loaded('apcu') && apcu_enabled()) {
      foreach ($cids as $cid) {
        $data = apcu_fetch($this->getApcuKey($cid), $success);

        if ($success) {
          $cache[$cid] = $this->prepareItem($data);
        }
      }

      $cids = array_diff($cids, array_keys($cache));
    }

    return $cache;
  }

  /**
   * Prepares a cached item.
   *
   * Checks that items are either permanent or did not expire.
   *
   * @param object $cache
   *   An item loaded from cache_get() or cache_get_multiple().
   *
   * @return false|object
   *   The item with unserialized data, or FALSE if there is no valid item to
   *   load.
   */
  protected function prepareItem(object $cache): object|false {
    return isset($cache->data) ? $cache : FALSE;
  }

  /**
   * {@inheritdoc}
   */
  public function set($cid, $data, $expire = CACHE_PERMANENT): void {
    $this->addOperation(array(
      'method' => 'set()',
      'bin' => $this->bin,
      'cid' => array($cid),
    ));

    if (extension_loaded('apcu') && apcu_enabled()) {
      // Create new cache object.
      $cache = new stdClass();
      $cache->cid = $cid;
      $cache->created = REQUEST_TIME;
      $cache->expire = $expire;
      $cache->data = $data;

      // Cache values stored in APCu do not need to be serialized, as APCu does
      // that.
      $cache->serialized = 0;

      switch ($expire) {
        case CACHE_PERMANENT:
          $ttl = 0;
          break;

        case CACHE_TEMPORARY:
          // For apcu_store(), using 0 as TTL means the stored data will never
          // expire. The minimum lifetime is one second.
          $cache_lifetime = settings_get('apc_storage_cache_lifetime', 0);
          $ttl = max(1, $cache_lifetime);
          break;

        default:
          $ttl = $expire - time();
          break;
      }

      apcu_store($this->getApcuKey($cid), $cache, $ttl);
    }
  }

  /**
   * {@inheritdoc}
   */
  public function delete($cid): void {
    $this->addOperation(array(
      'method' => 'delete()',
      'bin' => $this->bin,
      'cid' => array($cid),
    ));

    if (extension_loaded('apcu') && apcu_enabled()) {
      apcu_delete($this->getApcuKey($cid));
    }
  }

  /**
   * {@inheritdoc}
   */
  public function deleteMultiple(array $cids): void {
    $this->addOperation(array(
      'method' => 'deleteMultiple()',
      'bin' => $this->bin,
      'cids' => $cids,
    ));

    if (extension_loaded('apcu') && apcu_enabled()) {
      foreach ($cids as $cid) {
        apcu_delete($this->getApcuKey($cid));
      }
    }
  }

  /**
   * {@inheritdoc}
   */
  public function deletePrefix($prefix = ''): void {
    if (is_string($prefix)) {
      $this->addOperation(array(
        'method' => 'deletePrefix()',
        'bin' => $this->bin,
        'cid' => array($prefix),
      ));

      if (extension_loaded('apcu') && apcu_enabled()) {
        $escaped_key = preg_quote($this->getApcuKey() . $prefix, '/');
        $iterator = new APCUIterator("/^$escaped_key/", APC_ITER_KEY);
        apcu_delete($iterator);
      }
    }
  }

  /**
   * {@inheritdoc}
   */
  public function flush(): void {
    $this->deletePrefix();
  }

  public function garbageCollection() {
    // Expired items are automatically deleted by the APCu extension.
  }

  /**
   * Deletes all the cache data stored by ApcStorageCache.
   *
   * @return bool
   *   TRUE if the data was successfully deleted, FALSE otherwise.
   */
  public static function deleteAllStoredData(): bool {
    self::addOperation(array(
      'method' => 'deleteAllStoredData()',
    ));

    if (extension_loaded('apcu') && apcu_enabled()) {
      $iterator = new APCUIterator('/^apc_storage_cache::[a-zA-Z0-9-_]{4,}::[0-9a-f]{2,}/', APC_ITER_KEY);
      apcu_delete($iterator);
      return TRUE;
    }

    return FALSE;
  }

  /**
   * {@inheritdoc}
   */
  public function isEmpty(): bool {
    if (extension_loaded('apcu') && apcu_enabled()) {
      $escaped_key = preg_quote($this->getApcuKey(), '/');
      $iterator = new APCUIterator('/^' . $escaped_key . '/', APC_ITER_KEY);

      return $iterator->getTotalCount() === 0;
    }

    return TRUE;
  }

  /**
   * Retrieves the list of operations done on the cache.
   *
   * @return array
   *   The list of cache operations.
   */
  public static function operations(): array {
    return self::$operations;
  }

  /**
   * Adds a new operation to the list of operations.
   *
   * @param array $operation
   *   The new operation to add.
   */
  protected static function addOperation(array $operation): void {
    // Add the default values for bin and cid.
    $operation += array(
      'bin' => '',
      'cid' => '',
    );

    if (config_get('apc_storage.admin', 'gather_debug_info')) {
      self::$operations[] = $operation;
    }
  }

}
