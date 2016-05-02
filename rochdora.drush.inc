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
        'rochdora_clean_thumbnail_versions' => array(
            'description' => dt('Removes thumbnail versions where the number of thumbnail versions do not match the number of source versions.'),
            'drupal dependencies' => array('rochdora'),
            'aliases' => array('rctv'),
            'bootstrap' => DRUSH_BOOTSTRAP_DRUPAL_LOGIN,
            'examples' => array('drush -u 1 rctv'),
        ),
        'rochdora_mods_ingest' => array(
            'description' => dt('Load mods into the specified diary'),
            'aliases' => array('rochmi'),
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
                    'description' => dt('The collection to which mods should be added'),
                ),
            )
        ),
        'rochdora_tei_replacer' => array(
            'description' => dt('Replace the tei for the given parent'),
            'aliases' => array('rochtr'),
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
                'parent' => array(
                    'description' => dt('The collection to which tei should be fixed'),
                ),
            )
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

/**
 * Sets a batch to remove extra thumbnail versions.
 */
function drush_rochdora_clean_thumbnail_versions() {
    module_load_include('inc', 'rochdora', 'includes/batch');
    drupal_static_reset('islandora_get_tuque_connection');
    $batch = rochdora_get_thumbnail_version_cleaning_batch();
    batch_set($batch);
    drush_backend_batch_process();
}

/**
 * Sets a batch to remove extra thumbnail versions.
 */
function drush_rochdora_mods_ingest() {
    module_load_include('inc', 'rochdora', 'includes/batch');
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
    
    $batch = rochdora_get_mods_fix_batch($connection, $parameters);
    batch_set($batch);
    drush_backend_batch_process();
}

/**
 * Sets a batch to remove extra thumbnail versions.
 */
function drush_rochdora_tei_replacer() {
    module_load_include('inc', 'rochdora', 'includes/batch');
    drupal_static_reset('islandora_get_tuque_connection');
    
    $connection = islandora_get_tuque_connection();
    $parameters = array(
        'type' => drush_get_option('type'),
        'target' => drush_get_option('target'),
        'parent' => drush_get_option('parent', 'islandora:bookCollection'),
        'parent_relationship_uri' => FEDORA_RELS_EXT_URI,
        'parent_relationship_pred' => 'isMemberOfCollection',
    );
    
    $batch = rochdora_get_tei_replace_batch($connection, $parameters);
    batch_set($batch);
    drush_backend_batch_process();
}
