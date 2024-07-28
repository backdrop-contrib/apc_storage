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
   * Requests to clear the cache to send via XML-RPC to the server.
   *
   * @var array
   */
  protected static $pendingRequests = array();

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
   * The prefix to use for the cache bin.
   *
   * @var string
   */
  protected $prefix;

  /**
   * Sets the prefix to use for the cache bin.
   */
  protected function setBinPrefix() {
    $prefixes = variable_get('apc_cache_prefix', '');

    // @todo Remove the following lines in the 7.x-2.x branch.
    if (empty($prefixes)) {
      $prefixes = variable_get('cache_prefix', '');
    }

    if (is_string($prefixes)) {
      // $prefixes can be a string used for all the cache bins.
      $this->prefix = $prefixes;
      return;
    }

    if (is_array($prefixes)) {
      if (isset($prefixes[$this->bin]) && $prefixes[$this->bin] !== TRUE) {
        if ($prefixes[$this->bin] === FALSE) {
          // If $prefixes[$this->bin] is FALSE, no prefix is used for that cache
          // bin, whichever value is used for $prefixes['default'].
          $this->prefix = '';
          return;
        }

        if (is_string($prefixes[$this->bin])) {
          // If $prefixes[$this->bin] is set and not FALSE, that value is used
          // for the cache bin.
          $this->prefix = $prefixes[$this->bin];
        }
      }
      elseif (isset($prefixes['default']) && is_string($prefixes['default'])) {
        $this->prefix = $prefixes['default'];
      }
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

    if (empty($this->prefix) && isset($databases) && is_array($databases)) {
      $db_info = '';

      if (isset($databases['default']['default']) && is_array($databases['default']['default'])) {
        if (isset($databases['default']['default']['host'])) {
          $db_info .= $databases['default']['default']['host'];
        }
        if (isset($databases['default']['default']['database'])) {
          $db_info .= $databases['default']['default']['database'];
        }
        if (isset($databases['default']['default']['prefix'])) {
          $db_info .= serialize($databases['default']['default']['prefix']);
        }
      }

      $hash = substr(hash('sha256', $db_info, TRUE), 0, 16);
      $this->prefix = drupal_base64_encode($hash);
    }

    if (!empty($this->prefix) && substr($this->prefix, -2) !== '::') {
      $this->prefix .= '::';
    }

    // In testing mode, add the test prefix.
    if ($test_prefix = drupal_valid_test_ua()) {
      $this->prefix = $test_prefix . '::' . $this->prefix;
    }
  }

  public function __construct($bin) {
    $this->bin = $bin;

    $this->setBinPrefix();
    $this->addDatabasePrefix();

    $this->prefix = empty($this->prefix) ? 'apc_cache::' : "apc_cache::$this->prefix";
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
    $key_name = $this->prefix . $this->bin . '::';

    if (!is_null($cid)) {
      $key_name .= $cid;
    }

    return $key_name;
  }

  /**
   * {@inheritdoc}
   */
  public function get($cid) {
    $this->operations(array('get()', $this->bin, array($cid)));

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
    $this->operations(array('getMultiple()', $this->bin, $cids));

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
    $this->operations(array('set()', $this->bin, $cid));

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
          $ttl = $cache_lifetime > 0 ? $cache_lifetime : 1;
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
      $this->operations(array('deleteKey()', $this->bin, $cid));
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
      $this->operations(array('deleteKeys()', $this->bin, $prefix));

      $escaped_key = preg_quote($this->keyName($prefix), '/');
      $iterator = new APCUIterator("/^$escaped_key/", APC_ITER_KEY);
      apcu_delete($iterator);
    }
  }

  /**
   * {@inheritdoc}
   */
  public function clear($cid = NULL, $wildcard = FALSE) {
    if (drupal_is_cli()) {
      // APCu uses a separate storage for CLI. Store these requests to be sent
      // to the server via XML-RPC.
      self::$pendingRequests[$this->bin][serialize($cid)] = $wildcard;

      return;
    }

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
   * Retrieves the requests that should be sent to the server via XML-RPC.
   *
   * @return array
   *   An array of requests.
   *   The array contains a key for each bin which must be cleared. The value
   *   for those keys is an array of cache IDs, whose value is TRUE if the cache
   *   ID is a wildcard string or FALSE otherwise.
   */
  public static function pendingRequests() {
    return self::$pendingRequests;
  }

  /**
   * Adds a new operation to the list of operations, or returns the list.
   *
   * @param array|null $new_operation
   *   The new operation to add.
   *
   * @return array
   *   The list of cache operations.
   */
  public static function operations($new_operation = NULL) {
    if (is_array($new_operation)) {
      self::$operations[] = $new_operation;
    }

    return self::$operations;
  }

}
