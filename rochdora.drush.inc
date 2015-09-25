<?php
/**
 * @file
 * Drush commands for Rochester.
 */

/**
 * Implements hook_drush_command().
 */
function rochdora_drush_command() {
  return array(
    'rochdora_prepare_diary' => array(
      'description' => dt('Prepare a diary for ingest.'),
      'drupal dependencies' => array('rochdora'),
      'options' => array(
        'type' => array(
          'description' => dt('Either "directory" or "zip".'),
          'required' => TRUE,
        ),
        'target' => array(
          'description' => dt('The target to directory or zip file to scan.'),
          'required' => TRUE,
        ),
        'namespace' => array(
          'description' => dt('The namespace for objects created by this command. Defaults to the namespace set in the Fedora config.'),
          'required' => FALSE,
        ),
        'parent' => array(
          'description' => dt('The collection to which generated items should be added. Only applies to the diary.'),
        ),
      ),
    ),
    'rochdora_fix_diary_labels' => array(
      'description' => dt('Replace the labels for diaries to say "Diary". This applies only to islandora:diaries children with islandora:bookCModel types. You should pass the --user variable into drush with a user that has permission to edit every diary.'),
      'drupal dependencies' => array('islandora'),
      'bootstrap' => DRUSH_BOOTSTRAP_DRUPAL_LOGIN,
    ),
  );
}

/**
 * Prepares a diary to be ingested for Rochester.
 */
function drush_rochdora_prepare_diary() {
  drupal_static_reset('islandora_get_tuque_connection');
  $connection = islandora_get_tuque_connection();
  $parameters = array(
    'type' => drush_get_option('type'),
    'namespace' => drush_get_option('namespace'),
    'target' => drush_get_option('target'),
    'parent' => drush_get_option('parent', 'islandora:bookCollection'),
    'parent_relationship_uri' => FEDORA_RELS_EXT_URI,
    'parent_relationship_pred' => 'isMemberOfCollection',
  );

  $preprocessor = new RochdoraBatchPreprocessor($connection, $parameters);
  islandora_batch_handle_preprocessor($preprocessor);
}

function drush_rochdora_fix_diary_labels() {
  module_load_include('inc', 'rochdora', 'includes/batch');
  batch_set(rochdora_fix_diary_labels_batch());
  drush_backend_batch_process();
}
