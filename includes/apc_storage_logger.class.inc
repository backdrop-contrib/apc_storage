<?php

/**
 * @file
 * Contains ApcStorageLogger.
 */

declare(strict_types=1);

/**
 * Class for data collection.
 */
class ApcStorageLogger {

  /**
   * The list of operations recorded by the data logger.
   *
   * @var array
   */
  protected static array $operations = array();

  /**
   * Adds an operation to the list of recorded operations.
   *
   * @param array $operation
   *   An associative array containing details of the operation to be recorded.
   */
  public static function addOperation(array $operation): void {
    self::$operations[] = $operation;
  }

  /**
   * Gets the list of the logged operations.
   *
   * @return array
   *   An array of associative arrays containing details of the operations
   *   recorded since the last reset.
   */
  public static function getOperations(): array {
    return self::$operations;
  }

}
