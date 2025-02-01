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
    try {
      return random_bytes(32);
    }
    catch (RandomException) {
      // Use a fallback in case random_bytes() fails.
      try {
        if (!isset(self::$engine)) {
          $engine = new Xoshiro256StarStar(
            hash('sha256', 'apc_storage_helper_' . hrtime(TRUE), TRUE)
          );
        }

        return hash('sha256', $engine->generate(), TRUE);
      }
      catch (ValueError) {
        // This should never happen because the seed passed to
        // Random\Engine\Xoshiro256StarStar::__construct() is surely 32 bytes
        // and those are not all zeros.
        return hash('sha256', 'apc_storage_helper_' . hrtime(TRUE), TRUE);
      }
    }
  }

}
