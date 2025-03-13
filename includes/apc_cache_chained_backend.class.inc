<?php

/**
 * @file
 * Contains ApcCacheChainedBackend.
 */

declare(strict_types=1);

/**
 * APCu chained cache implementation.
 */
class ApcCacheChainedBackend extends ApcCacheBackend {

  /**
   * Cache key used to track the last write.
   */
  const string LAST_WRITE_TIMESTAMP_KEY = 'apc_storage_chained_cache::last_write_timestamp';

  /**
   * {@inheritdoc}
   */
  protected string $identifier = 'apc_storage_chained_cache';

  /**
   * {@inheritdoc}
   */
  protected string $sitePrefix = 'apc_storage_chained_cache::';

  /**
   * The time at which the last write to this cache bin happened.
   *
   * @var ?float
   */
  protected ?float $lastWriteTimestamp = NULL;

  /**
   * The database cache backend.
   *
   * @var BackdropCacheInterface
   */
  protected BackdropCacheInterface $databaseBackend;

  public function __construct($bin) {
    parent::__construct($bin);

    $this->databaseBackend = new BackdropDatabaseCache($bin);
  }

  /**
   * Gets the last write timestamp.
   */
  protected function getLastWriteTimestamp(): float {
    if (is_null($this->lastWriteTimestamp)) {
      $cache = $this->databaseBackend->get(self::LAST_WRITE_TIMESTAMP_KEY);
      $this->lastWriteTimestamp = $cache ? $cache->data : 0.0;
    }

    return $this->lastWriteTimestamp;
  }

  /**
   * Sets the last write timestamp.
   *
   * @param float $timestamp
   *   The new timestamp.
   */
  protected function setLastWriteTimestamp(float $timestamp): void {
    $this->lastWriteTimestamp = $timestamp;
    $this->databaseBackend->set(self::LAST_WRITE_TIMESTAMP_KEY, $this->lastWriteTimestamp);
  }

  /**
   * Marks the fast cache bin as outdated because data has been written.
   */
  protected function markAsOutdated(): void {
    $last_write_timestamp = $this->getLastWriteTimestamp();
    $now = round(microtime(TRUE) + 0.001, 3);

    if ($now > $last_write_timestamp) {
      $this->setLastWriteTimestamp(round(microtime(TRUE) + 0.05, 3));
    }
  }

  /**
   * {@inheritdoc}
   */
  public function get($cid): object|false {
    $item = FALSE;

    if ($last_write_timestamp = $this->getLastWriteTimestamp()) {
       $item = parent::get($cid);

      if ($item && $item->created < $last_write_timestamp) {
        $item = $this->databaseBackend->get($cid);
        parent::set($item->cid, $item->data, $item->expire);
      }
    }

    return $item;
  }

  /**
   * {@inheritdoc}
   */
  public function getMultiple(&$cids): array {
    $cids_copy = $cids;
    $cache = array();

    if ($last_write_timestamp = $this->getLastWriteTimestamp()) {
      $items = parent::getMultiple($cids);

      foreach ($items as $item) {
        if ($item->created < $last_write_timestamp) {
          $cids[array_search($item->cid, $cids_copy)] = $item->cid;
        }
        else {
          $cache[$item->cid] = $item;
        }
      }
    }

    if ($cids) {
      foreach ($this->databaseBackend->getMultiple($cids) as $item) {
        $cache[$item->cid] = $item;

        parent::set($item->cid, $item->data, $item->expire);
      }
    }

    return $cache;
  }

  /**
   * {@inheritdoc}
   */
  public function set($cid, $data, $expire = CACHE_PERMANENT): void {
    $this->databaseBackend->set($cid, $data, $expire);
    $this->markAsOutdated();
    parent::set($cid, $data, $expire);
  }

  /**
   * {@inheritdoc}
   */
  public function delete($cid): void {
    $this->databaseBackend->delete($cid);
    $this->markAsOutdated();
  }

  /**
   * {@inheritdoc}
   */
  public function deleteMultiple(array $cids): void {
    $this->databaseBackend->deleteMultiple($cids);
    $this->markAsOutdated();
  }

  /**
   * {@inheritdoc}
   */
  public function deletePrefix($prefix): void {
    $this->databaseBackend->deletePrefix($prefix);
    $this->markAsOutdated();
  }

  /**
   * {@inheritdoc}
   */
  public function flush(): void {
    $this->databaseBackend->flush();
    parent::flush();
  }

}
