<?php

/**
 * @file
 * Contains ApcCacheTestBase.
 */

declare(strict_types=1);

require_once __DIR__ . '/apc_test_base.class.inc';

/**
 * Base class for cache tests.
 *
 * @testClass
 */
abstract class ApcCacheTestBase extends ApcTestBase {

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
  protected array $cacheBackends = [];

  /**
   * Gets a cache backend instance.
   *
   * @param ?string $bin
   *   The cache bin.
   *
   * @return BackdropCacheInterface
   *   The cache backend instance.
   */
  protected function getCacheBackend(string $bin = NULL): BackdropCacheInterface {
    $bin = $bin ?? $this->cacheBin;

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

    $this->cacheBackends = array();
    parent::tearDown();
  }

  /**
   * Asserts that the given data is a cache object.
   *
   * @param string $cid
   *   The cache ID.
   * @param mixed $data
   *   The data to check.
   *
   * @return bool
   *   TRUE on success, FALSE on failure.
   */
  protected function assertIsValidCacheObject(string $cid, mixed $data, int $expire = CACHE_PERMANENT): bool {
    $message_args = array('@cid' => $cid);
    $message = format_string('Item @cid is a cache object.', $message_args);
    $is_cache_object = is_object($data) &&
      isset($data->data) &&
      isset($data->created) &&
      isset($data->expire);

    $result = $this->assertTrue($is_cache_object, $message);

    $message = format_string('The creation time of @cid is correct.', $message_args);
    $result = $result && $this->assertTrue($data->created == REQUEST_TIME, $message);

    $message = format_string('The expiration time of @cid is correct.', $message_args);

    return $result && $this->assertTrue($data->expire == $expire, $message);
  }

  /**
   * Tests setting and getting cache items.
   */
  public function testSetGet() : void {
    $backend = $this->getCacheBackend();
    $data = [
      'test1' => ['bar' => '\\Backdrop\\foo\\Bar'],
      'test2' => ['value' => 3],
      'test_' . $this->randomName() => [$this->randomName() => $this->randomString()]
    ];

    foreach ($data as $cid => $item) {
      $message_args = array('@cid' => $cid);
      $backend->set($cid, $item);
      $cached = $backend->get($cid);
      $this->assertIsValidCacheObject($cid, $cached);
      $message = format_string('The value of @cid is the expected one.', $message_args);
      $this->assertEqual($item, $cached->data, $message);
    }

    // Verify that a cached value can be updated.
    foreach ($data as $cid => $item) {
      $item = array_merge($item, array($this->randomName() => $this->randomString()));
      $message_args = array('@cid' => $cid);
      $backend->set($cid, $item);
      $cached = $backend->get($cid);
      $this->assertIsValidCacheObject($cid, $cached);
      $message = format_string('The value of @cid has been updated.', $message_args);
      $this->assertIdentical($item, $cached->data, $message);
    }

    // Verify that a cached object is not affected by changing the original.
    foreach ($data as $cid => $item) {
      $item = (object) $item;
      $cid .= $this->randomName();
      $message_args = array('@cid' => $cid);
      $item->{$this->randomName()} = $this->randomString();
      $item->object = new stdClass();
      $item->object->{$this->randomName()} = $this->randomString();
      $backend->set($cid, $item);
      $expected_item = clone $item;
      $item->this_should_not_be_in_the_cache = TRUE;
      $cached = $backend->get($cid);
      $this->assertIsValidCacheObject($cid, $cached);
      $message = format_string('The value for @cid is the expected one.', $message_args);
      $this->assertEqual($expected_item, $cached->data, $message);
      $message = format_string(
        'The cached value for @cid does not contain the @property property.',
        $message_args + array('@property' => 'this_should_not_be_in_the_cache')
      );
      $this->assertFalse(isset($cached->data->this_should_not_be_in_the_cache), $message);
      $cached->data->this_should_not_be_in_the_cache = TRUE;
      $fresh_cached = $backend->get($cid);
      $this->assertIsValidCacheObject($cid, $fresh_cached);
      $message = format_string(
        'The cached value for @cid does not contain the @property property.',
        $message_args + array('@property' => 'this_should_not_be_in_the_cache')
      );
      $this->assertFalse(isset($fresh_cached->data->this_should_not_be_in_the_cache), $message);
    }

    // Verify that storing a value with a negative expiration time fails.
    foreach ($data as $cid => $item) {
      $message_args = array('@cid' => $cid);
      $message = format_string('Storing @cid with a negative expiration time failed.', $message_args);
      $backend->set($cid, $item, -3);
      $this->assertFalse($backend->get($cid), $message);
    }

    // Verify that cache keys are case-sensitive.
    $cid = 'TEST8';
    $backend->set($cid, 'value');
    $cached = $backend->get($cid);
    $this->assertIsValidCacheObject($cid, $cached);
    $cid = 'test8';
    $message = format_string('Item @cid has not been retrieved from the cache.', array('@cid' => $cid));
    $this->assertFalse($backend->get($cid), $message);

    // Verify that a cache ID with a trailing space and a cache ID with a
    // trailing space are treated as two different IDs.
    $cid = 'trailing-space-test';
    $backend->set($cid, 'Value for trailing-space-test');
    $cached = $backend->get($cid);
    $this->assertIsValidCacheObject($cid, $cached);
    $cid .= ' ';
    $message = format_string('Item "@cid" has not been retrieved from the cache.', array('@cid' => $cid));
    $this->assertFalse($backend->get($cid), $message);
  }

  /**
   * Tests the data types are preserved.
   */
  public function testValueTypeIsKept() : void {
    $backend = $this->getCacheBackend();
    $data = [
      'test1' => 1,
      'test2' => '0',
      'test3' => '',
      'test4' => 12.64,
      'test5' => FALSE,
      'test6' => [1, 3, 5],
    ];

    foreach ($data as $cid => $item) {
      $backend->set($cid, $item);
      $cached = $backend->get($cid);
      $this->assertIsValidCacheObject($cid, $cached);
      $message = format_string('Data type for @cid was preserved.', array('@cid' => $cid));
      $this->assertIdentical($item, $cached->data, $message);
    }  }

  /**
   * Tests deleting cache items.
   */
  public function testDelete() : void {
    $backend = $this->getCacheBackend();
    $data = [
      'test1' => 1,
      'test2' => 3,
      'test3' => 5,
      'test4' => 'test',
    ];

    foreach ($data as $cid => $item) {
      $backend->set($cid, $item);
      $cached = $backend->get($cid);
      $this->assertIsValidCacheObject($cid, $cached);
      $backend->delete($cid);
      $this->assertFalse($backend->get($cid),
        format_string('Backend does not contain data for @cid after deletion.',
          array('@cid' => $cid)
        )
      );
    }
  }

  /**
   * Tests deleting multiple cache items.
   */
  public function testDeleteMultiple(): void {
    $backend = $this->getCacheBackend();
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
        array('@cid' => $cid)
      );
      $this->assertFalse($backend->get($cid), $message);
    }
  }

  public function testDeletePrefix(): void {
    $first_backend = $this->getCacheBackend();
    $second_backend = $this->getCacheBackend('apc_storage_cache_delete_prefix_test');

    $data = array(
      'test1' => 1,
      'test2' => 3,
      'test3' => 5,
      $this->randomName() => 'test',
      $this->randomName() => 7,
    );

    foreach ($data as $cid => $item) {
      $first_backend->set($cid, $item);
      $second_backend->set($cid, $item);
    }

    $first_backend->deletePrefix('test');

    $keys = array_keys($data);

    foreach (array_slice($keys, 0, 3) as $cid) {
      $message = format_string(
        'Backend for @bin does not contain data for @cid after deletion.',
        array('@bin' => 'apc_storage_cache_delete_prefix_test', '@cid' => $cid)
      );
      $this->assertFalse($first_backend->get($cid), $message);
    }

    foreach (array_slice($keys, 3) as $cid) {
      $message = format_string(
        'Backend for @bin still contains data for @cid after deletion.',
        array('@bin' => 'apc_storage_cache_test', '@cid' => $cid)
      );
      $this->assertTrue($first_backend->get($cid), $message);
    }

    foreach ($data as $cid => $item) {
      $message = format_string(
        'Backend for @bin still contains data for @cid after deletion.',
        array('@bin' => 'apc_storage_cache_delete_prefix_test', '@cid' => $cid)
      );
      $this->assertTrue($second_backend->get($cid), $message);
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
      'test3' => 4,
      'test5' => 8,
    );

    foreach ($data as $cid => $item) {
      $first_backend->set($cid, $item);
      $second_backend->set($cid, $item);
    }

    $first_backend->flush();

    foreach ($data as $cid => $item) {
      $message_args = array('@bin' => 'apc_storage_cache_test', '@cid' => $cid);
      $message = format_string(
        'Backend for @bin does not contain data for @cid after flushing the cache.',
        $message_args
      );
      $this->assertFalse($first_backend->get($cid), $message);

      $message_args['@bin'] = 'apc_storage_cache_flush_test';
      $message = format_string(
        'Backend for @bin still contains data for @cid after flushing the cache.',
        $message_args
      );
      $this->assertTrue($second_backend->get($cid), $message);
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
      'test8' => '256',
    );

    foreach ($data as $cid => $item) {
      $backend->set($cid, $item);
    }

    $existing_keys = array_slice(array_keys($data), 4);
    $unexisting_keys = array('test21', 'test24', 'test27', 'test30');
    $requested_keys = array_merge($existing_keys, $unexisting_keys);

    shuffle($requested_keys);

    $cids = $requested_keys;
    $return = $backend->getMultiple($cids);

    foreach ($existing_keys as $cid) {
      $message_args = array('@cid' => $cid);
      $message = format_string('Backend returned data for @cid.', $message_args);

      if ($this->assertTrue(isset($return[$cid]), $message)) {
        $this->assertIsValidCacheObject($cid, $return[$cid]);
        $this->assertEqual($return[$cid]->data, $data[$cid],
          format_string('The value of @cid is the expected one.', $message_args));
      }

      $this->assertFalse(in_array($cid, $cids),
        format_string('$cids does not contain @cid.', $message_args));
    }

    foreach ($unexisting_keys as $cid) {
      $message_args = array('@cid' => $cid);
      $this->assertFalse(isset($return[$cid]),
        format_string('Backend did not return data for @cid.', $message_args));
      $this->assertTrue(in_array($cid, $cids),
        format_string('$cids contains @cid.', $message_args));
    }

    $key = array_keys($data)[0];
    $cids = array('cache:key' => $key);
    $return = $backend->getMultiple($cids);

    $message_args = array('@cid' => $key);
    $message = format_string('Backend returned data for @cid.', $message_args);

    if ($this->assertTrue(isset($return[$key]), $message)) {
      $this->assertIsValidCacheObject($key, $return[$key]);
      $this->assertEqual($return[$key]->data, $data[$key],
        format_string('The value of @cid is the expected one.', $message_args));
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

    foreach ($existing_keys as $cid) {
      $message_args = array('@cid' => $cid);
      $message = format_string('Backend returned data for @cid.', $message_args);

      if ($this->assertTrue(isset($return[$cid]), $message)) {
        $this->assertIsValidCacheObject($cid, $return[$cid]);
        $this->assertEqual($return[$cid]->data, $data[$cid],
          format_string('The value of @cid is the expected one.', $message_args));
      }

      $this->assertFalse(in_array($cid, $cids),
        format_string('$cids does not contain @cid.', $message_args));
    }

    foreach ($unexisting_keys as $cid) {
      $message_args = array('@cid' => $cid);
      $this->assertFalse(isset($return[$cid]),
        format_string('Backend did not return data for @cid.', $message_args));
      $this->assertTrue(in_array($cid, $cids),
        format_string('$cids contains @cid.', $message_args));
    }
  }

}
