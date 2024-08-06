<?php

/**
 * @file
 * Contains DrupalApcCache.
 */

// phpcs:disable SlevomatCodingStandard.ControlStructures.RequireNullCoalesceOperator.NullCoalesceOperatorNotUsed

/**
 * APCu cache implementation.
 *
 * This is a Drupal cache implementation which uses the APCu extension to store
 * cached data.
 */
class DrupalApcCache implements DrupalCacheInterface {

  /**
   * The list of all the operations done to the cache.
   *
   * @var array
   */
  protected static $operations = array();

  /**
   * The cache bin.
   *
   * @var string
   */
  protected $bin;

  /**
   * The strings used to build the name of the APCu keys for the cached data.
   *
   * @var array
   */
  protected $prefixes = array();

  /**
   * The part of the APCu key which identifies the cache bin.
   *
   * @var string
   */
  protected $binPrefix;

  /**
   * The first part of the APCu key.
   *
   * @var string
   */
  protected $modulePrefix = 'apc_cache';

  /**
   * Converts binary data to a hexadecimal representation.
   *
   * @param mixed $data
   *   The binary data to convert. If it is an empty string, a random 4-byte
   *   string is generated.
   *
   * @return string
   *   The hexadecimal representation of the binary data.
   */
  protected function binaryToHex($data) {
    if (is_array($data)) {
      if (empty($data)) {
        return unpack("H*", "\2\5\6")[1];
      }

      return unpack("H*", "\0\4\6" . serialize($data))[1];
    }
    elseif (is_bool($data)) {
      return $data ? "\3\0\5\1" : "\3\1\5\0";
    }
    elseif (is_string($data)) {
      if ($data === '') {
        // If an empty string is an acceptable value for the caller, return an
        // empty string.
        return '';
      }

      return unpack("H*", "\0\0\4$data")[1];
    }
    elseif (is_object($data) && get_class($data) === 'stdClass') {
      return unpack("H*", "\0\4\3" . var_export((object) (array) $data, TRUE))[1];
    }
    elseif (is_object($data)) {
      return unpack("H*", "\0\4\2" . var_export($data, TRUE))[1];
    }
    else {
      return unpack("H*", "\0\0\1" . var_export($data, TRUE))[1];
    }
  }

  /**
   * Sets the prefix to use for the cache bin.
   */
  protected function setBinPrefix() {
    $default_empty_prefix = '';

    if (extension_loaded('apcu') && apcu_enabled()) {
      $default_empty_prefix = apcu_fetch('apcu_cache::default_prefix', $success);

      if (!$success) {
        $default_empty_prefix = drupal_random_bytes(8);

        if (!apcu_store('apcu_cache::default_prefix', $default_empty_prefix)) {
          // Since it was not possible to store the value to use instead of the
          // empty string, set $default_empty_prefix to an empty string.
          $default_empty_prefix = '';
        }
      }
    }

    $prefixes = variable_get('apc_cache_prefix', $default_empty_prefix);

    // @todo Remove the following lines in the 7.x-2.x branch.
    if (empty($prefixes)) {
      $prefixes = variable_get('cache_prefix', $default_empty_prefix);
    }

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
   * Adds the database prefix to the bin prefix.
   *
   * This method checks if the $prefix property is empty and if the global
   * $databases variable is set and is an array. If these conditions are met,
   * it generates a prefix based on the database connection information.
   *
   * The generated prefix is an SHA-256 hash of the concatenated values of the
   * 'host', 'database', and 'prefix' keys from the 'default' array in the
   * 'default' array of the $databases variable.
   *
   * If the generated prefix is not empty and does not end with '::', the
   * '::' string is appended to the prefix.
   *
   * If the site is in testing mode, the generated prefix is prepended with the
   * value returned by drupal_valid_test_ua().
   */
  protected function addDatabasePrefix() {
    global $databases;
    $default_empty_prefix = '';

    if (extension_loaded('apcu') && apcu_enabled()) {
      $default_empty_prefix = apcu_fetch('apcu_cache::default_prefix', $success);

      if (!$success) {
        $default_empty_prefix = drupal_random_bytes(8);

        if (!apcu_store('apcu_cache::default_prefix', $default_empty_prefix)) {
          $default_empty_prefix = '';
        }
      }
    }

    if (isset($databases) && is_array($databases)) {
      $data = '';

      if (isset($databases['default']['default']) && is_array($databases['default']['default'])) {
        if (isset($databases['default']['default']['host'])) {
          $data .= $this->binaryToHex($databases['default']['default']['host']);
        }

        if (isset($databases['default']['default']['database'])) {
          $data .= $this->binaryToHex($databases['default']['default']['database']);
        }

        if (isset($databases['default']['default']['prefix'])) {
          $data .= $this->binaryToHex($databases['default']['default']['prefix']);
        }
      }

      if (empty($data)) {
        $data = $this->binaryToHex($default_empty_prefix);
      }

      $this->prefixes[2] = $data;
    }

    if ($test_prefix = drupal_valid_test_ua()) {
      $this->prefixes[1] = $this->binaryToHex($test_prefix);
    }
    else {
      $this->prefixes[1] = $this->binaryToHex($default_empty_prefix);
    }
  }

  public function __construct($bin) {
    $this->bin = $bin;

    $this->setBinPrefix();
    $this->addDatabasePrefix();

    $data = $this->prefixes[0] . $this->bin;
    $this->binPrefix = drupal_base64_encode(hash('sha256', $data, TRUE));
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
  protected function keyName($cid = NULL) {
    $key_name = $this->modulePrefix . '::' . $this->binPrefix . '::';

    if (!is_null($cid)) {
      $data = $this->prefixes[1] . $this->bin . $this->prefixes[2] . $cid;
      $data = drupal_base64_encode(hash('sha256', $data, TRUE));
      $key_name .= $data;
    }

    return $key_name;
  }

  /**
   * {@inheritdoc}
   */
  public function get($cid) {
    $this->addOperation(array('get()', $this->bin, array($cid)));

    if (apcu_enabled()) {
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
  protected function prepareItem($cache) {
    return isset($cache->data) ? $cache->data : FALSE;
  }

  /**
   * {@inheritdoc}
   */
  public function getMultiple(&$cids) {
    $cache = array();

    // Add a get to our statistics.
    $this->addOperation(array('getMultiple()', $this->bin, $cids));

    if (!$cids) {
      return $cache;
    }

    if (apcu_enabled()) {
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
  public function set($cid, $data, $expire = CACHE_PERMANENT) {
    // Add set to statistics.
    $this->addOperation(array('set()', $this->bin, $cid));

    if (apcu_enabled()) {
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
  protected function deleteKey($cid) {
    if (apcu_enabled()) {
      $this->addOperation(array('deleteKey()', $this->bin, $cid));
      apcu_delete($this->keyName($cid));
    }
  }

  /**
   * Deletes all the cache IDs matching the given prefix.
   *
   * @param string $prefix
   *   The prefix for the cache IDs to delete.
   */
  protected function deleteKeys($prefix = NULL) {
    if (apcu_enabled() && class_exists('APCUIterator')) {
      $this->addOperation(array('deleteKeys()', $this->bin, $prefix));

      $escaped_key = preg_quote($this->keyName($prefix), '/');
      $iterator = new APCUIterator("/^$escaped_key/", APC_ITER_KEY);
      apcu_delete($iterator);
    }
  }

  /**
   * {@inheritdoc}
   */
  public function clear($cid = NULL, $wildcard = FALSE) {
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
  public function isEmpty() {
    if (apcu_enabled() && class_exists('APCUIterator')) {
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
  public static function operations() {
    return self::$operations;
  }

  /**
   * Adds a new operation to the list of operations.
   *
   * @param array $operation
   *   The new operation to add.
   */
  protected function addOperation($operation) {
    if (variable_get('apc_show_debug', FALSE) && is_array($operation)) {
      self::$operations[] = $operation;
    }
  }

}
