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
    $backend = $this->getCacheBackend($bin);

    try {
      $backend->flush();

      $this->assertTrue(
        $backend->isEmpty(),
        format_string('The cache bin @bin is empty after flushing.', ['@bin' => $bin])
      );

      return TRUE;
    } catch (\Exception $e) {
      $this->fail(
        format_string(
          'Failed to flush the cache bin @bin or check it was flushed: @message',
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
   * @param int $expire
   *  The expiration time of the cache item.
   * @param bool $full_check
   *   Whether to perform a full check of the cache object.
   *
   * @return bool
   *   TRUE on success, FALSE on failure.
   */
  protected function assertIsValidCacheObject(string $cid, mixed $data, int $expire = CACHE_PERMANENT, $full_check = FALSE): bool {
    $is_cache_object = is_object($data) &&
      isset($data->cid) &&
      isset($data->data) &&
      isset($data->created) &&
      isset($data->expire);
    $message_args = ['@cid' => $cid];
    $messages = [
      '',
      format_string('The cache ID for @cid is correct.', $message_args),
      format_string('The creation time of @cid is correct.', $message_args),
      format_string('The expiration time of @cid is correct.', $message_args),
    ];
    $now = round(microtime(TRUE));

    if (isset($data->data)) {
      $messages[0] = format_string(
        'Item @cid is a cache object (@data).',
        ['@cid' => $cid, '@data' => var_export($data->data, TRUE)]
      );
    }
    else {
      $messages[0] = format_string('Item @cid is a cache object.', ['@cid' => $cid]);
    }

    if ($this->assertTrue($is_cache_object, $messages[0])) {
      if (!$full_check) {
        return TRUE;
      }

      $result = $this->assertTrue($data->cid === $cid, $messages[1]);
      $result = $result && $this->assertTrue($data->created >= $now - 1 && $data->created <= $now + 1, $messages[2]);

      return $result && $this->assertTrue($data->expire == $expire, $messages[3]);
    }

    return FALSE;
  }

  /**
   * Tests flushing the cache.
   */
  public function testFlush(): void {
    $first_backend_bin = 'apc_storage_cache_test';
    $second_backend_bin = 'apc_storage_cache_flush_test';
    $first_backend = $this->getCacheBackend($first_backend_bin);
    $second_backend = $this->getCacheBackend($second_backend_bin);

    $data = array(
      'test1' => 2,
      'test3' => 4,
      'test5' => 8,
    );

    foreach ($data as $cid => $item) {
      $first_backend->set($cid, $item);
      $second_backend->set($cid, $item);
    }

    $this->assertFlushedBin($first_backend_bin);

    foreach ($data as $cid => $item) {
      $message_args = ['@bin' => $first_backend_bin, '@cid' => $cid];
      $message = format_string(
        'Backend for @bin does not contain data for @cid after flushing the cache.',
        $message_args
      );

      $this->assertTrue(empty($first_backend->get($cid)), $message);

      $message_args['@bin'] = $second_backend_bin;
      $message = format_string(
        'Backend for @bin still contains data for @cid after flushing the cache.',
        $message_args
      );

      $this->assertFalse(empty($second_backend->get($cid)), $message);
    }
  }

  /**
   * Tests setting and getting cache items.
   */
  public function testSetGet() : void {
    $backend = $this->getCacheBackend('apc_storage_cache_set_get_test');
    $data = [
      'test_1' => ['bar' => '\\Backdrop\\foo\\Bar'],
      'test_2' => ['value' => 3],
      'test_3' => [$this->randomName() => $this->randomString()],
      'TEST_4' => ['foo' => 'bar'],
    ];

    // Verify that a cached value can be updated.
    foreach ($data as $cid => $item) {
      $backend->set($cid, $item);
      $cached = $backend->get($cid);

      if ($this->assertIsValidCacheObject($cid, $cached, full_check: TRUE)) {
        $message = format_string('The value of @cid is the expected one.', ['@cid' => $cid]);

        $this->assertEqual($item, $cached->data, $message);
      }

      $item[$this->randomName()] = $this->randomString();
      $backend->set($cid, $item);
      $updated_item = $backend->get($cid);

      if ($this->assertIsValidCacheObject($cid, $updated_item)) {
        $message = format_string('The value of @cid has been updated.', ['@cid' => $cid]);

        $this->assertEqual($item, $updated_item->data, $message);
      }
    }

    $this->assertFlushedBin('apc_storage_cache_set_get_test');

    // Verify that a cached item is not affected by changing the original.
    foreach ($data as $cid => $item) {
      $obj_item = (object) $item;
      $obj_item->{$this->randomName()} = $this->randomString();
      $obj_item->object = new stdClass();
      $obj_item->object->{$this->randomName()} = $this->randomString();
      $expected_item = clone $obj_item;
      $backend->set($cid, $obj_item);
      $obj_item->this_should_not_be_in_the_cache = TRUE;
      $cached = $backend->get($cid);

      if ($this->assertIsValidCacheObject($cid, $cached, full_check: TRUE)) {
        $message_args = ['@cid' => $cid];
        $messages = [
          format_string('The value for @cid is not affected by changing the original.', $message_args),
          format_string(
            'The cached value for @cid does not contain the @property property.',
            $message_args + ['@property' => 'this_should_not_be_in_the_cache']
          ),
        ];

        $this->assertEqual($expected_item, $cached->data, $messages[0]);
        $this->assertFalse(isset($cached->data->this_should_not_be_in_the_cache), $messages[1]);

        $cached->data->this_should_not_be_in_the_cache = TRUE;
        $fresh_cached = $backend->get($cid);

        if ($this->assertIsValidCacheObject($cid, $fresh_cached)) {
          $this->assertFalse(isset($fresh_cached->data->this_should_not_be_in_the_cache), $messages[1]);
        }
      }
    }
  }

  /**
   * Tests deleting cache items.
   */
  public function testDelete() : void {
    $backend = $this->getCacheBackend('apc_storage_cache_delete_test');
    $data = [
      'test1' => 1,
      'test2' => 3,
      'test3' => 5,
      'test4' => 'test',
    ];

    foreach ($data as $cid => $item) {
      $backend->set($cid, $item);
      $cached = $backend->get($cid);

      if ($this->assertIsValidCacheObject($cid, $cached)) {
        $message = format_string('Backend does not contain data for @cid.', ['@cid' => $cid]);
        $backend->delete($cid);

        $this->assertTrue(empty($backend->get($cid)), $message);
      }
    }
  }

  /**
   * Tests deleting multiple cache items.
   */
  public function testDeleteMultiple(): void {
    $backend = $this->getCacheBackend('apc_storage_cache_delete_multiple_test');
    $data = array(
      'test1' => 2,
      'test2' => 4,
      'test3' => 8,
    );
    $keys = array_keys($data);

    foreach ($data as $cid => $item) {
      $backend->set($cid, $item);
      $cached = $backend->get($cid);
      $this->assertIsValidCacheObject($cid, $cached);
    }

    $backend->deleteMultiple($keys);

    foreach ($keys as $cid) {
      $message = format_string(
        'Backend does not contain data for @cid after deletion.',
        ['@cid' => $cid]
      );

      $this->assertTrue(empty($backend->get($cid)), $message);
    }
  }

  public function testDeletePrefix(): void {
    $first_backend_bin = 'apc_storage_cache_delete_prefix_test_first_bin';
    $second_backend_bin = 'apc_storage_cache_delete_prefix_test_second_bin';
    $first_backend = $this->getCacheBackend($first_backend_bin);
    $second_backend = $this->getCacheBackend($second_backend_bin);

    $data = [
      'test1' => 1,
      'test2' => 3,
      'test3' => 5,
      $this->randomName() => 'test',
      $this->randomName() => 7,
    ];

    foreach ($data as $cid => $item) {
      $first_backend->set($cid, $item);
      $second_backend->set($cid, $item);

      $cached = $first_backend->get($cid);
      $this->assertIsValidCacheObject($cid, $cached, full_check: TRUE);
      $cached = $second_backend->get($cid);
      $this->assertIsValidCacheObject($cid, $cached, full_check: TRUE);
    }

    $first_backend->deletePrefix('test');

    $keys = array_keys($data);

    // Verify the first three items have been removed.
    foreach (array_slice($keys, 0, 3) as $cid) {
      $message = format_string(
        'Backend for @bin does not contain data for @cid after deletion.',
        ['@bin' => $first_backend_bin, '@cid' => $cid]
      );

      $this->assertTrue(empty($first_backend->get($cid)), $message);
    }

    // Verify the last two items still exist.
    foreach (array_slice($keys, 3) as $cid) {
      $message = format_string(
        'Backend for @bin still contains data for @cid after deletion.',
        ['@bin' => $first_backend_bin, '@cid' => $cid]
      );

      $this->assertFalse(empty($first_backend->get($cid)), $message);
    }

    // Verify the cache bin from which no items have been deleted still has all
    // its items.
    foreach ($data as $cid => $item) {
      $message = format_string(
        'Backend for @bin still contains data for @cid after deletion.',
        ['@bin' => $second_backend_bin, '@cid' => $cid]
      );

      $this->assertFalse(empty($second_backend->get($cid)), $message);
    }
  }

  public function testGetMultiple(): void {
    $backend = $this->getCacheBackend('apc_storage_cache_get_multiple_test');
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

    foreach ($data as $cid => $item) {
      $backend->set($cid, $item);

      // Verify the item has been stored correctly.
      $cached = $backend->get($cid);
      $this->assertIsValidCacheObject($cid, $cached, full_check: TRUE);
    }

    $existing_keys = array_slice(array_keys($data), 4);
    $unexisting_keys = array('test21', 'test24', 'test27', 'test30');
    $requested_keys = array_merge($existing_keys, $unexisting_keys);

    shuffle($requested_keys);

    $cids = $requested_keys;
    $return = $backend->getMultiple($cids);

    foreach ($existing_keys as $cid) {
      $message_args = ['@cid' => $cid];
      $messages = [
        format_string('Backend returned data for @cid.', $message_args),
        format_string('The value of @cid is the expected one.', $message_args),
        format_string('$cids does not contain @cid.', $message_args),
      ];

      $this->assertTrue(!empty($return[$cid]), $messages[0]);
      $this->assertIsValidCacheObject($cid, $return[$cid]);
      $this->assertEqual($return[$cid]->data, $data[$cid], $messages[1]);

      $this->assertFalse(in_array($cid, $cids), $messages[2]);
    }

    foreach ($unexisting_keys as $cid) {
      $message_args = ['@cid' => $cid];
      $messages = [
        format_string('Backend did not return data for @cid.', $message_args),
        format_string('$cids contains @cid.', $message_args),
      ];

      $this->assertFalse(isset($return[$cid]), $messages[0]);
      $this->assertTrue(in_array($cid, $cids), $messages[1]);
    }

    // Verify that passing an indexed array of cache IDs works as well.
    $key = array_keys($data)[0];
    $cids = array('cache:key' => $key);
    $return = $backend->getMultiple($cids);

    $message_args = ['@cid' => $key];
    $messages = [
      format_string('Backend returned data for @cid.', $message_args),
      format_string('The value of @cid is the expected one.', $message_args),
    ];

    $this->assertTrue(isset($return[$key]), $messages[0]);
    $this->assertIsValidCacheObject($key, $return[$key]);
    $this->assertEqual($return[$key]->data, $data[$key], $messages[1]);

    // Test a second time after deleting and setting new keys.
    $keys = array_keys($data);

    $backend->delete($keys[0]);
    $backend->delete($keys[1]);
    $backend->set('test9', 512);

    $data = array_slice($data, 2);
    $existing_keys = array_slice(array_keys($data), 0, 4);
    $unexisting_keys = ['test33', 'test36', 'test39', 'test42'];
    $requested_keys = array_merge($existing_keys, $unexisting_keys);

    shuffle($requested_keys);

    $cids = $requested_keys;
    $return = $backend->getMultiple($cids);

    foreach ($existing_keys as $cid) {
      $message_args = ['@cid' => $cid];
      $messages = [
        format_string('Backend returned data for @cid.', $message_args),
        format_string('The value of @cid is the expected one.', $message_args),
        format_string('$cids does not contain @cid.', $message_args),
      ];

      $this->assertTrue(isset($return[$cid]), $messages[0]);
      $this->assertIsValidCacheObject($cid, $return[$cid]);
      $this->assertEqual($return[$cid]->data, $data[$cid], $messages[1]);
      $this->assertFalse(in_array($cid, $cids), $messages[2]);
    }

    foreach ($unexisting_keys as $cid) {
      $message_args = ['@cid' => $cid];
      $messages = [
        format_string('Backend did not return data for @cid.', $message_args),
        format_string('$cids contains @cid.', $message_args),
      ];

      $this->assertFalse(isset($return[$cid]), $messages[0]);
      $this->assertTrue(in_array($cid, $cids), $messages[1]);
    }
  }

}
