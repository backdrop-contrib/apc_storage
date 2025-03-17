<?php

/**
 * @file
 * Contains ApcCacheLoggedBackend.
 */

declare(strict_types=1);

/**
 * APCu cache implementation which logs the operations.
 */
class ApcCacheLoggedBackend implements BackdropCacheInterface {

  /**
   * The class for the cache backend used to store data.
   *
   * @var string
   */
  protected string $backendClass;

  /**
   * The cache backend which stores data.
   *
   * @var BackdropCacheInterface
   */
  protected BackdropCacheInterface $backend;

  /**
   * The cache bin.
   *
   * @var string
   */
  protected string $bin;

  public function __construct(string $bin) {
    // The allowed cache backends this class can use are the ones implemented by
    // this module.
    $allowed_backends = array('ApcCacheBackend', 'ApcCacheChainedBackend');
    $logged_backend = settings_get('apc_storage_logged_backend', 'ApcCacheBackend');

    if (!in_array($logged_backend, $allowed_backends)) {
      $logged_backend = 'ApcCacheBackend';
    }

    $this->bin = $bin;
    $this->backend = new $logged_backend($bin);
  }

  protected function logCacheOperation(float $start, float $stop, string $operation, array $cids = array()): void {
    if (!class_exists('ApcDataLogger')) {
      require_once __DIR__ . '/apc_data_logger.class.inc';
    }

    ApcDataLogger::addOperation(array(
      'backend' => $this->backendClass,
      'method' => $operation,
      'bin' => $this->bin,
      'cids' => $cids,
      'start' => $start,
      'stop' => $stop,
      'apcu_enabled' => extension_loaded('apcu') && apcu_enabled(),
    ));
  }

  /**
   * {@inheritdoc}
   */
  public function get($cid): object|false {
    $start = microtime(TRUE);
    $cache = $this->backend->get($cid);
    $stop = microtime(TRUE);

    $this->logCacheOperation($start, $stop, 'get()', array($cid));

    return $cache;
  }

  /**
   * {@inheritdoc}
   */
  public function getMultiple(&$cids): array {
    $cids_copy = $cids;
    $start = microtime(TRUE);
    $cache = $this->backend->getMultiple($cids);
    $stop = microtime(TRUE);

    $this->logCacheOperation($start, $stop, 'getMultiple()', $cids_copy);

    return $cache;
  }

  /**
   * {@inheritdoc}
   */
  public function set($cid, $data, $expire = CACHE_PERMANENT): void {
    $start = microtime(TRUE);
    $this->backend->set($cid, $data, $expire);
    $stop = microtime(TRUE);

    $this->logCacheOperation($start, $stop, 'set()', array($cid));
  }

  /**
   * {@inheritdoc}
   */
  public function delete($cid): void {
    $start = microtime(TRUE);
    $this->backend->delete($cid);
    $stop = microtime(TRUE);

    $this->logCacheOperation($start, $stop, 'delete()', array($cid));
  }

    /**
   * {@inheritdoc}
   */
  public function deleteMultiple(array $cids): void {
    $start = microtime(TRUE);
    $this->backend->deleteMultiple($cids);
    $stop = microtime(TRUE);

    $this->logCacheOperation($start, $stop, 'deleteMultiple()', $cids);
  }

   /**
   * {@inheritdoc}
   */
  public function deletePrefix($prefix): void {
    $start = microtime(TRUE);
    $this->backend->deletePrefix($prefix);
    $stop = microtime(TRUE);

    $this->logCacheOperation($start, $stop, 'deletePrefix()', array($prefix));
  }

    /**
   * {@inheritdoc}
   */
  public function flush(): void {
    $start = microtime(TRUE);
    $this->backend->flush();
    $stop = microtime(TRUE);

    $this->logCacheOperation($start, $stop, 'flush()');
  }

  /**
   * {@inheritdoc}
   */
  public function garbageCollection(): void {
    $start = microtime(TRUE);
    $this->backend->garbageCollection();
    $stop = microtime(TRUE);

    $this->logCacheOperation($start, $stop, 'garbageCollection()');
  }

    /**
   * {@inheritdoc}
   */
  public function isEmpty(): bool {
    $start = microtime(TRUE);
    $empty = $this->backend->isEmpty();
    $stop = microtime(TRUE);

    $this->logCacheOperation($start, $stop, 'isEmpty()');

    return $empty;
  }

}
