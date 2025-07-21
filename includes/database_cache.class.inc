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
   * The connection to use for the cache.
   *
   * @var \DatabaseConnection
   */
  protected \DatabaseConnection $connection;

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
    try {
      $connection_info = settings_get('apc_storage.connection_info', []);
      if (!empty($connection_info) && is_array($connection_info) && count($connection_info) == 2) {
        $connection = \Database::getConnection($connection_info[1], $connection_info[0]);

        // Verify the connection exists and it works.
        $connection->query("SELECT 1");
      }
      else {
        $connection = \Database::getConnection();
      }
    }
    catch (\Exception) {
      // The connection does not exist, or it does not work.
      // Use the default one.
      $connection = \Database::getConnection();
    }
    finally {
      $this->connection = $connection;
    }

    // All cache tables should be prefixed with 'cache_', except for the
    // default 'cache' bin.
    $this->bin = ($bin == 'cache' ? 'cache' : 'cache_' . $bin);

    $default_max_rows = settings_get('apc_storage.max_rows', self::MAXIMUM_NONE);
    $this->maxRows = settings_get("apc_storage.max_rows_$this->bin", $default_max_rows);

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
   *   The cache bin table exists, but accessing the database failed.
   */
  protected function catchException(\Exception $e, ?string $table_name = NULL): void {
    $database_schema = $this->connection->schema();
    if ($database_schema->tableExists($table_name ?: $this->bin)) {
      // The exception was not caused by the table that did not exist; rethrow
      // the exception.
      throw $e;
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

      if ($database_schema->tableExists($this->bin)) {
        return TRUE;
      }

      $schema_definition = $this->schemaDefinition();
      $database_schema->createTable($this->bin, $schema_definition);

      return TRUE;
    }
    // If another process has already created the cache table, attempting to
    // recreate it will throw an exception. In this case just catch the
    // exception and do nothing.
    catch (\DatabaseSchemaObjectExistsException) {
      return TRUE;
    }
    catch (\Exception) {
      return FALSE;
    }
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
   *
   * @throws \Exception
   *   The cache bin table exists, but writing the database failed.
   */
  public function delete($cid): void {
    try {
      db_delete($this->bin)
        ->condition('cid', $this->normalizeCid($cid))
        ->execute();
    }
    catch (\Exception $e) {
      if (!$this->ensureBinExists()) {
        $this->catchException($e);
      }
    }
  }

  /**
   * {@inheritdoc}
   *
   * @throws \Exception
   *   The cache bin table exists, but writing the database failed.
   */
  public function deleteMultiple(array $cids): void {
    $cids = array_values(array_map([$this, 'normalizeCid'], $cids));

    try {
      // Delete in chunks when a large array is passed.
      do {
        $this->connection->delete($this->bin)
          ->condition('cid', array_splice($cids, 0, 1000), 'IN')
          ->execute();
      } while (count($cids));
    }
    catch (\Exception $e) {
      // Create the cache table, which will be empty. This fixes cases where a
      // cache table is cleared before it is set.
      if (!$this->ensureBinExists()) {
        $this->catchException($e);
      }
    }
  }

  /**
   * {@inheritdoc}
   * @throws \Exception
   *   The cache bin table exists, but writing the database failed.
   */
  public function deletePrefix($prefix): void {
    if (mb_strlen($prefix, '8bit') > 255) {
      $prefix = mb_substr($prefix, 0, 253);
    }

    $prefix = $this->connection->escapeLike($prefix) . '%';

    try {
      $this->connection->delete($this->bin)
        ->condition('cid', $prefix, 'LIKE')
        ->execute();
    }
    catch (\Exception $e) {
      // Create the cache table, which will be empty. This fixes cases where a
      // cache table is cleared before it is set.
      if (!$this->ensureBinExists()) {
        $this->catchException($e);
      }
    }
  }

  /**
   * {@inheritdoc}
   *
   * @throws \Exception
   *   The cache bin table exists, but writing the database failed.
   */
  public function flush(): void {
    try {
      $this->connection->truncate($this->bin)
        ->execute();
    }
    catch (\Exception $e) {
      if (!$this->ensureBinExists()) {
        $this->catchException($e);
      }
    }
  }

  /**
   * {@inheritdoc}
   */
  public function garbageCollection(): void {
    try {
      // Bounded size cache bin, using FIFO.
      if ($this->maxRows !== static::MAXIMUM_NONE) {
        $first_invalid_create_time = $this->connection->select($this->bin)
          ->fields($this->bin, [
            'created',
          ])
          ->orderBy("$this->bin.created", 'DESC')
          ->range($this->maxRows, 1)
          ->execute()
          ->fetchField();

        if ($first_invalid_create_time) {
          $this->connection->delete($this->bin)
            ->condition('created', $first_invalid_create_time, '<=')
            ->execute();
        }
      }

      $this->connection->delete($this->bin)
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

      foreach ($cids as $cid) {
        $cid_mapping[$this->normalizeCid($cid)] = $cid;
      }

      // To avoid the overhead of using ::select() with cached items, ::query()
      // is instead used.
      // Passing to ::query() a concatenated string is highly discouraged; it is
      // used here only due to the performance overhead it would otherwise
      // incur. It is necessary to pass the table name, one of the concatenated
      // string, to ::escapeTable() to sanitize it.
      $table = $this->connection->escapeTable($this->bin);
      $result = $this->connection->query(
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
   *
   * @throws \Exception
   *   The cache bin table exists, but accessing the database failed.
   */
  public function isEmpty(): bool {
    try {
      $this->garbageCollection();
      $query = $this->connection->select($this->bin);
      $query->addExpression('1');
      $result = $query->range(0, 1)
        ->execute()
        ->fetchField();

      return empty($result);
    }
    catch (\Exception $e) {
      if (!$this->ensureBinExists()) {
        $this->catchException($e);
      }

      return FALSE;
    }
  }

  /**
   * {@inheritdoc}
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
    catch (\Exception) {
      // If there was an exception, try to create the bins.
      $try_again = $this->ensureBinExists();
    }

    // Now that the bin has been created, try again if necessary.
    try {
      if ($try_again) {
        $this->doSet($cid, $fields);
      }
    }
    catch (\Exception) {
      // The database may not be available; ignore these calls.
    }
  }

}
