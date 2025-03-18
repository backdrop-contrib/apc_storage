<?php

/**
 * @file
 * Contains ApcStorageChainedCache.
 */

declare(strict_types=1);

/**
 * APCu chained cache implementation.
 */
class ApcStorageChainedCache extends ApcStorageCache {

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
   * @var ?int
   */
  protected ?int $lastWriteTimestamp = NULL;

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
      $this->lastWriteTimestamp = $cache ? $cache->data : 0;
    }

    return $this->lastWriteTimestamp;
  }

  /**
   * Marks the fast cache bins outdated because data has been written.
   */
  protected function markAsOutdated(): void {
    $last_write_timestamp = $this->getLastWriteTimestamp();
    $now = time() + 1;

    if ($now > $last_write_timestamp) {
      $this->setLastWriteTimestamp($now);
    }
  }

  /**
   * Normalizes a cache ID in order to comply with database limitations.
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
   * Sets the last write timestamp.
   *
   * @param int $timestamp
   *   The new timestamp.
   */
  protected function setLastWriteTimestamp(int $timestamp): void {
    $this->lastWriteTimestamp = $timestamp;
    $this->databaseBackend->set(self::LAST_WRITE_TIMESTAMP_KEY, $this->lastWriteTimestamp);
  }

  /**
   * {@inheritdoc}
   */
  public function get($cid): object|false {
    $item = FALSE;
    $normalized_cid = $this->normalizeCid($cid);

    if ($last_write_timestamp = $this->getLastWriteTimestamp()) {
       $item = parent::get($normalized_cid);

      if ($item && $item->created < $last_write_timestamp) {
        $item = $this->databaseBackend->get($normalized_cid);
        parent::set($item->cid, $item->data, $item->expire);
      }
    }

    if ($item) {
      $item->cid = $cid;
    }

    return $item;
  }

  /**
   * {@inheritdoc}
   */
  public function getMultiple(&$cids): array {
    $cid_mapping = array();
    $cache = array();
    $normalized_cids = array();

    foreach ($cids as $cid) {
      $normalized_cid = $this->normalizeCid($cid);
      $normalized_cids[] = $normalized_cid;
      $cid_mapping[$normalized_cid] = $cid;
    }

    if ($last_write_timestamp = $this->getLastWriteTimestamp()) {
      $items = parent::getMultiple($normalized_cids);

      foreach ($items as $item) {
        if ($item->created < $last_write_timestamp) {
          continue;
        }

        $item->cid = $cid_mapping[$item->cid];
        $cache[$item->cid] = $item;
      }
    }

    $remaining_cids = $normalized_cids;

    foreach ($cache as $item) {
      $index = array_search($this->normalizeCid($item->cid), $remaining_cids);
      if ($index !== FALSE) {
        unset($remaining_cids[$index]);
      }
    }

    $database_items = $this->databaseBackend->getMultiple($remaining_cids);

    foreach ($database_items as $item) {
      $cache[$cid_mapping[$item->cid]] = $item;
      parent::set($item->cid, $item->data, $item->expire);
    }

    $cids = array_diff($cids, array_keys($cache));

    return $cache;
  }

  /**
   * {@inheritdoc}
   */
  public function set($cid, $data, $expire = CACHE_PERMANENT): void {
    $normalized_cid = $this->normalizeCid($cid);

    $this->databaseBackend->set($normalized_cid, $data, $expire);
    $this->markAsOutdated();
    parent::set($normalized_cid, $data, $expire);
  }

  /**
   * {@inheritdoc}
   */
  public function delete($cid): void {
    $this->databaseBackend->delete($this->normalizeCid($cid));
    $this->markAsOutdated();
  }

  /**
   * {@inheritdoc}
   */
  public function deleteMultiple(array $cids): void {
    $normalized_cids = array_map(array($this, 'normalizeCid'), $cids);
    $this->databaseBackend->deleteMultiple($normalized_cids);
    $this->markAsOutdated();
  }

  /**
   * {@inheritdoc}
   */
  public function deletePrefix($prefix): void {
    $prefix = mb_strlen($prefix, '8bit') > 255 ? mb_substr($prefix, 0, 253, '8bit') : $prefix;
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
