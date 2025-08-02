<?php

/**
 * @file
 * Contains ApcCacheTestBase.
 */

declare(strict_types=1);

use apc_storage\cache\CacheInterface;

require_once __DIR__ . '/apc_test_base.class.inc';

/**
 * Base class for cache tests.
 *
 * @testClass
 */
abstract class ApcCacheTestBase extends ApcTestBase {

  /**
   * An array of objects implementing \apc_storage\cache\CacheInterface.
   *
   * @var \apc_storage\cache\CacheInterface[]
   */
  protected array $cacheBackends = [];

  /**
   * Gets a previously created cache backend instance, or creates a new one.
   *
   * @param string $bin
   *   The cache bin.
   *
   * @return CacheInterface
   *   The cache backend instance.
   */
  protected function getCacheBackend(string $bin): CacheInterface {
    if (!isset($this->cacheBackends[$bin])) {
      $this->cacheBackends[$bin] = $this->createCacheBackend($bin);
      $this->cacheBackends[$bin]->flush();
    }

    return $this->cacheBackends[$bin];
  }

  /**
   * Creates a cache backend instance.
   *
   * @param string $bin
   *   The cache bin.
   *
   * @return CacheInterface
   *   The cache backend instance.
   */
  abstract protected function createCacheBackend(string $bin): CacheInterface;

  /**
   * {@inheritdoc}
   */
  protected function setUp(): void {
    $this->cacheBackends = array();
    parent::setUp();
  }

  /**
   * {@inheritdoc}
   */
  protected function tearDown(): void {
    foreach ($this->cacheBackends as $cache_backend) {
      $cache_backend->flush();
    }

    $this->cacheBackends = array();
    parent::tearDown();
  }

  /**
   * Asserts that the given cache bin has been flushed.
   *
   * @param string $bin
   *   The cache bin to flush.
   *
   * @return bool
   *   TRUE if the bin was flushed successfully, FALSE otherwise.
   */
  public function assertFlushedBin(string $bin): bool {
    $messages = [
      'The cache bin @bin is empty after flushing it.',
      'Failed to flush the cache bin @bin or check it was flushed: @message',
    ];

    $backend = $this->getCacheBackend($bin);

    try {
      $backend->flush();

      $this->assertTrue(
        $backend->isEmpty(),
        format_string($messages[0], ['@bin' => $bin])
      );

      return TRUE;
    } catch (Exception $e) {
      $this->assert(
        'exception',
        format_string(
          $messages[1],
          ['@bin' => $bin, '@message' => $e->getMessage()]
        )
      );

      return FALSE;
    }
  }

  /**
   * Asserts that the given data is a cache object.
   *
   * @param string $cid
   *   The cache ID.
   * @param mixed $data
   *   The data to check.
  * @param mixed $expected_value
  *   The expected value of the cache item, or NULL.
  * @param int $expire
   *  The expiration time of the cache item.
   *
   * @return bool
   *   TRUE when $data is a valid cache object containing the expected value,
   *   FALSE otherwise.
   */
  protected function assertIsValidCacheObject(string $cid, mixed $data, mixed $expected_value = NULL, int $expire = CACHE_PERMANENT): bool {
    $args = ['@cid' => $cid];
    $is_cache_object = is_object($data) && isset($data->cid) &&
      isset($data->data) && isset($data->created) && isset($data->expire);
    $messages = [
      '@cid is a cache object.',
      'The cache ID for @cid is correct.',
      'The creation time of @cid is correct.',
      'The expiration time of @cid is correct.',
      'The data for @cid is the expected one.',
    ];
    $now = round(microtime(TRUE));

    if ($this->assertTrue($is_cache_object, $messages[0])) {
      $message = format_string($messages[1], $args);
      $result = $this->assertTrue($data->cid === $cid, $message);
      $message = format_string($message[2], $args);
      $result = $result && $this->assertTrue($data->created >= $now - 1 && $data->created <= $now + 1, $message);
      $message = format_string($messages[3], $args);
      $result = $result && $this->assertTrue($data->expire == $expire, $message);

      if (!is_null($expected_value)) {
        $message = format_string($messages[4], $args);
        $result = $result && $this->assertEqual($data->data, $expected_value, $message);
      }

      return $result;
    }
    else {
      $this->assert(
        'debug',
        format_string(
          'Value obtained for @cid: @data', ['@cid' => $cid, '@data' => var_export($data, TRUE)]
        )
      );

      return FALSE;
    }
  }

  /**
   * Tests flushing the cache.
   */
  public function testFlush(): void {
    $data = [
      'test_2' => 8,
      'test_3' => '16',
      'test_5' => 64.0,
    ];
    $first_backend_bin = 'apc_storage_cache_flush_test_1';
    $messages = [
      'Backend for @bin does not contain data for @cid after flushing the cache.',
      'Backend for @bin still contains data for @cid after flushing the cache.',
    ];
    $second_backend_bin = 'apc_storage_cache_flush_test_2';
    $first_backend = $this->getCacheBackend($first_backend_bin);
    $second_backend = $this->getCacheBackend($second_backend_bin);

    foreach ($data as $cid => $item) {
      $first_backend->set($cid, $item);
      $second_backend->set($cid, $item);
    }

    $this->assertFlushedBin($first_backend_bin);

    foreach ($data as $cid => $item) {
      $args = ['@bin' => $first_backend_bin, '@cid' => $cid];
      $message = format_string($messages[0], $args);

      $this->assertTrue(empty($first_backend->get($cid)), $message);

      $args['@bin'] = $second_backend_bin;
      $message = format_string(
        $messages[1],
        $args
      );

      $this->assertFalse(empty($second_backend->get($cid)), $message);
    }
  }

  /**
   * Tests setting and getting cache items.
   */
  public function testSetGet() : void {
    $backend_bin = 'apc_storage_cache_set_get_test';
    $backend = $this->getCacheBackend($backend_bin);
    $data = [
      'test_1' => ['bar' => '\\Backdrop\\foo\\Bar'],
      'test_2' => ['value' => 3],
      'test_3' => [42],
      'test_4' => ['answer' => 42.42],
      'test_5' => ['msg' => 'Hello, world!'],
      // @todo Before uncommenting the next item, change
      //   DatabaseCache::normalizeCid() to encode Cache IDs in lower-case
      //   Base36; Base64 cannot be used since it used both lower and
      //   upper case characters.
      // 'Test_32' => ['foo' => 'bar'],
      // @todo Verify why the next item is not correctly stored.
      // 'test_' . $this->randomName() => ['baz' => 'qux'],
    ];
    $messages = [
      '@cid has been added to the cache.',
      'All the @total items were retrieved from @bin.',
      '@cid has been updated.'
    ];
    $retrieved_data = [];

    foreach ($data as $cid => $item) {
      $backend->set($cid, $item);
      $this->pass(format_string($messages[0], ['@cid' => $cid]));
    }

    foreach ($data as $cid => $item) {
      $cached = $backend->get($cid);

      if ($cached !== FALSE) {
        $retrieved_data[$cid] = $item;
      }

      $this->assertIsValidCacheObject($cid, $cached, $item);
    }

    $message = format_string(
      $messages[1],
      ['@total' => count($data), '@bin' => $backend_bin]
    );
    $this->assertTrue(count($retrieved_data) == count($data), $message);

    // Avoid the next steps if no item was retrieved.
    if (count($retrieved_data)) {
      $updated_data = [];

      // Add a new value to the retrieved data.
      foreach ($retrieved_data as $cid => $item) {
        $updated_data[$cid] = $item;
        $updated_data[$cid] += ['new_value' => 'foo'];

        $backend->set($cid, $updated_data[$cid]);
        $this->pass(format_string($messages[2], ['@cid' => $cid]));
      }

      // Verify that a cached value can be updated.
      foreach ($updated_data as $cid => $item) {
        $updated_item = $backend->get($cid);

        $this->assertIsValidCacheObject($cid, $updated_item, $item);
      }
    }
  }

  /**
   * Tests deleting multiple cache items.
   */
  public function testDeleteMultiple(): void {
    $backend_bin = 'apc_storage_cache_delete_multiple_test';
    $backend = $this->getCacheBackend($backend_bin);
    $data = array(
      'test1' => 2,
      'test2' => 4,
      'test3' => 8,
    );
    $keys = array_keys($data);
    $messages = [
      '@cid has been added to the cache.',
      'All the @total items were retrieved from @bin.',
      'Items have been deleted from @bin.',
      'Backend does not contain data for @cid after deletion.',
    ];
    $retrieved_data = [];

    foreach ($data as $cid => $item) {
      $backend->set($cid, $item);
      $this->pass(format_string($messages[0], ['@cid' => $cid]));
    }

    foreach ($data as $cid => $item) {
      $cached = $backend->get($cid);

      if ($cached !== FALSE) {
        $retrieved_data[$cid] = TRUE;
      }


      $this->assertIsValidCacheObject($cid, $cached, $item);
    }

    $message = format_string($messages[1], ['@total' => count($data), '@bin' => $backend_bin]);
    $this->assertTrue(count($retrieved_data) == count($data), $message);

    // Avoid the next steps if no item was retrieved.
    if (count($retrieved_data)) {
      $backend->deleteMultiple($keys);
      $this->pass(format_string($messages[2], ['@bin' => $backend_bin]));

      foreach ($keys as $cid) {
        if (!isset($retrieved_data[$cid])) {
          continue;
        }

        $message = format_string($messages[3], ['@cid' => $cid]);
        $this->assertTrue(empty($backend->get($cid)), $message);
      }
    }
  }

  public function testDeletePrefix(): void {
    $backend_bin = 'apc_storage_cache_delete_prefix_test';
    $backend = $this->getCacheBackend($backend_bin);
    $data = [
      'test_1' => 1,
      'test_2' => 3,
      'test_3' => 5,
      'not_test_1' => 'test',
      'not_test_2' => 7,
    ];
    $messages = [
      '@cid has been added to the cache.',
      'All the @total items were retrieved from @bin.',
      'Backend for @bin does not contain data for @cid after deletion.',
      'Backend for @bin still contains data for @cid after deletion.',
    ];
    $retrieved_data = [];

    foreach ($data as $cid => $item) {
      $backend->set($cid, $item);
      $this->pass(format_string($messages[0], ['@cid' => $cid]));
    }

    foreach ($data as $cid => $item) {
      $cached = $backend->get($cid);

      if ($cached !== FALSE) {
        $retrieved_data[$cid] = TRUE;
      }

      $this->assertIsValidCacheObject($cid, $cached, $item);
    }

    $message = format_string($messages[1], ['@total' => count($data), '@bin' => $backend_bin]);
    $this->assertTrue(count($retrieved_data) == count($data), $message);

    // Avoid the next steps if no item was retrieved.
    if (count($retrieved_data)) {
      $backend->deletePrefix('test');

      $keys = array_keys($data);

      // Verify the first three items have been removed.
      foreach (array_slice($keys, 0, 3) as $cid) {
        if (!isset($retrieved_data[$cid])) {
          continue;
        }

        $message = format_string($messages[2], ['@bin' => $backend_bin, '@cid' => $cid]);
        $this->assertTrue(empty($backend->get($cid)), $message);
      }

      // Verify the last two items still exist.
      foreach (array_slice($keys, 3) as $cid) {
        if (!isset($retrieved_data[$cid])) {
          continue;
        }

        $message = format_string($messages[3], ['@bin' => $backend_bin, '@cid' => $cid]);
        $this->assertFalse(empty($backend->get($cid)), $message);
      }
    }
  }

  public function testGetMultiple(): void {
    $backend_bin = 'apc_storage_cache_get_multiple_test';
    $backend = $this->getCacheBackend($backend_bin);
    $data = array(
      'test1' => 2,
      'test2' => 4,
      'test3' => 8,
      'test4' => 16,
      'test5' => 32,
      'test6' => 64,
      'test7' => 128,
      'test8' => '256',
    );
    $messages = [
      '@cid has been added to the cache.',
      'All the @total items were retrieved from @bin.',
      'Backend returned data for @cid.',
      '$cids does not contain @cid.',
      'Backend did not return data for @cid.',
      '$cids contains @cid.',
    ];
    $retrieved_data = [];

    foreach ($data as $cid => $item) {
      $backend->set($cid, $item);
      $this->pass(format_string($messages[0], ['@cid' => $cid]));
    }

    foreach ($data as $cid => $item) {
      $cached = $backend->get($cid);

      if ($cached !== FALSE) {
        $retrieved_data[$cid] = TRUE;
      }

      $this->assertIsValidCacheObject($cid, $cached, $item);
    }

    $message = format_string($messages[1], ['@total' => count($data), '@bin' => $backend_bin]);
    $this->assertTrue(count($retrieved_data) == count($data), $message);

    // Avoid the next steps if no item was retrieved.
    if (count($retrieved_data)) {
      $existing_keys = array_slice(array_keys($data), 0, 4);
      $unexisting_keys = array('test10', 'test13', 'test16', 'test19');
      $requested_keys = array_merge($existing_keys, $unexisting_keys);

      shuffle($requested_keys);

      $cids = $requested_keys;
      $return = $backend->getMultiple($cids);

      foreach ($existing_keys as $cid) {
        if (!isset($retrieved_data[$cid])) {
          continue;
        }

        $args = ['@cid' => $cid];
        $this->assertTrue(isset($return[$cid]), format_string($messages[2], $args));
        $this->assertIsValidCacheObject($cid, $return[$cid]);
        $this->assertFalse(in_array($cid, $cids), format_string($messages[3], $args));
      }

      foreach ($unexisting_keys as $cid) {
        $args = ['@cid' => $cid];
        $this->assertFalse(isset($return[$cid]), format_string($messages[4], $args));
        $this->assertTrue(in_array($cid, $cids), format_string($messages[5], $args));
      }
    }

    $existing_keys = array_slice(array_keys($data), 4);
    $unexisting_keys = array('test21', 'test24', 'test27', 'test30');
    $requested_keys = array_merge($existing_keys, $unexisting_keys);

    shuffle($requested_keys);

    $cids = $requested_keys;
    $return = $backend->getMultiple($cids);

    foreach ($existing_keys as $cid) {
      if (!isset($retrieved_data[$cid])) {
        continue;
      }

      $args = ['@cid' => $cid];
      $this->assertTrue(!empty($return[$cid]), format_string($messages[2], $args));
      $this->assertIsValidCacheObject($cid, $return[$cid]);
      $this->assertFalse(in_array($cid, $cids), format_string($messages[3], $args));
    }

    foreach ($unexisting_keys as $cid) {
      $args = ['@cid' => $cid];
      $this->assertFalse(isset($return[$cid]), format_string($messages[0], $args));
      $this->assertTrue(in_array($cid, $cids), format_string($messages[1], $args));
    }

    // Verify that passing an indexed array of cache IDs works as well.
    $key = array_keys($data)[0];
    $cids = array('cache:key' => $key);
    $return = $backend->getMultiple($cids);

    $this->assertTrue(isset($return[$key]), format_string($messages[2], ['@cid' => $key]));
    $this->assertIsValidCacheObject($key, $return[$key]);
  }

}
