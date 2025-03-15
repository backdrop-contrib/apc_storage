<?php

/**
 * @file
 * Contains ApcCacheBackend.
 */

declare(strict_types=1);

/**
 * APCu cache implementation.
 */
class ApcCacheBackend implements BackdropCacheInterface {

  /**
   * The identifier used to generate unique APCu keys.
   *
   * @var string
   */
  protected string $identifier = 'apc_storage_cache';

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
  protected string $sitePrefix = 'apc_storage_cache::';

  public function __construct($bin) {
    $prefix = $this->identifier . '.' . 'backdrop.' . BACKDROP_VERSION . '.' .
      settings_get('apc_storage_deployment_identifier', '') . '.';
    $site_path = conf_path();

    if (settings_get('apc_storage_ensure_unique_prefix', TRUE)) {
      $prefix .= hash_hmac(
        'sha256',
        $this->identifier,
        backdrop_get_hash_salt() . '.' . BACKDROP_ROOT . '/' . $site_path,
        TRUE
      );
    }
    else {
      $prefix .= hash(
        'sha256',
        BACKDROP_ROOT . '/' . $site_path,
        TRUE
      );
    }

    $this->bin = $bin;
    $this->sitePrefix .= backdrop_base64_encode($prefix);
    $this->binPrefix = $this->sitePrefix . '::' . backdrop_base64_encode($this->bin) . '::';
  }

  /**
   * Gets the key to use in APC calls.
   *
   * @param string $cid
   *   The cache ID.
   *
   * @return string
   *   The key to use with APCu functions to get, set, or delete a cache item.
   */
  public function getApcuKey(string $cid = ''): string {
    return $this->binPrefix . $cid;
  }

  /**
   * {@inheritdoc}
   */
  public function get($cid): object|false {
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
    $map = array();

    if (!$cids) {
      return $cache;
    }

    if (extension_loaded('apcu') && apcu_enabled()) {
      foreach ($cids as $cid) {
        $map[$this->getApcuKey($cid)] = $cid;
      }

      $result = apcu_fetch(array_keys($map), $success);

      if ($success) {
        foreach ($result as $key => $item) {
          $item = $this->prepareItem($item);
          if ($item) {
            $cache[$map[$key]] = $item;
          }
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
      apcu_delete(array_map(array($this, 'getApcuKey'), $cids));
    }
  }

  /**
   * {@inheritdoc}
   */
  public function deletePrefix($prefix): void {
    if (is_string($prefix)) {
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
    if (extension_loaded('apcu') && apcu_enabled()) {
      $escaped_key = preg_quote($this->getApcuKey(), '/');
      $iterator = new APCUIterator("/^$escaped_key/", APC_ITER_KEY);

      apcu_delete($iterator);
    }
  }

  public function garbageCollection() {
    // Expired items are automatically deleted by the APCu extension.
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

}
