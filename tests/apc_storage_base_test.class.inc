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
   * Returns a parsable string representation of a variable.
   *
   * This method is similar to drupal_var_export(), but it accepts a single
   * argument, and it outputs an array on a single line.
   *
   * @param mixed $var
   *   The variable to export.
   *
   * @return string
   *   The exported variable.
   */
  protected static function varExport(mixed $var): string {
    if (is_array($var)) {
      if (empty($var)) {
        $output = 'array()';
      }
      else {
        // Don't export keys if the array is non-associative.
        $export_keys = array_values($var) != $var;
        $output = 'array(';

        foreach ($var as $key => $value) {
          $output .= ($export_keys ? self::varExport($key) . ' => ' : '') . self::varExport($value) . ', ';
        }

        $output = rtrim($output, ', ') . ')';
      }
    }
    elseif (is_bool($var)) {
      $output = $var ? 'TRUE' : 'FALSE';
    }
    elseif (is_string($var)) {
      if (str_contains($var, "\n") || str_contains($var, "'")) {
        // If the string contains a line break or a single quote, use the
        // double quote export mode. Encode backslash and double quotes and
        // transform some common control characters.
        $var = str_replace(array('\\', '"', "\n", "\r", "\t"), array('\\\\', '\"', '\n', '\r', '\t'), $var);
        $output = '"' . $var . '"';
      }
      else {
        $output = "'" . $var . "'";
      }
    }
    else {
      $output = var_export($var, TRUE);
    }

    return $output;
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
      'ttl' => $this->randomInt(1, 6),
    );
    yield '_02_' . $this->randomName() => array(
      'value' => TRUE,
      'ttl' => 0,
    );
    yield '_03_' . $this->randomName() => array(
      'value' => FALSE,
      'ttl' => $this->randomInt(2, 8),
    );
    yield '_04_' . $this->randomName() => array(
      'value' => $this->randomFloat(),
      'ttl' => 0,
    );
    yield '_05_' . $this->randomName() => array(
      'value' => $this->randomInt(),
      'ttl' => $this->randomInt(2, 8),
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
      'ttl' => $this->randomInt(1, 7),
    );
    yield '_07_' . $this->randomName() => array(
      'value' => $this->randomString(),
      'ttl' => $this->randomInt(1, 8),
    );
    yield '_08_' . $this->randomName() => array(
      'value' => FALSE,
      'ttl' => 0,
    );
    yield '_09_' . $this->randomName() => array(
      'value' => $this->randomFloat(),
      'ttl' => $this->randomInt(2, 6),
    );
  }

}
