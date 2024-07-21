<?php

/**
 * @file
 * Drush hook implementations for the Alternative PHP Cache module.
 */

/**
 * Implements hook_drush_exit().
 */
function apc_drush_exit() {
  if (class_exists('DrupalApcCache')) {
    DrupalApcCache::remoteFlush();
  }
}
