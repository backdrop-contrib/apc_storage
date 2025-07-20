<?php

/**
 * @file
 * Contains \apc_storage\cache\ChainedCache.
 */

declare(strict_types=1);

namespace apc_storage\cache;

/**
 * Cache implementation with an APCu and a database backend chain.
 *
 * In addition to being useful for sites running on multiple web nodes, this
 * implementation can also be useful for sites running on a single web node
 * where the APCu backend isn't shareable between the web and CLI processes.
 */
class ChainedCache extends Cache {

  /**
   * Cache key used to track the last writing.
   */
  const string LAST_WRITE_TIMESTAMP_KEY = 'apc_storage_chained_cache::last_write_timestamp';

  /**
   * {@inheritdoc}
   */
  protected string $identifier = 'apc_storage_chained_cache';

  /**
   * The time at which the last writing to this cache bin happened.
   *
   * @var ?float
   */
  protected ?float $lastWriteTimestamp = NULL;

  /**
   * {@inheritdoc}
   */
  protected string $sitePrefix = 'apc_storage_cache::';

  /**
   * The database cache backend.
   *
   * @var \BackdropCacheInterface
   */
  protected \BackdropCacheInterface $databaseBackend;

  public function __construct($bin) {
    parent::__construct($bin);

    $this->databaseBackend = new DatabaseCache($bin);
  }

  /**
   * Gets the last writing timestamp.
   */
  protected function getLastWriteTimestamp(): float {
    if (is_null($this->lastWriteTimestamp)) {
      $cache = $this->databaseBackend->get(self::LAST_WRITE_TIMESTAMP_KEY);
      $this->lastWriteTimestamp = $cache ? $cache->data : 0;
    }

    return $this->lastWriteTimestamp;
  }

  /**
   * Marks the APCu cache bins outdated because data has been written.
   *
   * @throws \Exception
   *   Writing the database failed.
   */
  protected function markAsOutdated(): void {
    // Do not assume 'now' on this server is always later than the stored
    // timestamp. Add one second to the current time each time we write it to
    // the database backend and make sure it is always at least one millisecond
    // ahead of the current time.
    $compare = round(microtime(TRUE) + .001, 3);

    if ($compare > $this->getLastWriteTimestamp()) {
      $now = round(microtime(TRUE) + 1, 3);
      $this->setLastWriteTimestamp($now);
    }
  }

  /**
   * Sets the last writing timestamp.
   *
   * @param float $timestamp
   *   The new timestamp.
   *
   * @throws \Exception
   *   Writing the database failed.
   */
  protected function setLastWriteTimestamp(float $timestamp): void {
    $this->lastWriteTimestamp = $timestamp;
    $this->databaseBackend->set(self::LAST_WRITE_TIMESTAMP_KEY, $this->lastWriteTimestamp);
  }

  /**
   * {@inheritdoc}
   *
   * @throws \Exception
   *   Writing the database failed.
   */
  public function delete($cid): void {
    $this->databaseBackend->deleteMultiple([$cid]);
    $this->markAsOutdated();
  }

  /**
   * {@inheritdoc}
   *
   * @throws \Exception
   *   Writing the database failed.
   */
  public function deleteMultiple(array $cids): void {
    $this->databaseBackend->deleteMultiple($cids);
    $this->markAsOutdated();
  }

  /**
   * {@inheritdoc}
   *
   * @throws \Exception
   *   Writing the database failed.
   */
  public function deletePrefix($prefix): void {
    if (is_string($prefix)) {
      $this->databaseBackend->deletePrefix($prefix);
      $this->markAsOutdated();
    }
  }

  /**
   * {@inheritdoc}
   *
   * @throws \Exception
   *   Writing the database failed.
   */
  public function flush(): void {
    $this->databaseBackend->flush();
    $this->markAsOutdated();
  }

  /**
   * {@inheritdoc}
   */
  public function garbageCollection(): void {
    $this->databaseBackend->garbageCollection();
    parent::garbageCollection();
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
    $cids_copy = $cids;
    $cache = [];
    $now = round(microtime(TRUE), 3);
    $last_write_timestamp = $this->getLastWriteTimestamp();

    // Don't bother to either look up from or write to the APCu backend when the
    // $last_write_timestamp is in the future.
    if ($last_write_timestamp && $now > $last_write_timestamp) {
      try {
        $items = parent::getMultiple($cids);
      }
      catch (\Exception) {
        $cids = $cids_copy;
        $items = [];
      }

      // Even if items were successfully fetched from the APCu backend, they are
      // potentially invalid if older than the last time the bin was written to
      // the database backend.
      foreach ($items as $item) {
        if ($item->created <= $last_write_timestamp) {
          $cids[array_search($item->cid, $cids_copy)] = $item->cid;
        }
        else {
          $cache[$item->cid] = $item;
        }
      }
    }

    // If there were cache entries not available in the APCu backend, retrieve
    // them from the database backend and store them in the APCu backend.
    if ($cids) {
      foreach ($this->databaseBackend->getMultiple($cids) as $item) {
        $cache[$item->cid] = $item;

        // Only write back to the fast backend if the created time will be later
        // than $last_write_timestamp to avoid wasted writes.
        if ($now > $last_write_timestamp) {
          parent::set($item->cid, $item->data, $item->expire);
        }
      }
    }

    return $cache;
  }

  /**
   * {@inheritdoc}
   */
  public function isEmpty(): bool {
    return $this->databaseBackend->isEmpty();
  }

  /**
   * {@inheritdoc}
   *
   * @throws \Exception
   *   Writing the database failed.
   */
  public function set($cid, $data, $expire = CACHE_PERMANENT): void {
    // Do not write to the APCu backend. This avoids lock/write contention on
    // the APCu backend for cache items which may not be requested immediately.
    // The APCu backend will be populated via the logic in ::get() instead, when
    // the cache items are actually requested.
    $this->databaseBackend->set($cid, $data, $expire);
    $this->markAsOutdated();
  }

}
