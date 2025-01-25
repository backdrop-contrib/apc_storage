# APC Storage

The APC Storage module integrates the APCu extension with Drupal,
either as cache backend or as lock backend.


## Table of contents

- Requirements
- Installation
- Configuration
- Maintainers


## Requirements

This module requires no modules outside of Drupal core.
It requires the [APCu](https://www.php.net/manual/en/book.apcu.php) extension
(5.0.0 or higher) and PHP 8.3 or higher.


## Installation

Install as you would normally install a contributed Drupal module. For further
information, see
[Installing Drupal Modules](https://www.drupal.org/docs/extending-drupal/installing-drupal-modules).


## Configuration

The module has no menu or modifiable settings.
Most of the configuration values are stored in the settings.php file.

### Cache backend

At least the following line must be added in the settings.php file to use this
module as cache backend. (Replace *sites/all/modules/apc_storage* with the path for the
module, in the case the module is installed in a different directory.)

```php
$conf['cache_backends'][] = 'sites/all/modules/apc_storage/includes/apc_storage_cache.class.inc';
```

Then lines like the following ones must be added for each cache bin to store on
APCu.

```php
$conf['cache_class_cache'] = 'ApcStorageCache';
$conf['cache_class_cache_bootstrap'] = 'ApcStorageCache';
```

What follows `cache_class_` is the name of the cache bin. (In the given example,
the cache bins are *cache* and *cache_bootstrap*.)

APCu normally has a limited memory available (32M) and should be used to cache
the entries which are mostly used, since it is the cache closest to PHP (and
maybe the fastest one).
When the memory allocated by APCu is big enough to cache the entire Drupal
cache, instead of setting each single cache bin, you can use the following line,
which sets APCu as default backend for all the cache bins.

```php
$conf['cache_default_class'] = 'ApcStorageCache';
```

When `ApcStorageCache` is used for the *cache_page* bin, the following lines can
be added to the settings.php file used for the site.

```php
$conf['page_cache_without_database'] = TRUE;
$conf['page_cache_invoke_hooks'] = FALSE;
```

In case of multi-site installations, you can use one of the following lines in
the settings.php file used by each site to avoid that values cached for a site
are returned for all the sites.

```php
$conf['apc_storage_cache_prefix'] = random_bytes(8);
```

It is also possible to provide a different prefix for each cache bin, for
example using the following lines.

```php
$conf['apc_storage_cache_prefix'] = array(
  'cache' => random_bytes(8),
  'cache_bootstrap' => random_bytes(8),
  'default' => random_bytes(8),
);
```

`$conf['apc_storage_cache_prefix']['default']` is used for all the cache bins for which
no prefix is provided.

The APC Storage module uses the database information to avoid
conflicts in case of multi-site installations, but setting
`$conf['apc_storage_cache_prefix']` can help to further avoid conflicts.


### Lock backend

To use the lock backend implemented by the APC Storage module, the
following lines need to be added in the settings.php file. (Replace
*sites/all/modules/apc_storage* with the path for the module, in the case the
module is installed in a different directory.)

```php
$conf['lock_inc'] = 'sites/all/modules/apc_storage/includes/apc_storage_lock.class.inc';
```

Differently from cache backends, only a single lock backend can be used at time.
The lock backend does not have settings.

### Other settings

To enable debugging, add the following line.

```php
$conf['apc_storage_show_details'] = TRUE;
```

When debugging is enabled, each page will show a list of the operations done on
the cache, for example which cache items were removed, which ones were added or
updated.
Only the accounts with the *access APC Storage data* permission will be
able to see that information.


## Maintainers

- Alberto Paderno - [avpaderno](https://www.drupal.org/u/avpaderno) (Drupal 7)
