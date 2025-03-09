<?php

/**
 * @file
 * Contains ApcStorageCacheTestBase.
 */

declare(strict_types=1);

require_once __DIR__ . '/apc_storage_test_base.class.inc';

abstract class ApcStorageCacheTestBase extends ApcStorageTestBase {

  /**
   * The default cache bin used for the tests.
   *
   * @var string
   */
  protected string $cacheBin = 'apc_storage_cache_test';

  /**
   * An array of objects implementing BackdropCacheInterface.
   *
   * @var BackdropCacheInterface[]
   */
  protected array $cacheBackends = array();

  /**
   * Gets a cache backend instance.
   *
   * @param ?string $bin
   *   The cache bin.
   *
   * @return BackdropCacheInterface
   *   The cache backend instance.
   */
  protected function getCacheBackend(?string $bin = NULL): BackdropCacheInterface {
    if (is_null($bin)) {
      $bin = $this->cacheBin;
    }

    if (!isset($this->cacheBackends[$bin])) {
      $this->cacheBackends[$bin] = $this->createCacheBackend($bin);

      // Ensure the backend is empty.
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
   * @return BackdropCacheInterface
   *   The cache backend instance.
   */
  abstract protected function createCacheBackend(string $bin): BackdropCacheInterface;

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

    unset($this->cacheBackends);

    parent::tearDown();
  }

  /**
   * Asserts that the given data is a cache object.
   *
   * @param string $id
   *   The cache ID.
   * @param mixed $data
   *   The data to check.
   *
   * @return bool
   *   TRUE on success, FALSE on failure.
   */
  protected function assertIsValidCacheObject(string $id, mixed $data, int $expire = CACHE_PERMANENT): bool {
    $message_args = array('@id' => $id);
    $message = format_string(
      'Item @id is a cache object.',
      $message_args
    );

    $is_cache_object = is_object($data) && isset($data->data) &&
      isset($data->created) && isset($data->expire);

    $result = $this->assertTrue($is_cache_object, $message);

    $message = format_string(
      'The creation time of @id is correct.',
      $message_args
    );

    $result &= $this->assertTrue($data->created == REQUEST_TIME, $message);

    $message = format_string(
      'The expiration time of @id is correct.',
      $message_args
    );

    $result &= $this->assertTrue($data->expire == $expire, $message);

    return (bool) $result;
  }

  /**
   * Tests setting and getting cache items.
   */
  public function testSetGet() : void {
    $backend = $this->getCacheBackend();
    $data = array(
      'test1' => array('bar' => '\\Backdrop\\foo\\Bar'),
      'test2' => array('value' => 3),
      'test_' . $this->randomName() =>  array($this->randomName() => $this->randomString()),
    );

    foreach ($data as $id => $item) {
      $message_args = array('@id' => $id);

      $backend->set($id, $item);

      $cached = $backend->get($id);

      $this->assertIsValidCacheObject($id, $cached);

      $message = format_string(
        'The value of @id is the expected one.',
        $message_args
      );

      $this->assertEqual($item, $cached->data, $message);
    }

    // Verify that a cached value can be updated.
    foreach ($data as $id => $item) {
      $item += array($this->randomName() => $this->randomString());
      $message_args = array('@id' => $id);

      $backend->set($id, $item);

      $cached = $backend->get($id);

      $this->assertIsValidCacheObject($id, $cached);

      $message = format_string(
        'The value of @id has been updated.',
        $message_args
      );

      $this->assertIdentical($item, $cached->data, $message);
    }

    // Verify that a cached object is not affected by changing the original.
    foreach ($data as $id => $item) {
      $item = (object) $item;
      $id .= $this->randomName();
      $message_args = array('@id' => $id);

      $item->{$this->randomName()} = $this->randomString();
      $item->object = new stdClass();
      $item->object->{$this->randomName()} = $this->randomString();

      $backend->set($id, $item);

      $expected_item = clone $item;

      // Add a property to the original. It should not appear in the cached
      // data.
      $item->this_should_not_be_in_the_cache = TRUE;

      $cached = $backend->get($id);

      $this->assertIsValidCacheObject($id, $cached);

      $message = format_string(
        'The value for @id is the expected one.',
        $message_args
      );

      $this->assertEqual($expected_item, $cached->data, $message);

      $message = format_string(
        'The cached value for @id does not contain the @property property.',
        $message_args + array('@property' => 'this_should_not_be_in_the_cache')
      );

      $this->assertFalse(isset($cached->data->this_should_not_be_in_the_cache), $message);

      // Add a property to the cache data. It should not appear when the data
      // is fetched from cache again.
      $cached->data->this_should_not_be_in_the_cache = TRUE;

      $fresh_cached = $backend->get($id);

      $this->assertIsValidCacheObject($id, $fresh_cached);

      $message = format_string(
        'The cached value for @id does not contain the @property property.',
        $message_args + array('@property' => 'this_should_not_be_in_the_cache')
      );

      $this->assertFalse(isset($fresh_cached->data->this_should_not_be_in_the_cache), $message);
    }

    // Verify that storing a value with a negative expiration time fails.
    foreach ($data as $id => $item) {
      $message_args = array('@id' => $id);
      $message = format_string(
        'Storing @id with a negative expiration time failed.',
        $message_args
      );

      $backend->set($id, $item, -3);

      $this->assertFalse($backend->get($id), $message);
    }

    // Check caching a value with a long key.
    $cid = str_repeat('a', 300);
    $message_args = array('@id' => $cid);

    $backend->set($cid, 'test');

    $cached = $backend->get($cid);

    $this->assertIsValidCacheObject($id, $cached);

    $message = format_string(
      'The value for @id is the expected one.',
      $message_args
    );

    $this->assertEqual('test', $cached->data, $message);

    // Verify that cache keys are case-sensitive.
    $cid = 'TEST8';

    $backend->set($cid, 'value');

    $cached = $backend->get($cid);

    $this->assertIsValidCacheObject($cid, $cached);

    $cid = 'test8';
    $message = format_string(
      'Item @id has not been retrieved from the cache.',
      array('@id' => $cid)
    );

    $this->assertFalse($backend->get($cid), $message);

    // Verify that a cache ID with a trailing space and a cache ID with a
    // trailing space are treated as two different IDs.
    $cid = 'trailing-space-test';

    $backend->set($cid, 'Value for trailing-space-test');

    $cached = $backend->get($cid);

    $this->assertIsValidCacheObject($cid, $cached);

    $cid .= ' ';
    $message = format_string(
      'Item "@id" has not been retrieved from the cache.',
      array('@id' => $cid)
    );

    $this->assertFalse($backend->get($cid), $message);
  }

  /**
   * Tests the data types are preserved.
   */
  public function testValueTypeIsKept() : void {
    $backend = $this->getCacheBackend();
    $data = array(
      'test1' => 1,
      'test2' => '0',
      'test3' => '',
      'test4' => 12.64,
      'test5' => FALSE,
      'test6' => array(1, 3, 5),
    );

    foreach ($data as $id => $item) {
      $backend->set($id, $item);

      $cached = $backend->get($id);

      $this->assertIsValidCacheObject($id, $cached);

      $message = format_string(
        'The data type for @id has been preserved.',
        array('@id' => $id)
      );

      $this->assertIdentical($item, $cached->data, $message);
    }
  }

  /**
   * Tests deleting cache items.
   */
  public function testDelete() : void {
    $backend = $this->getCacheBackend();
    $data = array(
      'test1' => 1,
      'test2' => 3,
      'test3' => 5,
      str_repeat('a', 300) => 'test',
    );

    foreach ($data as $cid => $item) {
      $message_args = array('@id' => $cid);

      $backend->set($cid, $item);

      $cached = $backend->get($cid);

      $this->assertIsValidCacheObject($cid, $cached);

      $backend->delete($cid);

      $message = format_string(
        'Backend does not contain data for @id after deletion.',
        $message_args
      );

      $this->assertFalse($backend->get($cid), $message);
    }
  }

  /**
   * Tests deleting multiple cache items.
   */
  public function testDeleteMultiple() : void {
    $backend = $this->getCacheBackend();
    $data = array(
      'test1' => 2,
      'test2' => 4,
      'test3' => 8,
      str_repeat('a', 300) => 'test',
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
        'Backend does not contain data for @id after deletion.',
        array('@id' => $cid)
      );
      $this->assertFalse($backend->get($cid), $message);
    }
  }

  public function testDeletePrefix() : void {
    $first_backend = $this->getCacheBackend();
    $second_backend = $this->getCacheBackend('apc_storage_cache_delete_prefix_test');

    $data = array(
      'test1' => 1,
      'test2' => 3,
      'test3' => 5,
      str_repeat('a', 300) => 'test',
    );

    foreach ($data as $id => $item) {
      $first_backend->set($id, $item);
      $second_backend->set($id, $item);
    }

    $first_backend->deletePrefix('test');

    foreach (array_slice(array_keys($data), 0, 3) as $id) {
      $message = format_string(
        'Backend for @bin does not contain data for @id after deletion.',
        array('@bin' => 'apc_storage_cache_delete_prefix_test', '@id' => $id)
      );

      $this->assertFalse($first_backend->get($id), $message);
    }

    foreach (array_slice(array_keys($data), 3) as $id) {
      $message = format_string(
        'Backend for @bin still contains data for @id after deletion.',
        array('@bin' => 'apc_storage_cache_test', '@id' => $id)
      );

      $this->assertTrue($first_backend->get($id), $message);
    }

    foreach ($data as $id => $item) {
      $message = format_string(
        'Backend for @bin still contains data for @id after deletion.',
        array('@bin' => 'apc_storage_cache_delete_prefix_test', '@id' => $id)
      );

      $this->assertTrue($second_backend->get($id), $message);
    }
  }

  /**
   * Tests flushing the cache.
   */
  public function testFlush(): void {
    $first_backend = $this->getCacheBackend();
    $second_backend = $this->getCacheBackend('apc_storage_cache_flush_test');
    $data = array(
      'test1' => 2,
      'test2' => 4,
      'test3' => 8,
      str_repeat('a', 300) => 'test',
    );

    foreach ($data as $id => $item) {
      $first_backend->set($id, $item);
      $second_backend->set($id, $item);
    }

    $first_backend->flush();

    foreach ($data as $id => $item) {
      $message = format_string(
        'Backend for @bin does not contain data for @id after flushing the cache.',
        array('@bin' => 'apc_storage_cache_test', '@id' => $id)
      );

      $this->assertFalse($first_backend->get($id), $message);

      $message = format_string(
        'Backend for @bin still contains data for @id after flushing the cache.',
        array('@bin' => 'apc_storage_cache_flush_test', '@id' => $id)
      );

      $this->assertTrue($second_backend->get($id), $message);
    }
  }

  public function testGetMultiple(): void {
    $backend = $this->getCacheBackend();
    $data = array(
      'test1' => 2,
      'test2' => 4,
      'test3' => 8,
      'test4' => 16,
      'test5' => 32,
      'test6' => 64,
      'test7' => 128,
      str_repeat('a', 300) => '256',
    );

    foreach ($data as $id => $item) {
      $backend->set($id, $item);
    }

    $existing_keys = array_slice(array_keys($data), 4);
    $unexisting_keys = array('test21', 'test24', 'test27', 'test30');
    $requested_keys = array_merge($existing_keys, $unexisting_keys);

    shuffle($requested_keys);

    $cids = $requested_keys;
    $return = $backend->getMultiple($cids);

    foreach ($existing_keys as $id) {
      $message_args = array('@id' => $id);
      $message = format_string(
        'Backend returned data for @id.',
        $message_args
      );

      if ($this->assertTrue(isset($return[$id]), $message)) {
        $this->assertIsValidCacheObject($id, $return[$id]);

        $message = format_string(
          'The value of @id is the expected one.',
          $message_args
        );

        $this->assertEqual($return[$id]->data, $data[$id], $message);
      }

      $message = format_string(
        '$cids does not contain @id.',
        $message_args
      );

      $this->assertFalse(in_array($id, $cids), $message);
    }

    foreach ($unexisting_keys as $id) {
      $message_args = array('@id' => $id);
      $message = format_string(
        'Backend did not return data for @id.',
        $message_args
      );

      $this->assertFalse(isset($return[$id]), $message);

      $message = format_string(
        '$cids contains @id.',
        $message_args
      );

      $this->assertTrue(in_array($id, $cids), $message);
    }

    $key = array_keys($data)[0];
    $cids = array('cache:key' => $key);
    $return = $backend->getMultiple($cids);
    $message_args = array('@id' => $key);
    $message = format_string(
      'Backend returned data for @id.',
      $message_args
    );

    if ($this->assertTrue(isset($return[$key]), $message)) {
      $this->assertIsValidCacheObject($key, $return[$key]);

      $message = format_string(
        'The value of @id is the expected one.',
        $message_args
      );

      $this->assertEqual($return[$key]->data, $data[$key], $message);
      $this->assertTrue(empty($cids), '$cids is empty');
    }

    // Test a second time after deleting and setting new keys.
    $keys = array_keys($data);

    $backend->delete($keys[0]);
    $backend->delete($keys[1]);
    $backend->set('test9', 512);

    $data = array_slice($data, 2);
    $existing_keys = array_slice(array_keys($data), 0, 4);
    $unexisting_keys = array('test33', 'test36', 'test39', 'test42');
    $requested_keys = array_merge($existing_keys, $unexisting_keys);

    shuffle($requested_keys);

    $cids = $requested_keys;
    $return = $backend->getMultiple($cids);

    foreach ($existing_keys as $id) {
      $message_args = array('@id' => $id);
      $message = format_string(
        'Backend returned data for @id.',
        $message_args
      );

      if ($this->assertTrue(isset($return[$id]), $message)) {
        $this->assertIsValidCacheObject($id, $return[$id]);

        $message = format_string(
          'The value of @id is the expected one.',
          $message_args
        );

        $this->assertEqual($return[$id]->data, $data[$id], $message);
      }

      $message = format_string(
        '$cids does not contain @id.',
        $message_args
      );

      $this->assertFalse(in_array($id, $cids), $message);
    }

    foreach ($unexisting_keys as $id) {
      $message_args = array('@id' => $id);
      $message = format_string(
        'Backend did not return data for @id.',
        $message_args
      );

      $this->assertFalse(isset($return[$id]), $message);

      $message = format_string(
        '$cids contains @id.',
        $message_args
      );

      $this->assertTrue(in_array($id, $cids), $message);
    }
  }

}
