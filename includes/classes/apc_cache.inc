<?php

/**
 * @file
 * Contains ApcCache.
 */

declare(strict_types=1);

/**
 * APCu cache implementation.
 *
 * This is a Drupal cache implementation which uses the APCu extension to store
 * cached data.
 */
class ApcCache implements DrupalCacheInterface {

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
   * The strings used to build the name of the APCu keys for the cached data.
   *
   * @var array
   */
  protected array $prefixes = array();

  /**
   * The part of the APCu key which identifies the cache bin.
   *
   * @var string
   */
  protected string $binPrefix;

  /**
   * The first part of the APCu key.
   *
   * @var string
   */
  protected string $modulePrefix = 'apc_cache';

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
   * Sets the one of the strings used for the APCu storage key.
   */
  protected function setBinPrefix(): void {
    $prefixes = variable_get('apc_cache_prefix', '');

    if (is_string($prefixes) && !empty($prefixes)) {
      // $prefixes can be a string used for all the cache bins.
      $this->prefixes[0] = $this->binaryToHex($prefixes);
      return;
    }

    if (is_array($prefixes)) {
      if (isset($prefixes[$this->bin]) && $prefixes[$this->bin] !== TRUE) {
        if ($prefixes[$this->bin] === FALSE) {
          // If $prefixes[$this->bin] is FALSE, no prefix is used for that cache
          // bin, whichever value is used for $prefixes['default'].
          $this->prefixes[0] = $this->binaryToHex('');
          return;
        }

        if (is_string($prefixes[$this->bin])) {
          // If $prefixes[$this->bin] is set and not FALSE, that value is used
          // for the cache bin.
          $this->prefixes[0] = $this->binaryToHex($prefixes[$this->bin]);
        }
      }
      elseif (isset($prefixes['default']) && is_string($prefixes['default'])) {
        $this->prefixes[0] = $this->binaryToHex($prefixes['default']);
      }
    }

    if (empty($prefixes)) {
      $this->prefixes[0] = $this->binaryToHex('');
    }
  }

  /**
   * Sets two of the strings used for the APCu storage key.
   */
  protected function setDatabasePrefix(): void {
    global $databases;

    if (isset($databases) && is_array($databases)) {
      $data = array();

      if (isset($databases['default']['default']) && is_array($databases['default']['default'])) {
        if (isset($databases['default']['default']['host'])) {
          $data['host'] = $databases['default']['default']['host'];
        }

        if (isset($databases['default']['default']['database'])) {
          $data['database'] = $databases['default']['default']['database'];
        }

        if (isset($databases['default']['default']['prefix'])) {
          $data['prefix'] = $databases['default']['default']['prefix'];
        }
      }

      $this->prefixes[2] = $this->binaryToHex(serialize($data));
    }

    if ($test_prefix = drupal_valid_test_ua()) {
      $this->prefixes[1] = $this->binaryToHex($test_prefix);
    }
    else {
      $this->prefixes[1] = $this->binaryToHex('');
    }
  }

  public function __construct($bin) {
    $this->bin = $bin;

    $this->setBinPrefix();
    $this->setDatabasePrefix();

    $data = $this->prefixes[0] . $this->bin . $this->prefixes[1];
    $data = $this->prefixes[2] . hash('sha256', $data, TRUE);
    $this->binPrefix = drupal_base64_encode($data);
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
  protected function keyName(string $cid = ''): string {
    $key_name = $this->modulePrefix . '::' . $this->binPrefix . '::';

    if ($cid != '') {
      $key_name .= $this->binaryToHex($cid);
    }

    return $key_name;
  }

  /**
   * {@inheritdoc}
   */
  public function get($cid): object|false {
    $this->addOperation(array('get()', $this->bin, array($cid)));

    if (extension_loaded('apcu') && apcu_enabled()) {
      $data = apcu_fetch($this->keyName($cid), $success);

      if (!$success) {
        return FALSE;
      }

      return $this->prepareItem($data);
    }
    else {
      return FALSE;
    }
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
  public function getMultiple(&$cids): array {
    $cache = array();

    // Add a get to our statistics.
    $this->addOperation(array('getMultiple()', $this->bin, $cids));

    if (!$cids) {
      return $cache;
    }

    if (extension_loaded('apcu') && apcu_enabled()) {
      foreach ($cids as $cid) {
        $data = apcu_fetch($this->keyName($cid), $success);

        if ($success) {
          $cache[$cid] = $this->prepareItem($data);
        }
      }

      $cids = array_diff($cids, array_keys($cache));
    }

    return $cache;
  }

  /**
   * {@inheritdoc}
   */
  public function set($cid, $data, $expire = CACHE_PERMANENT): void {
    // Add set to statistics.
    $this->addOperation(array('set()', $this->bin, $cid));

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
          $cache_lifetime = variable_get('cache_lifetime', 0);
          $ttl = max(1, $cache_lifetime);
          break;

        default:
          $ttl = $expire - time();
          break;
      }

      apcu_store($this->keyName($cid), $cache, $ttl);
    }
  }

  /**
   * Deletes a cache item identified by the given cache ID.
   *
   * @param string $cid
   *   The cache ID of the item to delete.
   */
  protected function deleteKey(string $cid): void {
    if (extension_loaded('apcu') && apcu_enabled()) {
      $this->addOperation(array('deleteKey()', $this->bin, $cid));
      apcu_delete($this->keyName($cid));
    }
  }

  /**
   * Deletes all the cache IDs matching the given prefix.
   *
   * @param string|null $prefix
   *   The prefix for the cache IDs to delete.
   */
  protected function deleteKeys(string $prefix = ''): void {
    if (extension_loaded('apcu') && apcu_enabled() && class_exists('APCUIterator')) {
      $this->addOperation(array('deleteKeys()', $this->bin, $prefix));

      $escaped_key = preg_quote($this->keyName($prefix), '/');
      $iterator = new APCUIterator("/^$escaped_key/", APC_ITER_KEY);
      apcu_delete($iterator);
    }
  }

  /**
   * {@inheritdoc}
   */
  public function clear($cid = NULL, $wildcard = FALSE): void {
    if (empty($cid)) {
      $this->deleteKeys();
    }
    else {
      if ($wildcard) {
        if ($cid == '*') {
          $this->deleteKeys();
        }
        else {
          $this->deleteKeys($cid);
        }
      }
      elseif (is_array($cid)) {
        foreach ($cid as $entry) {
          $this->deleteKey($entry);
        }
      }
      else {
        $this->deleteKey($cid);
      }
    }
  }

  /**
   * {@inheritdoc}
   */
  public function isEmpty(): bool {
    if (extension_loaded('apcu') && apcu_enabled()) {
      $escaped_key = preg_quote($this->keyName(), '/');
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
  protected function addOperation(array $operation): void {
    if (variable_get('apc_show_debug', FALSE)) {
      self::$operations[] = $operation;
    }
  }

}
