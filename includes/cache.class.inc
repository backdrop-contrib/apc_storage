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
class Cache extends CacheBase {

  /**
   * The setting name for cache lifetime configuration.
   */
  protected const string DEFAULT_CACHE_LIFETIME = 'apc_storage_cache_lifetime';

  /**
   * The setting name for deployment identifier configuration.
   */
  protected const string DEPLOYMENT_IDENTIFIER = 'apc_storage_deployment_identifier';

  /**
   * The setting name for unique prefix configuration.
   */
  protected const string ENSURE_UNIQUE_PREFIX = 'apc_storage_ensure_unique_prefix';

  /**
   * Whether the APCu extension is available and enabled.
   *
   * @var bool
   */
  protected bool $apcuAvailable {
    get => extension_loaded('apcu') && apcu_enabled();
  }

  /**
   * The identifier used to generate unique APCu keys.
   *
   * @var string
   */
  protected string $identifier = 'apc_storage_cache';

  /**
   * Prefix for all keys in this cache bin.
   *
   * @var string
   */
  protected string $binPrefix = '' {
    get {
      if ($this->binPrefix === '') {
        $prefix = $this->identifier . '.backdrop.' . BACKDROP_VERSION . '.';
        $prefix .= settings_get(self::DEPLOYMENT_IDENTIFIER, '') . '.';

        $site_path = conf_path();
        $hash_salt = backdrop_get_hash_salt() . '.' . BACKDROP_ROOT . '/' . $site_path;

        if (settings_get(self::ENSURE_UNIQUE_PREFIX, TRUE)) {
          $prefix .= hash_hmac('sha256', $this->identifier, $hash_salt, TRUE);
        }
        else {
          $prefix .= hash('sha256', BACKDROP_ROOT . '/' . $site_path, TRUE);
        }

        $this->binPrefix = $this->sitePrefix . backdrop_base64_encode($prefix) . '::' .
          backdrop_base64_encode($this->bin) . '::';
      }

      return $this->binPrefix;
    }
  }

  /**
   * Prefix for all keys in the storage that belong to this site.
   *
   * @var string
   */
  protected string $sitePrefix = 'apc_storage_cache::';

  public function __construct(string $bin) {
    $this->bin = $bin;
  }

  /**
   * Gets the key to use in APC calls.
   *
   * @param string $cid
   *   The cache ID.
   *
   * @return string
   *   The key to use with APCu to get, set, or delete a cache item.
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
  public function deleteMultiple(array $cids): void {
    if ($this->apcuAvailable) {
      apcu_delete(array_map([$this, 'getApcuKey'], $cids));
    }
  }

  /**
   * {@inheritdoc}
   */
  public function deletePrefix($prefix): void {
    if (is_string($prefix) && $this->apcuAvailable) {
      $key = $this->getApcuKey() . $prefix;
      apcu_delete(new \APCUIterator("/^" . preg_quote($key, '/') . "/"));
    }
  }

  /**
   * {@inheritdoc}
   */
  public function flush(): void {
    if ($this->apcuAvailable) {
      $key = preg_quote($this->getApcuKey(), '/');
      apcu_delete(new \APCUIterator("/^$key/"));
    }
  }

  /**
   * {@inheritdoc}
   */
  public function get($cid): mixed {
    if ($this->apcuAvailable) {
      $item = apcu_fetch($this->getApcuKey($cid), $success);
      return $success ? $this->prepareItem($item) : FALSE;
    }
    return FALSE;
  }

  /**
   * {@inheritdoc}
   */
  public function getMultiple(array &$cids): array {
    if (!$cids || !$this->apcuAvailable) {
      return [];
    }

    $cache = [];
    $keys = array_map([$this, 'getApcuKey'], $cids);

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
   * {@inheritdoc}
   */
  public function isEmpty(): bool {
    if ($this->apcuAvailable) {
      $iterator = new \APCUIterator('/^' . preg_quote($this->getApcuKey(), '/') . '/');
      return $iterator->getTotalCount() === 0;
    }
    return TRUE;
  }

  /**
   * {@inheritdoc}
   */
  public function set($cid, $data, $expire = CACHE_PERMANENT): void {
    if ($this->apcuAvailable) {
      $cache = (object) [
        'cid' => $cid,
        'created' => round(microtime(TRUE)),
        'expire' => $expire,
        'data' => $data,
        'serialized' => 0,
      ];

      $ttl = match ($expire) {
        CACHE_PERMANENT => 0,
        CACHE_TEMPORARY => max(1, settings_get(self::DEFAULT_CACHE_LIFETIME, 0)),
        default => $expire - time(),
      };

      apcu_store($this->getApcuKey($cid), $cache, $ttl);
    }
  }

}
