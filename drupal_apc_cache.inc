<?php

/**
 * @file
 * Contains DrupalApcCache.
 */

/**
 * Statistics about the APC cache.
 */
$_apc_statistics = array();

/**
 * APC cache implementation.
 *
 * This is a Drupal cache implementation which uses the APC extension to store
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
   * Whether the class is used by Drush.
   *
   * @var bool
   */
  protected $drush;

  /**
   * Requests to clear the cache to send via XMLRPC to the server.
   *
   * @var array
   */
  protected static $remoteRequests = array();

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
    $this->bin = $bin;
    $this->drush = (drupal_is_cli() && function_exists('drush_log'));

    // First we determine the prefix from a setting.
    $prefix = self::getPrefixSettingForBin($this->bin);

    // If we do not have a configured prefix we use the HTTP_HOST.
    if (empty($prefix) && isset($_SERVER['HTTP_HOST'])) {
      // Provide a fallback for multisite. This is on purpose not inside the
      // getPrefixSettingsForBin() function in order to decouple the unified
      // prefix variable logic and custom module related security logic, which
      // is not necessary for all backends.
      $prefix = $_SERVER['HTTP_HOST'] . '::';
    }
    else {
      $prefix = $prefix . '::';
    }

    // When we are in testing mode we add the test prefix.
    if ($test_prefix = drupal_valid_test_ua()) {
      $prefix = $test_prefix . '::' . $prefix;
    }
    elseif (isset($GLOBALS['drupal_test_info']['test_run_id'])) {
      $prefix = $GLOBALS['drupal_test_info']['test_run_id'] . '::' . $prefix;
    }

    $this->prefix = "apc_$prefix";
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

    // Fetch the data.
    $cache = apcu_fetch($this->key($cid));

    return $this->prepareItem($cache);
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
    if (!isset($cache->data)) {
      return FALSE;
    }

    $cache_lifetime = variable_get('cache_lifetime', 0);
    $expired = isset($_SESSION['cache_expiration'][$this->bin]) && $_SESSION['cache_expiration'][$this->bin] > $cache->created;
    $expires = $cache->expire != CACHE_PERMANENT;

    // If the cached data is temporary and subject to a per-user minimum
    // lifetime, compare the cache entry timestamp with the user session
    // cache_expiration timestamp. If the cache entry is too old, ignore it.
    if ($expires && $cache_lifetime && $expired) {
      // Ignore cache data that is too old and thus not valid for this user.
      return FALSE;
    }

    return $cache;
  }

  /**
   * {@inheritdoc}
   */
  public function getMultiple(&$cids) {
    $cache = array();
    $keys = array();

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
    if ($this->drush) {
      // APC uses a separate storage for CLI. Send these requests to the server,
      // via XMLRPC.
      self::$remoteRequests[$this->bin][serialize($cid)] = $wildcard;
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
   * Flushes the remote cache.
   */
  public static function remoteFlush() {
    global $base_url;

    if (!module_exists('apc')) {
      drush_log(
        dt('You need to enable the Alternative PHP Cache module for remote cache clearing to work. Run drush pm-enable apc.'),
        'error'
      );
      return;
    }

    if (!empty(self::$remoteRequests)) {
      if ($base_url == 'http://' . basename(conf_path())) {
        drush_log(
          dt('$base_url might not be set correctly. Try using the -l/--uri option for Drush.'),
          'warning'
        );
      }

      // Optimize '*' clears.
      $star = serialize('*');

      foreach (self::$remoteRequests as $bin => $cids) {
        if (!empty($cids[$star])) {
          self::$remoteRequests[$bin] = array($star => TRUE);
        }
      }

      $args = array(
        'apc.clear_cache' => array(
          variable_get('cron_key', 'drupal'),
          self::$remoteRequests,
        ),
      );

      $uri = $base_url . '/xmlrpc.php';
      $response = xmlrpc($uri, $args);

      if ($response === FALSE) {
        drush_log(
          dt(
            'apc.clear_cache: %err_msg (%err_no)',
            array('%err_msg' => xmlrpc_error(), '%err_no' => xmlrpc_errno())
          ),
          'error'
        );
      }
      else {
        drush_log(
          dt("apc.clear_cache: The cache has been cleared."),
          'success'
        );
      }
    }
  }

}
