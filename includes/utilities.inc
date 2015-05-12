<?php
/**
 * @file
 * Helpers for Rochester.
 */

/**
 * Retrieves files to be processed given a target.
 *
 * @param array $file_listing
 *   Listing of files to parse.
 * @param string $mask
 *   The string to be used as the mask.
 *
 * @return array
 *   An array containing the files that are children.
 */
function rochdora_retrieve_files_for_object($file_listing, $mask) {
  $files = array();
  foreach ($file_listing as $uri => $value) {
    if (preg_match($mask, $uri)) {
      $files[$uri] = $value;
    }
  }
  ksort($files);
  return $files;
}
