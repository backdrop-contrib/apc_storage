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
    $prefix = $this->identifier . '.backdrop.' . BACKDROP_VERSION . '.';
    $prefix .= settings_get('apc_storage_deployment_identifier', '') . '.';

    $site_path = conf_path();
    $hash_salt = backdrop_get_hash_salt() . '.' . BACKDROP_ROOT . '/' . $site_path;

    if (settings_get('apc_storage_ensure_unique_prefix', TRUE)) {
      $prefix .= hash_hmac('sha256', $this->identifier, $hash_salt, TRUE);
    }
    else {
      $prefix .= hash('sha256', BACKDROP_ROOT . '/' . $site_path, TRUE);
    }

    $this->bin = $bin;
    $this->sitePrefix .= backdrop_base64_encode($prefix);
    $this->binPrefix = $this->sitePrefix . '::' . backdrop_base64_encode($bin) . '::';
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
  protected function getApcuKey(string $cid = ''): string {
    return $this->binPrefix . $cid;
  }

  /**
   * Retrieves the cache ID from a key.
   *
   * @param string $key
   *   The key to retrieve the cache ID from.
   *
   * @return string
   *   The cache ID.
   */
  protected function getCidFromKey(string $key): string {
    return mb_substr($key, mb_strlen($this->binPrefix, '8bit'), encoding: '8bit');
  }

  /**
   * {@inheritdoc}
   */
  public function get($cid): object|false {
    if (extension_loaded('apcu') && apcu_enabled()) {
      $item = apcu_fetch($this->getApcuKey($cid), $success);

      return $success ? $this->prepareItem($item) : FALSE;
    }

    return FALSE;
  }

  /**
   * {@inheritdoc}
   */
  public function getMultiple(&$cids): array {
    if (!$cids || !extension_loaded('apcu') || !apcu_enabled()) {
      return array();
    }

    $cache = array();
    $keys = array_map(array($this, 'getApcuKey'), $cids);

    if ($result = apcu_fetch($keys, $success)) {
      foreach ($result as $key => $item) {
        $item = $this->prepareItem($item);
        if ($item) {
          $cache[$this->getCidFromKey($key)] = $item;
        }
      }
    }

    $cids = array_diff($cids, array_keys($cache));

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
      $cache = (object) array(
        'cid' => $cid,
        'created' => REQUEST_TIME,
        'expire' => $expire,
        'data' => $data,
        'serialized' => 0,
      );

      $ttl = match ($expire) {
        CACHE_PERMANENT => 0,
        CACHE_TEMPORARY => max(1, settings_get('apc_storage_cache_lifetime', 0)),
        default => $expire - time(),
      };

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
    if (extension_loaded('apcu') && apcu_enabled()) {
      apcu_delete(array_map(array($this, 'getApcuKey'), $cids));
    }
  }

  /**
   * {@inheritdoc}
   */
  public function deletePrefix($prefix): void {
    if (is_string($prefix) && extension_loaded('apcu') && apcu_enabled()) {
      $key = $this->getApcuKey() . $prefix;

      apcu_delete(new APCUIterator("/^" . preg_quote($key, '/') . "/"));
    }
  }

  /**
   * {@inheritdoc}
   */
  public function flush(): void {
    if (extension_loaded('apcu') && apcu_enabled()) {
      $key = preg_quote($this->getApcuKey(), '/');

      apcu_delete(new APCUIterator("/^$key/"));
    }
  }

  /**
   * {@inheritdoc}
   */
  public function garbageCollection(): void {
    // Expired items are automatically deleted by the APCu extension.
  }

  /**
   * {@inheritdoc}
   */
  public function isEmpty(): bool {
    if (extension_loaded('apcu') && apcu_enabled()) {
      $iterator = new APCUIterator('/^' . preg_quote($this->getApcuKey(), '/') . '/');

      return $iterator->getTotalCount() === 0;
    }

    return TRUE;
  }

}
