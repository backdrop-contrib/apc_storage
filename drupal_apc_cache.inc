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
   * Requests to clear the cache to send via XML-RPC to the server.
   *
   * @var array
   */
  protected static $pendingRequests = array();

  /**
   * Gets the prefix to use for the cache bin.
   *
   * @param string $bin
   *   The cache bin.
   *
   * @return string
   *   The prefix to use for the cache bin, or an empty string if no prefix has
   *   been set or the prefix is not a string.
   */
  protected static function getPrefixSettingForBin($bin) {
    $prefixes = variable_get('apc_cache_prefix', '');

    // @todo Remove the following lines in the 7.x-2.x branch.
    if (empty($prefixes)) {
      $prefixes = variable_get('cache_prefix', '');
    }

    if (is_string($prefixes)) {
      // $prefixes can be a string used for all the cache bins.
      return $prefixes;
    }

    if (is_array($prefixes)) {
      if (isset($prefixes[$bin]) && $prefixes[$bin] !== TRUE) {
        if ($prefixes[$bin] === FALSE) {
          // If $prefixes[$bin] is FALSE, no prefix is used for that cache bin,
          // whichever value is used for $prefixes['default'].
          return '';
        }

        if (is_string($prefixes[$bin])) {
          // If $prefixes[$bin] is set and not FALSE, that value is used for the
          // cache bin.
          return $prefixes[$bin];
        }
      }
      elseif (isset($prefixes['default']) && is_string($prefixes['default'])) {
        return $prefixes['default'];
      }
    }

    return '';
  }

  public function __construct($bin) {
    global $databases;

    $this->bin = $bin;

    // First we determine the prefix from a setting.
    $prefix = self::getPrefixSettingForBin($this->bin);

    // As fallback for a multisite installation, use the database connection
    // information to generate a prefix.
    if (empty($prefix) && isset($databases) && is_array($databases)) {
      $db_info = '';

      if (isset($databases['default']['default']) && is_array($databases['default']['default'])) {
        if (isset($databases['default']['default']['host'])) {
          $db_info .= $databases['default']['default']['prefix'];
        }
        if (isset($databases['default']['default']['database'])) {
          $db_info .= $databases['default']['default']['database'];
        }
        if (isset($databases['default']['default']['prefix'])) {
          $db_info .= serialize($databases['default']['default']['prefix']);
        }
      }

      $hash = substr(hash('sha256', $db_info, TRUE), 0, 16);
      $prefix = drupal_base64_encode($hash);
    }

    if (!empty($prefix) && substr($prefix, -2) !== '::') {
      $prefix .= '::';
    }

    // When we are in testing mode we add the test prefix.
    if ($test_prefix = drupal_valid_test_ua()) {
      $prefix = $test_prefix . '::' . $prefix;
    }

    $this->prefix = empty($prefix) ? 'apc_cache::' : "apc_cache_$prefix";
  }

  /**
   * Gets the key to use for the cache bin in APC calls.
   *
   * @return string
   *   The key to use for the cache bin.
   */
  private function binKey() {
    return $this->prefix . $this->bin . '::';
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
  private function key($cid) {
    return $this->binKey() . $cid;
  }

  /**
   * {@inheritdoc}
   */
  public function get($cid) {
    // Add a get to our statistics.
    $GLOBALS['_apc_statistics'][] = array('get', $this->bin, array($cid));

    return $this->prepareItem(apcu_fetch($this->key($cid)));
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
    $GLOBALS['_apc_statistics'][] = array('get', $this->bin, $cids);

    if (!$cids) {
      return $cache;
    }

    foreach ($cids as $cid) {
      $data = apcu_fetch($this->key($cid), $success);

      if ($success) {
        $cache[$cid] = $this->prepareItem($data);
      }
    }

    $cids = array_diff($cids, array_keys($cache));

    return $cache;
  }

  /**
   * {@inheritdoc}
   */
  public function set($cid, $data, $expire = CACHE_PERMANENT) {
    // Add set to statistics.
    $GLOBALS['_apc_statistics'][] = array('set', $this->bin, $cid);

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

    apcu_store($this->key($cid), $cache, $ttl);
  }

  /**
   * Deletes all the cache IDs matching the given prefix.
   *
   * @param string $prefix
   *   The prefix for the cache IDs to delete.
   */
  protected function deletePrefix($prefix) {
    if (class_exists('APCUIterator')) {
      $escaped_key = preg_quote($this->binKey() . $prefix, '/');
      $iterator = new APCUIterator("/^$escaped_key/", APC_ITER_KEY);
      apcu_delete($iterator);
    }
  }

  /**
   * Flushes all cache items in a bin.
   */
  protected function flush() {
    $this->deletePrefix('');
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

    // Add a get to our statistics.
    $GLOBALS['_apc_statistics'][] = array('clear', $this->bin, $cid, (int) $wildcard);

    if (empty($cid)) {
      $this->flush();
    }
    else {
      if ($wildcard) {
        if ($cid == '*') {
          $this->flush();
        }
        else {
          $this->deletePrefix($cid);
        }
      }
      elseif (is_array($cid)) {
        foreach ($cid as $entry) {
          apcu_delete($this->key($entry));
        }
      }
      else {
        apcu_delete($this->key($cid));
      }
    }
  }

  /**
   * {@inheritdoc}
   */
  public function isEmpty() {
    if (class_exists('APCUIterator')) {
      $escaped_key = preg_quote($this->binKey(), '/');
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

}
