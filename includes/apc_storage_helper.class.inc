<?php

/**
 * @file
 * Contains ApcStorageHelper.
 */

declare(strict_types=1);

use Random\Engine\Xoshiro256StarStar;
use Random\RandomException;

class ApcStorageHelper {

  static Xoshiro256StarStar $engine;

  /**
   * Generates a unique identifier.
   *
   * This method attempts to generate a 32-byte random identifier using
   * random_bytes(). If random_bytes() fails, a fallback mechanism is used
   * with the Xoshiro256StarStar engine to generate a hash-based identifier.
   *
   * @return string
   *   A unique 32-byte string.
   */
  public static function uniqueId(): string {
    if (!isset(self::$engine)) {
      try {
        self::$engine = new Xoshiro256StarStar(
          hash('sha256', hrtime(TRUE) . 'apc_storage_helper_', TRUE)
        );
      }
      catch (ValueError) {
        // This exception should never be thrown because the seed passed to
        // Random\Engine\Xoshiro256StarStar::__construct() is surely 32 bytes
        // and those are not all zeros.
      }
    }

    try {
      return random_bytes(32);
    }
    catch (RandomException) {
      // Use a fallback in case random_bytes() fails.
      $int = unpack('P', self::$engine->generate())[1] & 0xF;
      $algo = match (TRUE) {
        $int > 8 => 'sha512/256',
        $int > 3 => 'sha3-256',
        $int > 0 => 'sha256',
      };

      return hash($algo, self::$engine->generate(), TRUE);
    }
  }

}
