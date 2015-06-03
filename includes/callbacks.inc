<?php
/**
 * @file
 * Menu callbacks.
 */

/**
 * Menu callback for returning a compound object block.
 */
function rochdora_compound_block(AbstractObject $object) {
  module_load_include('inc', 'islandora_compound_object', 'includes/blocks');
  $compound = islandora_compound_object_navigation_block($object);
  return drupal_json_output(drupal_render($compound));
}
