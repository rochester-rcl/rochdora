<?php
/**
 * @file
 * Helpers for Rochester.
 */

/**
 * Retrieves files to be processed given a target.
 *
 * @param string $target
 *   The target URI to search from.
 * @param string $mask
 *   The string to be used as the mask.
 *
 * @return array
 *   An array containing the files that are children.
 */
function rochdora_retrieve_files_for_object($target, $mask) {
  $files = array();
  $target_path = drupal_realpath($target);
  $target_length = strlen($target_path) + 1;
  $directory_contents = file_scan_directory(
    $target_path,
    $mask,
    array('recurse' => TRUE)
  );
  foreach ($directory_contents as $uri => $value) {
    $files[substr($uri, $target_length)] = $value;
  }
  ksort($files);
  return $files;
}
