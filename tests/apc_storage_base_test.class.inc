<?php

/**
 * @file
 * Contains ApcStorageBaseTestCase.
 */

declare(strict_types=1);

use Random\Engine\Xoshiro256StarStar;
use Random\IntervalBoundary;
use Random\Randomizer;

/**
 * Base class for the APC Storage module tests.
 */
class ApcStorageBaseTestCase extends BackdropWebTestCase {

  /**
   * {@inheritdoc}
   */
  protected $profile = 'testing';

  /**
   * Whether the test should be skipped.
   *
   * @var bool
   */
  protected bool $skipTest = FALSE;

  /**
   * The random engine used by the tests.
   *
   * @var \Random\Engine\Xoshiro256StarStar
   */
  protected Xoshiro256StarStar $engine;

  /**
   * The random generator used by the tests.
   *
   * @var \Random\Randomizer
   */
  protected Randomizer $randomizer;

  /**
   * {@inheritdoc}
   */
  protected function setUp(): void {
    if (!extension_loaded('apcu') && !apcu_enabled()) {
      $this->skipTest = TRUE;

      $this->pass('This test only runs when the APCu extension is loaded and enabled.');
    }
    else {
      try {
        $this->engine = new Xoshiro256StarStar(hash('sha256', 'apc_storage_test_' . hrtime(TRUE), TRUE));
        $this->randomizer = new Randomizer($this->engine);
      }
      catch (ValueError) {
        // This exception should never be thrown because the seed passed to
        // Random\Engine\Xoshiro256StarStar::__construct() is surely 32 bytes
        // and those are not all zeros.
      }
    }

    parent::setUp('apc_storage');
  }

  /**
   * {@inheritdoc}
   */
  protected function tearDown(): void {
    parent::tearDown();

    if (extension_loaded('apcu') && apcu_enabled()) {
      apcu_clear_cache();
    }
  }

  /**
   * Generates a random floating-point number between two values.
   *
   * @param float $from
   *   The lower bound of the range.
   * @param float $to
   *   The upper bound of the range.
   *
   * @return float
   *   The randomly generated floating-point number.
   */
  protected function randomFloat(float $from = 1.0, float $to = 8.0): float {
    try {
      $min = min($from, $to);
      $max = max($from, $to);

      if ($min === $max) {
        return $min;
      }

      return $this->randomizer->getFloat($min, $max, IntervalBoundary::ClosedClosed);
    }
    catch (ValueError) {
      // This exception should never be thrown.
    }
  }

  /**
   * Generates a random integer between two values.
   *
   * @param int $from
   *   The lower bound of the range.
   * @param int $to
   *   The upper bound of the range.
   *
   * @return int
   *   The randomly generated integer.
   */
  protected function randomInt(int $from = 128, int $to = 65536): int {
    try {
      $min = min($from, $to);
      $max = max($from, $to);

      if ($min === $max) {
        return $min;
      }

      return $this->randomizer->getInt($min, $max);
    }
    catch (ValueError) {
      // This exception should never be thrown.
    }
  }

  /**
   * Returns data to be stored.
   *
   * This method is called from any test that needs to store data either
   * directly in APCu or as cached data.
   */
  protected function storageData(): Generator {
    // Return a multiple of 3 items.
    // Avoid using NULL as value, since that fails tests.
    yield '_01_' . $this->randomName() => array(
      'value' => $this->randomString(),
      'ttl' => $this->randomInt(1, 4),
    );
    yield '_02_' . $this->randomName() => array(
      'value' => TRUE,
      'ttl' => 0,
    );
    yield '_03_' . $this->randomName() => array(
      'value' => FALSE,
      'ttl' => $this->randomInt(2, 5),
    );
    yield '_04_' . $this->randomName() => array(
      'value' => $this->randomFloat(),
      'ttl' => 0,
    );
    yield '_05_' . $this->randomName() => array(
      'value' => $this->randomInt(),
      'ttl' => $this->randomInt(2, 4),
    );
    yield '_06_' . $this->randomName() => array(
      'value' => array(
        $this->randomName() => $this->randomString(),
        $this->randomName() => $this->randomInt(),
        $this->randomName() => $this->randomFloat(),
        $this->randomName() => TRUE,
        $this->randomName() => NULL,
        $this->randomName() => FALSE,
      ),
      'ttl' => $this->randomInt(3, 5),
    );
    yield '_07_' . $this->randomName() => array(
      'value' => $this->randomString(),
      'ttl' => $this->randomInt(1, 3),
    );
    yield '_08_' . $this->randomName() => array(
      'value' => FALSE,
      'ttl' => 0,
    );
    yield '_09_' . $this->randomName() => array(
      'value' => $this->randomFloat(),
      'ttl' => $this->randomInt(1, 3),
    );
  }

  /**
   * Asserts that the APCu storage is empty.
   */
  protected function assertApcuEmpty(): bool {
    $iterator = new APCUIterator('/^.+/', APC_ITER_KEY);

    return $this->assertTrue($iterator->getTotalCount() === 0, 'The APCu storage is empty.');
  }

  /**
   * Asserts that the APCu storage is not empty.
   *
   * @return bool
   *   TRUE if the APCu storage is not empty, FALSE otherwise.
   */
  protected function assertApcuNotEmpty(): bool {
    $iterator = new APCUIterator('/^.+/', APC_ITER_KEY);

    return $this->assertTrue($iterator->getTotalCount() > 0, 'The APCu storage is not empty.');
  }

  /**
   * Asserts that a key-value pair was successfully stored in APCu.
   *
   * @param string $key
   *   The name of the APCu key to store.
   * @param mixed $value
   *   The value to store in APCu.
   * @param int $ttl
   *   The time to live for the stored value.
   *
   * @return bool
   *   TRUE if the key-value pair was stored, FALSE otherwise.
   */
  protected function assertApcuKeySaved(string $key, mixed $value, int $ttl = 0): bool {
    $message = format_string('@key was stored in APCu.', array('@key' => $key));
    $result = apcu_store($key, $value, $ttl);

    return $this->assertTrue($result, $message);
  }

  /**
   * Asserts there is an APCu key matching the given name.
   *
   * @param string $key
   *   The name of the APCu key to search for.
   * @param array $options
   *   An array of options, which can contain the following keys:
   *     - message: A custom message to display upon assertion.
   *     - value: The expected value of the APCu key.
   *
   * @return bool
   *   TRUE if the APCu key exists and its value matches the expected value,
   *   FALSE otherwise.
   */
  protected function assertApcuKeyByName(string $key, array $options = array()): bool {
    $args = array('@key' => $key);

    if (!empty($options['message'])) {
      $string = $options['message'];
    }
    else {
      $string = '@key was found in the APCu storage.';
    }

    $message = format_string($string, $args);

    if ($this->assertTrue(apcu_exists($key), $message)) {
      if (array_key_exists('value', $options)) {
        $message = format_string('The value for @key is the expected one.', array('@key' => $key));

        $retrieved = apcu_fetch($key, $success);

        return $this->assertTrue($success && $retrieved === $options['value'], $message);
      }
    }

    return FALSE;
  }

  /**
   * Asserts that there is not an APCu key matching the given name.
   *
   * @param string $key
   *   The name of the APCu key to search for.
   * @param array $options
   *   An array of options, which can contain the following keys:
   *     - message: A custom message to display upon assertion.
   *
   * @return bool
   *   TRUE if the APCu key exists, FALSE otherwise.
   */
  protected function assertNoApcuKeyByName(string $key, array $options = array()): bool {
    $args = array('@key' => $key);

    if (!empty($options['message'])) {
      $string = $options['message'];
    }
    else {
      $string = '@key was not found in the APCu storage.';
    }

    $message = format_string($string, $args);

    return $this->assertFalse(apcu_exists($key), $message);
  }

  /**
   * Asserts that there are APCu keys matching the given prefix.
   *
   * @param string $key
   *   The prefix to search for in APCu keys, excluding the regular expression
   *   delimiters and the ^ anchor.
   * @param array $options
   *   An array of options, which can contain the following keys:
   *     - message: A custom message to display upon assertion.
   *     - regexp: If TRUE, $key is a regular expression that does not need to
   *       be passed to preg_quote().
   *
   * @return bool
   *   TRUE if there are APCu keys matching the prefix, FALSE otherwise.
   */
  protected function assertApcuKeysByPrefix(string $key, array $options = array()): bool {
    $options += array(
      'message' => 'There are APCu keys matching @key.',
      'regexp' => FALSE,
    );
    $escaped_key = $options['regexp'] ? $key : preg_quote($key, '/');
    $iterator = new APCUIterator("/^$escaped_key/", APC_ITER_KEY);
    $message = format_string(
      $options['message'],
      array('@key' => "/^$escaped_key/")
    );

    return $this->assertTrue($iterator->getTotalCount() > 0, $message);
  }

  /**
   * Asserts that there are no APCu keys matching the given prefix.
   *
   * @param string $key
   *   The prefix to search for in APCu keys, excluding the regular expression
   *   delimiters and the ^ anchor.
   * @param array $options
   *   An array of options, which can contain the following keys:
   *     - message: A custom message to display upon assertion.
   *     - regexp: If TRUE, $key is a regular expression that does not need to
   *       be passed to preg_quote().
   *
   * @return bool
   *   TRUE if there are no APCu keys matching the prefix, FALSE otherwise.
   */
  protected function assertNoApcuKeysByPrefix(string $key, array $options = array()): bool {
    $options += array(
      'message' => 'There are no APCu keys matching @key.',
      'regexp' => FALSE,
    );
    $escaped_key = $options['regexp'] ? $key : preg_quote($key, '/');
    $iterator = new APCUIterator("/^$escaped_key/", APC_ITER_KEY);
    $message = format_string(
      $options['message'],
      array('@key' => "/^$escaped_key/")
    );

    return $this->assertTrue($iterator->getTotalCount() === 0, $message);
  }

}
