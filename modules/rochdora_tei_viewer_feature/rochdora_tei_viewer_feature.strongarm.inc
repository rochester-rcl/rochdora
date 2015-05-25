<?php
/**
 * @file
 * rochdora_tei_viewer_feature.strongarm.inc
 */

/**
 * Implements hook_strongarm().
 */
function rochdora_tei_viewer_feature_strongarm() {
  $export = array();

  $strongarm = new stdClass();
  $strongarm->disabled = FALSE; /* Edit this to true to make a default strongarm disabled initially */
  $strongarm->api_version = 1;
  $strongarm->name = 'islandora_book_viewers';
  $strongarm->value = array(
    'name' => array(
      'none' => 'none',
      'islandora_internet_archive_bookreader' => 'islandora_internet_archive_bookreader',
      'islandora_paged_tei_seadragon' => 'islandora_paged_tei_seadragon',
    ),
    'default' => 'islandora_paged_tei_seadragon',
  );
  $export['islandora_book_viewers'] = $strongarm;

  $strongarm = new stdClass();
  $strongarm->disabled = FALSE; /* Edit this to true to make a default strongarm disabled initially */
  $strongarm->api_version = 1;
  $strongarm->name = 'islandora_image_annotation_mappings';
  $strongarm->value = array(
    'islandora:sp_large_image_cmodel' => array(
      'DSID' => 'JPG',
      'TAX' => 'None',
    ),
    'islandora:pageCModel' => array(
      'DSID' => 'JPG',
      'TAX' => 'None',
    ),
  );
  $export['islandora_image_annotation_mappings'] = $strongarm;

  return $export;
}
