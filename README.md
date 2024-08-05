## Installation

### Step 1
  Enable the module and verify the status page (/admin/reports/status) shows
  the APCu extension has been loaded.

### Step 2
  Choose whenever you want the APC extension for all the cache bins used from
  Drupal or just for some cache bins.
  APCu normally has a limited memory available (32M) and should be used to cache
  the entries which are mostly used, since it is the cache closest (and maybe
  fastest) to PHP. When the memory allocated by APCu is big enough to cache the
  entire Drupal cache, you can follow **Step 2b**; otherwise you should follow
  **Step 2a**.

#### Step 2a
  Add the following lines to your settings.php file

  ```php
  /**
   * APC cache settings.
   */
  $conf['cache_backends'][] = 'sites/all/modules/apc/drupal_apc_cache.inc';
  $conf['cache_class_cache'] = 'DrupalAPCCache';
  $conf['cache_class_cache_bootstrap'] = 'DrupalAPCCache';

  // Uncomment the following line to enable debugging.
  // $conf['apc_show_debug'] = TRUE;
  ```

#### Step 2b
  Add the following lines to your settings.php file.

  ```php
  /**
   * APC cache settings.
   */
  $conf['cache_backends'][] = 'sites/all/modules/apc/drupal_apc_cache.inc';
  $conf['cache_default_class'] = 'DrupalAPCCache';

  // The 'cache_form' bin must be assigned to non-volatile storage.
  $conf['cache_class_cache_form'] = 'DrupalDatabaseCache';

  // Uncomment the following line to enable debugging.
  // $conf['apc_show_debug'] = TRUE;
  ```

### Step 3
  Visit your site and verify it still works.

### Step 4 (optional)
  When `DrupalAPCCache` is used for the *cache_page* bin, the following lines
  can be added to the settings.php file used for the site.

  ```php
  $conf['page_cache_without_database'] = TRUE;
  $conf['page_cache_invoke_hooks'] = FALSE;
  ```
