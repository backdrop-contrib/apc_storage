<?php

/**
 * @file
 * Contains ApcTestBase.
 */

declare(strict_types=1);

use Random\Engine\Xoshiro256StarStar;
use Random\IntervalBoundary;
use Random\Randomizer;

/**
 * Base class for the APC Storage tests.
 */
class ApcTestBase extends BackdropWebTestCase {

  /**
   * {@inheritdoc}
   */
  protected $profile = 'testing';

  /**
   * {@inheritdoc}
   */
  protected function checkRequirements(): array {
    $errors = parent::checkRequirements();

    if (!extension_loaded('apcu')) {
      $errors[] = 'The APCu extension is not loaded.';
    }
    elseif (!apcu_enabled()) {
      $errors[] = 'The APCu extension is not enabled.';
    }

    return $errors;
  }

  /**
   * {@inheritdoc}
   */
  protected function setUp(): void {
    parent::setUp('apc_storage');
  }

}
