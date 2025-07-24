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
class DatabaseCache implements CacheInterface {

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
        $this->ensureBinExists();
      }
      catch (\Exception) {
        // Bootstrapping the database was not possible. The database functions
        // will not be available.
      }
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
    $database_schema = NULL;

    try {
      $database_schema = $this->connection->schema();

      if ($database_schema->tableExists($this->bin)) {
        return TRUE;
      }
    }
    catch (\Exception $e) {
      // The connection failed. Assume the bin does not exist.
      return FALSE;
    }

    try {
      $database_schema->createTable($this->bin, $this->schemaDefinition());

      return TRUE;
    }
    catch (\DatabaseSchemaObjectExistsException) {
      // If another process has already created the cache table, attempting to
      // recreate it will throw an exception. In this case just catch the
      // exception and do nothing.
      return TRUE;
    }
    catch (\Exception) {
      return FALSE;
    }
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
      if ($this->ensureBinExists()) {
        // The cache bin exists, but the database failed to write.
        // Rethrow the exception.
        throw $e;
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
      if ($this->ensureBinExists()) {
        // The cache bin exists, but the database failed to write.
        // Rethrow the exception.
        throw $e;
      }
    }
  }

  /**
   * {@inheritdoc}
   * @throws \Exception
   *   The cache bin table exists, but writing the database failed.
   */
  public function deletePrefix($prefix): void {
    // Trailing spaces in cache IDs are not stored; remove them from the passed
    // $prefix.
    $prefix = mb_rtrim($prefix, ' ', '8bit');

    if (mb_strlen($prefix, '8bit') > 255) {
      $prefix = mb_substr($prefix, 0, 253);
    }

    // Cache IDs are stored in lower-case characters; make $prefix lower case
    // too.
    $prefix = mb_strtolower($prefix, '8bit');

    try {
      $this->connection->delete($this->bin)
        ->condition('cid', $this->connection->escapeLike($prefix) . '%', 'LIKE')
        ->execute();
    }
    catch (\Exception $e) {
      if ($this->ensureBinExists()) {
        // The cache bin exists, but the database failed to write.
        // Rethrow the exception.
        throw $e;
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
      if ($this->ensureBinExists()) {
        // The cache bin exists, but the database failed to write.
        // Rethrow the exception.
        throw $e;
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
      if ($this->ensureBinExists()) {
        // The cache bin exists, but the database failed to write.
        // Rethrow the exception.
        throw $e;
      }
    }
  }

  /**
   * {@inheritdoc}
   */
  public function normalizeCid(string $cid): string {
    $lower_case = mb_strtolower($cid, '8bit') === $cid;
    $needle = ' ';
    $needle_length = mb_strlen($needle, '8bit');
    $with_trailing_spaces = mb_substr($cid, -$needle_length, $needle_length, '8bit') == $needle;

    if (mb_strlen($cid, '8bit') <= 255 && $lower_case && !$with_trailing_spaces) {
      return $cid;
    }

    $hash = backdrop_hash_base64($cid);

    if ($with_trailing_spaces) {
      $cid = mb_rtrim($cid, $needle, '8bit');
    }

    if (!$lower_case) {
      $cid = mb_strtolower($cid, '8bit');
    }

    // Return a string that uses as much as possible of the original cache ID
    // with the hash appended.
    return mb_substr($cid, 0, 255 - mb_strlen('::' . $hash, '8bit')) . '::' . $hash;
  }

  /**
   * {@inheritdoc}
   */
  public function prepareItem(object $cache): object|false {
    if (!isset($cache->data)) {
      return FALSE;
    }

    if ($cache->serialized) {
      $cache->data = unserialize($cache->data);
    }

    return $cache;
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
