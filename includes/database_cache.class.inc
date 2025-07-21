<?php

/**
 * @file
 * Contains \apc_storage\cache\DatabaseCache.
 */

declare(strict_types=1);

namespace apc_storage\cache;

/**
 * Defines a database cache implementation.
 *
 */
class DatabaseCache implements \BackdropCacheInterface {

  /**
   * Indicates that an infinite number of rows is allowed for the cache backend.
   *
   * @var int
   */
  const int MAXIMUM_NONE = -1;

  /**
   * The bin name.
   *
   * @var string
   */
  protected string $bin;

  /**
   * The maximum number of rows that this cache bin table is allowed to store.
   *
   * @var int
   */
  protected int $maxRows = self::MAXIMUM_NONE;

  public function __construct($bin) {
    // All cache tables should be prefixed with 'cache_', except for the
    // default 'cache' bin.
    $this->bin = ($bin == 'cache' ? 'cache' : 'cache_' . $bin);

    $default_max_rows = settings_get('apc_storage_max_rows', self::MAXIMUM_NONE);
    $this->maxRows = settings_get("apc_storage_max_rows_$this->bin", $default_max_rows);

    // Bootstrap the database if it is not yet available.
    if (!function_exists('db_query') || backdrop_get_bootstrap_phase() < BACKDROP_BOOTSTRAP_DATABASE) {
      try {
        backdrop_bootstrap(BACKDROP_BOOTSTRAP_DATABASE, FALSE);
      }
      catch (\Exception) {
        // Bootstrapping the database was not possible. The database functions
        // will not be available.
      }
    }
  }

  /**
   * Acts on an exception when the cache might be stale.
   *
   * If the table exists and yet the query failed, then the cache is stale and
   * the exception needs to propagate.
   *
   * @param \Exception $e
   *   The exception.
   * @param ?string $table_name
   *   The table name. It defaults to $this->bin.
   *
   * @throws \Exception
   */
  protected function catchException(\Exception $e, \DatabaseConnection $connection, ?string $table_name = NULL): void {
    try {
      $database_schema = $connection->schema();
      if ($database_schema->tableExists($table_name ?: $this->bin)) {
        throw $e;
      }
    }
    catch (\Exception) {
      // Ignore the exception.
    }
  }

  /**
   * Stores data in the cache.
   *
   * @param string $cid
   *   The cache ID.
   * @param array $fields
   *
   * @throws \Exception
   *   Writing the database failed.
   *
   * @see \apc_storage\cache\DatabaseCache::set()
   */
  protected function doSet(string $cid, array $fields): void {
    db_merge($this->bin)
      ->key(['cid' => $this->normalizeCid($cid)])
      ->fields($fields)
      ->execute();
  }

  /**
   * Checks the cache bin exists and creates it if not.
   *
   * @result bool
   *   TRUE if the cache bin exists, or it has been created, FALSE otherwise.
   */
  protected function ensureBinExists(): bool {
    try {
      $database_schema = \Database::getConnection()->schema();

      if (!$database_schema->tableExists($this->bin)) {
        $schema_definition = $this->schemaDefinition();
        $database_schema->createTable($this->bin, $schema_definition);
        return TRUE;
      }
    }
    // If another process has already created the cache table, attempting to
    // recreate it will throw an exception. In this case just catch the
    // exception and do nothing.
    catch (\DatabaseSchemaObjectExistsException) {
      return TRUE;
    }

    return FALSE;
  }

  /**
   * Normalizes a cache ID to avoid database limitations.
   *
   * @param string $cid
   *   The cache ID.
   *
   * @return string
   *   The normalized cache ID.
   */
  protected function normalizeCid(string $cid): string {
    if (mb_strlen($cid, '8bit') <= 255 && !str_ends_with($cid, ' ')) {
      return $cid;
    }

    // Return a string that uses as much as possible of the original cache ID
    // with the hash appended.
    $hash = backdrop_hash_base64($cid);

    return mb_substr($cid, 0, 253 - mb_strlen($hash, '8bit')) . '%_' . $hash;
  }


  /**
   * Prepares a cached item.
   *
   * Checks that items are either permanent or did not expire, and unserializes
   * data as appropriate.
   *
   * @param object $cache
   *   An item loaded from self::get() or self::getMultiple().
   *
   * @return mixed|false
   *   The item with data unserialized as appropriate, or FALSE if there is no
   *   valid item to load.
   */
  protected function prepareItem(object $cache): object|false {
    if (!isset($cache->data)) {
      return FALSE;
    }

    if ($cache->serialized) {
      $cache->data = unserialize($cache->data);
    }

    return $cache;
  }

  /**
   * Defines the schema for the {cache_*} bin tables.
   *
   * @return array
   *   The schema definition.
   */
  protected function schemaDefinition(): array {
    $cache_schema = backdrop_get_schema_unprocessed('system', 'cache');
    $cache_schema['description'] = 'Storage for the cache API.';

    return $cache_schema;
  }

  /**
   * {@inheritdoc}
   */
  public function delete($cid): void {
    db_delete($this->bin)
      ->condition('cid', $this->normalizeCid($cid))
      ->execute();
  }

  /**
   * {@inheritdoc}
   *
   * @throws \Exception
   *   Writing the database failed.
   */
  public function deleteMultiple(array $cids): void {
    $cids = array_values(array_map([$this, 'normalizeCid'], $cids));
    $connection = \Database::getConnection();

    try {
      // Delete in chunks when a large array is passed.
      do {
        $connection->delete($this->bin)
          ->condition('cid', array_splice($cids, 0, 1000), 'IN')
          ->execute();
      } while (count($cids));
    }
    catch (\Exception $e) {
      // Create the cache table, which will be empty. This fixes cases where a
      // cache table is cleared before it is set.
      if (!$this->ensureBinExists()) {
        $this->catchException($e, $connection);
      }
    }
  }

  /**
   * {@inheritdoc}
   */
  public function deletePrefix($prefix): void {
    if (mb_strlen($prefix, '8bit') > 255) {
      $prefix = mb_substr($prefix, 0, 253);
    }

    $connection = \Database::getConnection();
    $prefix = $connection->escapeLike($prefix) . '%';

    $connection->delete($this->bin)
      ->condition('cid', $prefix, 'LIKE')
      ->execute();
  }

  /**
   * {@inheritdoc}
   */
  public function flush(): void {
    db_truncate($this->bin)
      ->execute();
  }

  /**
   * {@inheritdoc}
   */
  public function garbageCollection(): void {
    try {
      $connection = \Database::getConnection();

      // Bounded size cache bin, using FIFO.
      if ($this->maxRows !== static::MAXIMUM_NONE) {
        $first_invalid_create_time = $connection->select($this->bin)
          ->fields($this->bin, [
            'created',
          ])
          ->orderBy("$this->bin.created", 'DESC')
          ->range($this->maxRows, 1)
          ->execute()
          ->fetchField();

        if ($first_invalid_create_time) {
          $connection->delete($this->bin)
            ->condition('created', $first_invalid_create_time, '<=')
            ->execute();
        }
      }

      $connection->delete($this->bin)
        ->condition('expire', CACHE_PERMANENT, '<>')
        ->condition('expire', REQUEST_TIME, '<')
        ->execute();
    }
    catch (\Exception) {
      // There is nothing to do: If the table does not exist, it does not have
      // garbage in it; if the table exists, the next garbage collection will
      // clean up.
    }
  }

  /**
   * {@inheritdoc}
   */
  public function get($cid): mixed {
    $cids = [$cid];
    $cache = $this->getMultiple($cids);

    return reset($cache);
  }

  /**
   * {@inheritdoc}
   */
  public function getMultiple(array &$cids): array {
    try {
      $cache = [];
      $cid_mapping = [];
      $connection = \Database::getConnection();

      foreach ($cids as $cid) {
        $cid_mapping[$this->normalizeCid($cid)] = $cid;
      }

      // To avoid the overhead of using ::select() with cached items, ::query()
      // is instead used.
      // Passing to ::query() a concatenated string is highly discouraged; it is
      // used here only due to the performance overhead it would otherwise
      // incur. It is necessary to pass the table name, one of the concatenated
      // string, to ::escapeTable() to sanitize it.
      $table = $connection->escapeTable($this->bin);
      $result = $connection->query(
        sprintf(
          "SELECT cid, data, created, expire, serialized FROM {%s} WHERE cid IN (:cids)",
          $table
        ),
        [':cids' => $cids]
      );

      foreach ($result as $item) {
        // Map the cache ID back to the original.
        $item->cid = $cid_mapping[$item->cid];
        $item = $this->prepareItem($item);

        if ($item) {
          $cache[$item->cid] = $item;
        }
      }

      $cids = array_diff($cids, array_keys($cache));

      return $cache;
    }
    catch (\Exception) {
      return [];
    }
  }

  /**
   * {@inheritdoc}
   */
  public function isEmpty(): bool {
    $this->garbageCollection();

    $query = db_select($this->bin);
    $query->addExpression('1');

    $result = $query->range(0, 1)
      ->execute()
      ->fetchField();

    return empty($result);
  }

  /**
   * {@inheritdoc}
   *
   * @throws \Exception
   *   Writing the database failed.
   */
  public function set($cid, $data, $expire = CACHE_PERMANENT): void {
    $fields = [
      'serialized' => 0,
      'created' => round(microtime(TRUE)),
      'expire' => $expire,
    ];
    $try_again = FALSE;

    if (!is_string($data)) {
      $fields['data'] = serialize($data);
      $fields['serialized'] = 1;
    }
    else {
      $fields['data'] = $data;
    }

    try {
      $this->doSet($cid, $fields);
    }
    catch (\Exception $e) {
      // If there was an exception, try to create the bins.
      $try_again = $this->ensureBinExists();

      if (!$try_again) {
        // If the exception happened for another reason than the missing bin
        // table, propagate the exception.
        throw $e;
      }
    }

    // Now that the bin has been created, try again if necessary.
    if ($try_again) {
      $this->doSet($cid, $fields);
    }
  }

}
