<?php

/**
 * @file
 * Drush hook implementations for the Alternative PHP Cache module.
 */

/**
 * Implements hook_drush_exit().
 */
function apc_drush_exit() {
  global $base_url;

  if (class_exists('DrupalApcCache')) {
    $requests = DrupalApcCache::pendingRequests();

    if (!empty($requests) && is_array($requests)) {
      if (!function_exists('module_exists')) {
        // Drupal bootstrap did not reach the DRUPAL_BOOTSTRAP_VARIABLES phase,
        // or module_exists() would be defined. This means it is not possible to
        // check if the Alternative PHP Cache module is available.
        // Exit silently.
        return;
      }

      if (!module_exists('apc')) {
        drush_log(
          dt('You need to enable the Alternative PHP Cache module to clear the cache on the server.'),
          'error'
        );
        return;
      }

      $star = serialize('*');

      foreach (array_keys($requests) as $key) {
        if (!empty($requests[$key][$star])) {
          // If there is a request to clear all the entries for a bin, that is
          // the only request which needs to be passed.
          $requests[$key] = array($star => TRUE);
        }
      }

      $args = array(
        'apc.clear_cache' => array(
          variable_get('cron_key', 'drupal'),
          $requests,
        ),
      );

      $uri = $base_url . '/xmlrpc.php';
      $response = xmlrpc($uri, $args);

      if ($response === FALSE) {
        drush_log(
          dt(
            'apc.clear_cache: %err_msg (%err_no)',
            array('%err_msg' => xmlrpc_error(), '%err_no' => xmlrpc_errno())
          ),
          'error'
        );

        if ($base_url == 'http://' . basename(conf_path())) {
          drush_log(
            dt('$base_url might not be set correctly.'),
            'warning'
          );
        }
      }
      else {
        drush_log(
          dt("apc.clear_cache: The cache has been cleared."),
          'success'
        );
      }
    }
  }
}
