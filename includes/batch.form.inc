<?php
/**
 * @file
 * Batch ingest form for volumes.
 */

/**
 * Batch ingest through the UI for Rochester's volumes.
 *
 * @param array $form
 *   An array representing a form within Drupal.
 * @param array $form_state
 *   An array containing the Drupal form state.
 * @param AbstractObject $object
 *   An AbstractObject representing an object within Fedora.
 *
 * @return array
 *   An array representing the form to be rendered.
 */
function rochdora_volume_batch_form($form, $form_state, AbstractObject $object) {
  module_load_include('inc', 'islandora', 'includes/utilities');
  $default_namespace = islandora_get_namespace($object->id);
  $scheme = file_default_scheme();
  $form['target'] = array(
    '#type' => 'managed_file',
    '#title' => t('Zip file'),
    '#description' => t('A Zip file containing a number of volumes to be ingested. The file structure must conform to the specifications as defined in the README.'),
    '#required' => TRUE,
    '#upload_location' => "$scheme://",
    '#upload_validators' => array(
      'file_validate_extensions' => array('zip'),
    ),
  );
  $form['parent'] = array(
    '#type' => 'value',
    '#value' => $object->id,
  );
  $form['namespace'] = array(
    '#type' => 'textfield',
    '#title' => t('Namespace for created objects'),
    '#description' => t('Objects will be constructed within this namespace.'),
    "#default_value" => $default_namespace,
    '#size' => 15,
  );
  $form['ingest_set_immediately'] = array(
    '#type' => 'checkbox',
    '#title' => t('Ingest immediately?'),
    '#description' => t('If not selected, objects will just be preprocessed into the queue of objects to be ingested.'),
    '#default_value' => TRUE,
  );

  $form['submit'] = array(
    '#type' => 'submit',
    '#value' => t('Ingest'),
  );
  return $form;
}

/**
 * Form submit handler.
 *
 * May kick off a batch process.
 */
function rochdora_volume_batch_form_submit($form, $form_state) {
  $target = file_load($form_state['values']['target']);

  $form_state['values']['target'] = $target->uri;

  $connection = islandora_get_tuque_connection();
  $parameters = $form_state['values'] + array(
    'type' => 'zip',
    'parent_relationship_uri' => FEDORA_RELS_EXT_URI,
    'parent_relationship_pred' => 'isMemberOfCollection',
  );
  $preprocessor = new RochdoraBatchPreprocessor($connection, $parameters);
  file_usage_add($target, 'islandora_batch', 'islandora_batch_set', $preprocessor->getSetId());

  if ($form_state['values']['ingest_set_immediately']) {
    islandora_batch_ingest(array(
      'preprocessor' => $preprocessor,
    ));
  }
  else {
    islandora_batch_handle_preprocessor($preprocessor);
  }
}
