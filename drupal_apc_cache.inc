<?php

/**
 * @file
 * Functions and classes for the APC cache backend.
 */

/**
 * Dummy functions to prevent errors if APC is not installed.
 */
if (!function_exists('apc_fetch')) {

  /**
   * Replacement for apc_fetch() which always return FALSE.
   *
   * @param string $key
   *   The key used to store the value.
   * @param bool &$success
   *   A variable set to FALSE.
   *
   * @return false
   *   The value has not been fetched.
   */
  function apc_fetch($key, &$success = NULL) {
    if ($success) {
      $success = FALSE;
    }

    return FALSE;
  }

  /**
   * Replacement for apc_store() which always return FALSE.
   *
   * @param string $key
   *   The key used to store the value.
   * @param mixed $var
   *   The value to store.
   * @param int $ttl
   *   The time to live in seconds.
   *
   * @return false
   *   The value has not been stored.
   */
  function apc_store($key, $var, $ttl = 0) {
    return FALSE;
  }

}

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
class DrupalAPCCache implements DrupalCacheInterface {

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
   *   been set.
   */
  protected static function getPrefixSettingForBin($bin) {
    $prefixes = variable_get('cache_prefix', '');

    if (is_string($prefixes)) {
      // Variable can be a string, which then considered as a default behavior.
      return $prefixes;
    }

    if (isset($prefixes[$bin])) {
      if (FALSE !== $prefixes[$bin]) {
        // If entry is set and not FALSE, an explicit prefix is set for the bin.
        return $prefixes[$bin];
      }
      else {
        // If we have an explicit false, it means no prefix whatever is the
        // default configuration.
        return '';
      }
    }
    else {
      // Key is not set, we can safely rely on default behavior.
      if (isset($prefixes['default']) && FALSE !== $prefixes['default']) {
        return $prefixes['default'];
      }
      else {
        // When default is not set or an explicit FALSE, this means no prefix.
        return '';
      }
    }
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
    elseif (isset($GLOBALS['drupal_test_info'])) {
      $prefix = $GLOBALS['drupal_test_info']['test_run_id'] . '::' . $prefix;
    }

    $this->prefix = $prefix;
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
    $cache = apc_fetch($this->key($cid));
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
   * @return mixed
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
    $keys = array();

    if (!$cids) {
      return $keys;
    }

    // We need to search the cache with the proper keys and be able to get the
    // original $cid back.
    foreach ($cids as $cid) {
      $keys[$this->key($cid)] = $cid;
    }

    $fetch = apc_fetch(array_keys($keys));
    $cache = array();
    if (!empty($fetch)) {
      foreach ($fetch as $key => $data) {
        $cache[$keys[$key]] = $this->prepareItem($data);
      }
    }
    unset($fetch);

    // Add a get to our statistics.
    $GLOBALS['_apc_statistics'][] = array('get', $this->bin, $cids);

    $cids = array_diff($cids, array_keys($cache));

    return $cache;
  }

  /**
   * {@inheritdoc}
   */
  public function set($cid, $data, $expire = CACHE_PERMANENT, array $headers = NULL) {
    // Add set to statistics.
    $GLOBALS['_apc_statistics'][] = array('set', $this->bin, $cid);

    // Create new cache object.
    $cache = new stdClass();
    $cache->cid = $cid;
    // APC will serialize any structure we give itself.
    $cache->serialized = 0;
    $cache->created = REQUEST_TIME;
    $cache->expire = $expire;
    $cache->headers = $headers;

    $cache->data = $data;

    // What kind of expiration is being used.
    switch ($expire) {
      case CACHE_PERMANENT:
        $set_result = apc_store($this->key($cid), $cache);
        break;

      case CACHE_TEMPORARY:
        if (variable_get('cache_lifetime', 0) > 0) {
          $set_result = apc_store($this->key($cid), $cache, variable_get('cache_lifetime', 0));
        }
        else {
          $set_result = apc_store($this->key($cid), $cache);
        }
        break;

      default:
        $set_result = apc_store($this->key($cid), $cache, $expire - time());
        break;
    }
  }

  /**
   * Deletes all the cache IDs matching the given prefix.
   *
   * @param string $prefix
   *   The prefix for the cache IDs to delete.
   */
  protected function deletePrefix($prefix) {
    if (class_exists('APCIterator')) {
      $escapedKey = preg_quote($this->binKey() . $prefix, '/');
      $iterator = new APCIterator('user', '/^' . $escapedKey . '/', APC_ITER_KEY);

      foreach ($iterator as $key => $data) {
        apc_delete($key);
      }
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
          apc_delete($this->key($entry));
        }
      }
      else {
        apc_delete($this->key($cid));
      }
    }
  }

  /**
   * {@inheritdoc}
   */
  public function isEmpty() {
    if (class_exists('APCIterator')) {
      $escapedKey = preg_quote($this->binKey(), '/');
      $iterator = new APCIterator('user', '/^' . $escapedKey . '/', APC_ITER_KEY);
      return 0 === $iterator->getTotalCount();
    }
    return TRUE;
  }

  /**
   * Flushes the remote cache.
   */
  public static function remoteFlush() {
    global $base_url;

    if (!module_exists('apc')) {
      drush_log('You need to enable the APC module for remote cache clearing to work. Run drush pm-enable apc.', 'error');
      return;
    }

    if (!empty(self::$remoteRequests)) {
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
        drush_log('xmlrpc() error: (' . xmlrpc_errno() . ') ' . xmlrpc_error_msg(), 'error');
        if ($base_url == 'http://' . basename(conf_path())) {
          drush_log('The base_url might not be set correctly try using the -l/--uri option for drush.', 'warning');
        }
      }
      elseif (!$response['success']) {
        drush_log('APC could not flush cache(s) because ' . $uri . ' returned code ' . $response['message'], 'error');
      }
      else {
        drush_log("APC-Remote $uri: {$response['message']}", 'success');
      }
    }
  }

}
